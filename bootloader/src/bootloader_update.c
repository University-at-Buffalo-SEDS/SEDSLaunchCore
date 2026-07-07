#include "launchcore/bootloader_update.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/platform.h"
#include "launchcore/storage.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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

    uint8_t *image = (uint8_t *)malloc(manifest->bootloader_image_size);
    if (image == NULL) return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE;
    if (launchcore_storage_driver()->read(manifest->bootloader_image_addr, image, manifest->bootloader_image_size) != LAUNCHCORE_STORAGE_OK) {
        free(image);
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE;
    }

    if (launchcore_crc32(image, manifest->bootloader_image_size) != manifest->bootloader_crc32) {
        free(image);
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_BOOTLOADER_CRC;
    }
    uint8_t hash[32];
    launchcore_sha256(image, manifest->bootloader_image_size, hash);
    free(image);
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
    if (status != LAUNCHCORE_BOOTLOADER_UPDATE_OK) return status;

    if (!platform_flash_bootloader_image(manifest->bootloader_image_addr, manifest->bootloader_image_size)) {
        return LAUNCHCORE_BOOTLOADER_UPDATE_ERR_STORAGE;
    }

    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.flags &= ~LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING;
    md.flags |= LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP;
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
