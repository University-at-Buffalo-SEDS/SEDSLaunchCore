#include "launchcore/image.h"
#include "launchcore/storage.h"
#include <stddef.h>
#include <string.h>

uint32_t launchcore_image_header_crc(const launchcore_image_header_t *header)
{
    return launchcore_crc32(header, offsetof(launchcore_image_header_t, header_crc32));
}

launchcore_image_status_t launchcore_image_validate_header(const launchcore_image_header_t *header)
{
    if (header == NULL || header->magic != LAUNCHCORE_IMAGE_MAGIC) return LAUNCHCORE_IMAGE_ERR_MAGIC;
    if (header->header_version != LAUNCHCORE_IMAGE_HEADER_VERSION) return LAUNCHCORE_IMAGE_ERR_VERSION;
    if (header->header_size < sizeof(launchcore_image_header_t) || header->image_size == 0u) return LAUNCHCORE_IMAGE_ERR_SIZE;
    if (header->header_crc32 != launchcore_image_header_crc(header)) return LAUNCHCORE_IMAGE_ERR_HEADER_CRC;
    if (header->vector_table_addr < header->slot_base_addr + header->header_size) return LAUNCHCORE_IMAGE_ERR_VECTOR;
    if (header->entry_addr == 0u || (header->entry_addr & 1u) == 0u) return LAUNCHCORE_IMAGE_ERR_VECTOR;
    return LAUNCHCORE_IMAGE_OK;
}

launchcore_image_status_t launchcore_image_validate(const launchcore_image_header_t *header, const void *payload)
{
    launchcore_image_status_t status = launchcore_image_validate_header(header);
    if (status != LAUNCHCORE_IMAGE_OK) return status;
    if (payload == NULL) return LAUNCHCORE_IMAGE_ERR_SIZE;
    if (launchcore_crc32(payload, header->image_size) != header->image_crc32) return LAUNCHCORE_IMAGE_ERR_IMAGE_CRC;
    uint8_t hash[32];
    launchcore_sha256(payload, header->image_size, hash);
    if (memcmp(hash, header->image_sha256, sizeof(hash)) != 0) return LAUNCHCORE_IMAGE_ERR_IMAGE_HASH;
    return LAUNCHCORE_IMAGE_OK;
}

launchcore_image_status_t launchcore_image_validate_stored(const launchcore_image_header_t *header, uint32_t payload_addr)
{
    launchcore_image_status_t status = launchcore_image_validate_header(header);
    if (status != LAUNCHCORE_IMAGE_OK) return status;

    uint8_t chunk[LAUNCHCORE_VALIDATE_CHUNK_SIZE];
    launchcore_sha256_ctx_t sha256;
    uint32_t crc32 = 0u;
    uint32_t remaining = header->image_size;
    launchcore_sha256_init(&sha256);
    while (remaining > 0u) {
        uint32_t chunk_size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (launchcore_storage_driver()->read(payload_addr, chunk, chunk_size) != LAUNCHCORE_STORAGE_OK) {
            return LAUNCHCORE_IMAGE_ERR_STORAGE;
        }
        crc32 = launchcore_crc32_update(crc32, chunk, chunk_size);
        launchcore_sha256_update(&sha256, chunk, chunk_size);
        payload_addr += chunk_size;
        remaining -= chunk_size;
    }
    if (crc32 != header->image_crc32) return LAUNCHCORE_IMAGE_ERR_IMAGE_CRC;

    uint8_t hash[LAUNCHCORE_HASH_SHA256_SIZE];
    launchcore_sha256_final(&sha256, hash);
    if (memcmp(hash, header->image_sha256, sizeof(hash)) != 0) return LAUNCHCORE_IMAGE_ERR_IMAGE_HASH;
    return LAUNCHCORE_IMAGE_OK;
}

uint32_t launchcore_image_packed_version(const launchcore_image_header_t *header)
{
    return ((header->version_major & 0xFFu) << 24u) |
           ((header->version_minor & 0xFFu) << 16u) |
           ((header->version_patch & 0xFFu) << 8u) |
           (header->version_build & 0xFFu);
}
