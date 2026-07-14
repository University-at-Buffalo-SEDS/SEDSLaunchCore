#include "launchcore/bundle.h"
#include "launchcore/config.h"
#include "launchcore/image.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <stddef.h>
#include <string.h>

static bool artifact_range_valid(uint32_t offset, uint32_t size, uint32_t header_size, uint32_t total_size)
{
    return size == 0u ? offset == 0u :
        offset >= header_size && offset <= total_size && size <= total_size - offset;
}

uint32_t launchcore_bundle_header_crc(const launchcore_bundle_header_t *header)
{
    return launchcore_crc32(header, offsetof(launchcore_bundle_header_t, header_crc32));
}

bool launchcore_bundle_header_is_valid(const launchcore_bundle_header_t *header, uint32_t available_size)
{
    if (header == NULL || header->magic != LAUNCHCORE_BUNDLE_MAGIC ||
        header->version != LAUNCHCORE_BUNDLE_VERSION || header->header_size != sizeof(*header) ||
        header->total_size < sizeof(*header) || header->total_size > available_size ||
        header->header_crc32 != launchcore_bundle_header_crc(header) ||
        !artifact_range_valid(header->bootloader_offset, header->bootloader_size, header->header_size, header->total_size) ||
        !artifact_range_valid(header->app_offset, header->app_size, header->header_size, header->total_size) ||
        (header->bootloader_size == 0u && header->app_size == 0u)) return false;
    if (header->bootloader_size != 0u && header->app_size != 0u) {
        uint64_t bootloader_end = (uint64_t)header->bootloader_offset + header->bootloader_size;
        uint64_t app_end = (uint64_t)header->app_offset + header->app_size;
        if (!(bootloader_end <= header->app_offset || app_end <= header->bootloader_offset)) return false;
    }
    return header->bootloader_size == 0u ||
        (header->bootloader_signature_size > 0u &&
         header->bootloader_signature_size <= LAUNCHCORE_BOOTLOADER_SIGNATURE_MAX_SIZE);
}

static void reject_bundle(launchcore_metadata_t *md, launchcore_update_reason_t reason)
{
    md->flags &= ~(LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING |
        LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP |
        LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING);
    md->update_attempted = 1u;
    md->update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md->update_reason = (uint8_t)reason;
    (void)launchcore_metadata_commit(md);
}

bool launchcore_process_staged_bundle(launchcore_metadata_t *md)
{
    uint32_t bundle_flags = LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING |
        LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP;
    if (md == NULL || (md->flags & bundle_flags) == 0u) return true;
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    bool in_app_staging = md->staged_bundle_base == layout->app_staging_base &&
        md->staged_bundle_size <= layout->app_staging_size;
    bool in_bootloader_staging = md->staged_bundle_base == layout->bootloader_staging_base &&
        md->staged_bundle_size <= layout->bootloader_staging_size;
    launchcore_bundle_header_t bundle;
    if ((!in_app_staging && !in_bootloader_staging) ||
        launchcore_storage_driver()->read(md->staged_bundle_base, &bundle, sizeof(bundle)) != LAUNCHCORE_STORAGE_OK ||
        !launchcore_bundle_header_is_valid(&bundle, md->staged_bundle_size)) {
        reject_bundle(md, LAUNCHCORE_UPDATE_REASON_HEADER);
        return true;
    }

    if ((md->flags & LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING) != 0u) {
        if (bundle.bootloader_version > LAUNCHCORE_BOOTLOADER_VERSION) {
            launchcore_bootloader_update_manifest_t manifest;
            memset(&manifest, 0, sizeof(manifest));
            manifest.magic = LAUNCHCORE_BOOTLOADER_UPDATE_MAGIC;
            manifest.version = LAUNCHCORE_BOOTLOADER_UPDATE_VERSION;
            manifest.size = sizeof(manifest);
            manifest.bootloader_image_addr = md->staged_bundle_base + bundle.bootloader_offset;
            manifest.bootloader_image_size = bundle.bootloader_size;
            manifest.bootloader_crc32 = bundle.bootloader_crc32;
            memcpy(manifest.bootloader_sha256, bundle.bootloader_sha256, sizeof(manifest.bootloader_sha256));
            memcpy(manifest.bootloader_signature, bundle.bootloader_signature, sizeof(manifest.bootloader_signature));
            manifest.bootloader_signature_size = bundle.bootloader_signature_size;
            manifest.app_image_addr = bundle.app_size == 0u ? 0u : md->staged_bundle_base + bundle.app_offset;
            manifest.app_image_size = bundle.app_size;
            memcpy(manifest.app_sha256, bundle.app_sha256, sizeof(manifest.app_sha256));
            manifest.required_new_bootloader_version = bundle.bootloader_version;
            manifest.manifest_crc32 = launchcore_bootloader_update_manifest_crc(&manifest);
            (void)launchcore_bootloader_update_apply_staged(&manifest);
            return false;
        }
        md->flags &= ~LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING;
        md->flags |= LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP;
        if (launchcore_metadata_commit(md) != LAUNCHCORE_METADATA_OK) return false;
    }

    if ((md->flags & LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP) != 0u) {
        md->flags &= ~LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP;
        if (bundle.app_size != 0u) {
            launchcore_image_header_t app_header;
            if (launchcore_storage_driver()->read(md->staged_bundle_base + bundle.app_offset,
                &app_header, sizeof(app_header)) != LAUNCHCORE_STORAGE_OK ||
                launchcore_image_validate_header(&app_header) != LAUNCHCORE_IMAGE_OK) {
                reject_bundle(md, LAUNCHCORE_UPDATE_REASON_HEADER);
                return true;
            }
            md->flags |= LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING;
            launchcore_slot_id_t active = launchcore_slot_id_is_valid((launchcore_slot_id_t)md->active_slot) ?
                (launchcore_slot_id_t)md->active_slot : (launchcore_slot_id_t)md->confirmed_slot;
            md->update_slot = launchcore_storage_driver()->layout()->slot_b_size == 0u ? LAUNCHCORE_SLOT_A :
                (active == LAUNCHCORE_SLOT_A ? LAUNCHCORE_SLOT_B : LAUNCHCORE_SLOT_A);
            md->staged_app_addr = md->staged_bundle_base + bundle.app_offset;
            md->staged_app_size = bundle.app_size;
            md->staged_app_version = launchcore_image_packed_version(&app_header);
            memcpy(md->staged_app_hash, bundle.app_sha256, sizeof(md->staged_app_hash));
            md->update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
            md->update_attempted = 0u;
            md->update_result = LAUNCHCORE_UPDATE_RESULT_PENDING;
        } else {
            md->update_kind = LAUNCHCORE_UPDATE_KIND_BOOTLOADER;
            md->update_attempted = 1u;
            md->update_result = LAUNCHCORE_UPDATE_RESULT_SUCCESS;
            md->update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
        }
        if (launchcore_metadata_commit(md) != LAUNCHCORE_METADATA_OK) return false;
    }
    return true;
}
