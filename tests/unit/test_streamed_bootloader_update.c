#include "launchcore/bootloader_update.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <assert.h>
#include <string.h>

#define BOOTLOADER_SIZE (LAUNCHCORE_VALIDATE_CHUNK_SIZE * 4u + 29u)
#define STAGING_ADDRESS 0x00800000u

bool platform_verify_bootloader_signature(const uint8_t hash[32], const uint8_t *signature, uint32_t signature_len)
{
    (void)hash;
    return signature != NULL && signature_len == 1u && signature[0] == 0xA5u;
}

void platform_system_reset(void)
{
}

int main(void)
{
    uint8_t image[BOOTLOADER_SIZE];
    for (size_t i = 0; i < sizeof(image); ++i) image[i] = (uint8_t)(i * 17u + 3u);
    assert(launchcore_storage_init() == LAUNCHCORE_STORAGE_OK);
    for (size_t offset = 0; offset < sizeof(image); offset += 91u) {
        size_t len = sizeof(image) - offset;
        if (len > 91u) len = 91u;
        assert(launchcore_storage_driver()->write(STAGING_ADDRESS + (uint32_t)offset, image + offset, (uint32_t)len) == LAUNCHCORE_STORAGE_OK);
    }

    launchcore_bootloader_update_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    manifest.magic = LAUNCHCORE_BOOTLOADER_UPDATE_MAGIC;
    manifest.version = LAUNCHCORE_BOOTLOADER_UPDATE_VERSION;
    manifest.size = sizeof(manifest);
    manifest.bootloader_image_addr = STAGING_ADDRESS;
    manifest.bootloader_image_size = sizeof(image);
    manifest.bootloader_crc32 = launchcore_crc32(image, sizeof(image));
    launchcore_sha256(image, sizeof(image), manifest.bootloader_sha256);
    manifest.bootloader_signature[0] = 0xA5u;
    manifest.bootloader_signature_size = 1u;
    manifest.manifest_crc32 = launchcore_bootloader_update_manifest_crc(&manifest);
    assert(launchcore_bootloader_update_verify_staged(&manifest) == LAUNCHCORE_BOOTLOADER_UPDATE_OK);

    image[BOOTLOADER_SIZE - 1u] ^= 1u;
    assert(launchcore_storage_driver()->write(STAGING_ADDRESS + BOOTLOADER_SIZE - 1u,
        &image[BOOTLOADER_SIZE - 1u], 1u) == LAUNCHCORE_STORAGE_OK);
    assert(launchcore_bootloader_update_apply_staged(&manifest) ==
        LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_CRC);
    launchcore_metadata_t md;
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert(md.update_kind == LAUNCHCORE_UPDATE_KIND_BOOTLOADER);
    assert(md.update_attempted == 1u);
    assert(md.update_result == LAUNCHCORE_UPDATE_RESULT_FAILED);
    assert(md.update_reason == LAUNCHCORE_UPDATE_REASON_CRC32);
    return 0;
}
