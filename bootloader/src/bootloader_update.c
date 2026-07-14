#include "launchcore/bootloader_update.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/platform.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <stddef.h>
#include <string.h>

static launchcore_update_reason_t bootloader_failure_reason(launchcore_bootloader_update_status_t status)
{
    if (status == LAUNCHCORE_BOOTLOADER_UPDATE_ERR_SIZE) return LAUNCHCORE_UPDATE_REASON_SIZE;
    if (status == LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_CRC) return LAUNCHCORE_UPDATE_REASON_CRC32;
    if (status == LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_HASH) return LAUNCHCORE_UPDATE_REASON_SHA256;
    if (status == LAUNCHCORE_BOOTLOADER_UPDATE_ERR_SIGNATURE) return LAUNCHCORE_UPDATE_REASON_SIGNATURE;
    if (status == LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE) return LAUNCHCORE_UPDATE_REASON_STORAGE;
    return LAUNCHCORE_UPDATE_REASON_HEADER;
}

static void record_bootloader_failure(launchcore_bootloader_update_status_t status)
{
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.flags &= ~(LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING |
        LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP);
    md.update_kind = LAUNCHCORE_UPDATE_KIND_BOOTLOADER;
    md.update_attempted = 1u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md.update_reason = (uint8_t)bootloader_failure_reason(status);
    (void)launchcore_metadata_commit(&md);
}

uint32_t launchcore_bootloader_update_manifest_crc(const launchcore_bootloader_update_manifest_t *manifest)
{
    return launchcore_crc32(manifest, offsetof(launchcore_bootloader_update_manifest_t, manifest_crc32));
}

launchcore_bootloader_update_status_t launchcore_bootloader_update_validate_manifest(const launchcore_bootloader_update_manifest_t *manifest)
{
    if (manifest == NULL || manifest->magic != LAUNCHCORE_BOOTLOADER_UPDATE_MAGIC) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_MAGIC;
    }
    if (manifest->version != LAUNCHCORE_BOOTLOADER_UPDATE_VERSION) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_VERSION;
    }
    if (manifest->size < sizeof(*manifest) ||
        manifest->bootloader_image_size == 0u ||
        manifest->bootloader_signature_size == 0u ||
        manifest->bootloader_signature_size > LAUNCHCORE_BOOTLOADER_SIGNATURE_MAX_SIZE) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_SIZE;
    }
    if (manifest->manifest_crc32 != launchcore_bootloader_update_manifest_crc(manifest)) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_MANIFEST_CRC;
    }
    return LAUNCHCORE_BOOTLOADER_UPDATE_OK;
}

launchcore_bootloader_update_status_t launchcore_bootloader_update_verify_staged(const launchcore_bootloader_update_manifest_t *manifest)
{
    launchcore_bootloader_update_status_t status = launchcore_bootloader_update_validate_manifest(manifest);
    if (status != LAUNCHCORE_BOOTLOADER_UPDATE_OK) return status;

    uint8_t chunk[LAUNCHCORE_VALIDATE_CHUNK_SIZE];
    launchcore_sha256_ctx_t sha256;
    uint32_t crc32 = 0u;
    uint32_t remaining = manifest->bootloader_image_size;
    uint32_t address = manifest->bootloader_image_addr;
    launchcore_sha256_init(&sha256);
    while (remaining > 0u) {
        uint32_t chunk_size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (launchcore_storage_driver()->read(address, chunk, chunk_size) != LAUNCHCORE_STORAGE_OK) {
            return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE;
        }
        crc32 = launchcore_crc32_update(crc32, chunk, chunk_size);
        launchcore_sha256_update(&sha256, chunk, chunk_size);
        address += chunk_size;
        remaining -= chunk_size;
    }

    if (crc32 != manifest->bootloader_crc32) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_CRC;
    }
    uint8_t hash[32];
    launchcore_sha256_final(&sha256, hash);
    if (memcmp(hash, manifest->bootloader_sha256, sizeof(hash)) != 0) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_HASH;
    }
    if (!platform_verify_bootloader_signature(hash, manifest->bootloader_signature, manifest->bootloader_signature_size)) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_SIGNATURE;
    }
    return LAUNCHCORE_BOOTLOADER_UPDATE_OK;
}

launchcore_bootloader_update_status_t launchcore_bootloader_update_apply_staged(const launchcore_bootloader_update_manifest_t *manifest)
{
    launchcore_bootloader_update_status_t status = launchcore_bootloader_update_verify_staged(manifest);
    if (status != LAUNCHCORE_BOOTLOADER_UPDATE_OK) {
        record_bootloader_failure(status);
        return status;
    }

    if (!platform_flash_bootloader_image(manifest->bootloader_image_addr, manifest->bootloader_image_size)) {
        launchcore_metadata_t failed;
        (void)launchcore_metadata_read_latest(&failed);
        failed.flags &= ~(LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING |
            LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP);
        failed.update_kind = LAUNCHCORE_UPDATE_KIND_BOOTLOADER;
        failed.update_attempted = 1u;
        failed.update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
        failed.update_reason = LAUNCHCORE_UPDATE_REASON_FLASH;
        (void)launchcore_metadata_commit(&failed);
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE;
    }

    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.flags &= ~LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING;
    md.flags |= LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP;
    md.update_kind = LAUNCHCORE_UPDATE_KIND_BOOTLOADER;
    md.update_attempted = 1u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_SUCCESS;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_METADATA;
    }
    platform_system_reset();
    return LAUNCHCORE_BOOTLOADER_UPDATE_OK;
}

launchcore_bootloader_update_status_t launchcore_bootloader_update_continue_app_install(const launchcore_bootloader_update_manifest_t *manifest)
{
    launchcore_bootloader_update_status_t status = launchcore_bootloader_update_validate_manifest(manifest);
    if (status != LAUNCHCORE_BOOTLOADER_UPDATE_OK) return status;

    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.flags &= ~LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP;
    md.pending_slot = LAUNCHCORE_SLOT_NONE;
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_METADATA;
    }
    return LAUNCHCORE_BOOTLOADER_UPDATE_OK;
}

__attribute__((weak)) bool platform_verify_bootloader_signature(const uint8_t hash[32], const uint8_t *signature, uint32_t signature_len)
{
    (void)hash;
    (void)signature;
    (void)signature_len;
    return false;
}

__attribute__((weak)) bool platform_flash_bootloader_image(uint32_t source_addr, uint32_t image_size)
{
    (void)source_addr;
    (void)image_size;
    return false;
}
