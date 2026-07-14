#include "launchcore/boot.h"
#include "launchcore/image.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <string.h>

static launchcore_update_reason_t validation_reason(launchcore_image_status_t status)
{
    if (status == LAUNCHCORE_IMAGE_ERR_SIZE) return LAUNCHCORE_UPDATE_REASON_SIZE;
    if (status == LAUNCHCORE_IMAGE_ERR_IMAGE_CRC) return LAUNCHCORE_UPDATE_REASON_CRC32;
    if (status == LAUNCHCORE_IMAGE_ERR_IMAGE_HASH) return LAUNCHCORE_UPDATE_REASON_SHA256;
    if (status == LAUNCHCORE_IMAGE_ERR_STORAGE) return LAUNCHCORE_UPDATE_REASON_STORAGE;
    return LAUNCHCORE_UPDATE_REASON_HEADER;
}

static launchcore_app_install_status_t fail_install(launchcore_metadata_t *md,
    launchcore_update_reason_t reason, bool retry)
{
    md->update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md->update_attempted = 1u;
    md->update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md->update_reason = (uint8_t)reason;
    if (!retry) {
        md->flags &= ~LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING;
        md->update_slot = LAUNCHCORE_SLOT_NONE;
    }
    if (launchcore_metadata_commit(md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_APP_INSTALL_RETRY;
    return retry ? LAUNCHCORE_APP_INSTALL_RETRY : LAUNCHCORE_APP_INSTALL_REJECTED;
}

launchcore_app_install_status_t launchcore_install_staged_application(launchcore_metadata_t *md)
{
    if (md == NULL || (md->flags & LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING) == 0u) {
        return LAUNCHCORE_APP_INSTALL_NONE;
    }
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    launchcore_slot_id_t target = (launchcore_slot_id_t)md->update_slot;
    if (!launchcore_slot_id_is_valid(target)) return fail_install(md, LAUNCHCORE_UPDATE_REASON_SIZE, false);
    uint32_t target_base = launchcore_slot_base(target);
    uint32_t target_size = launchcore_slot_size(target);
    uint32_t staging_base = md->staged_app_addr != 0u ? md->staged_app_addr : layout->app_staging_base;
    uint64_t staging_end = (uint64_t)staging_base + md->staged_app_size;
    uint64_t configured_staging_end = (uint64_t)layout->app_staging_base + layout->app_staging_size;
    if (layout->app_staging_size == 0u ||
        staging_base < layout->app_staging_base || staging_end > configured_staging_end) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_SIZE, false);
    }

    launchcore_image_header_t header;
    if (storage->read(staging_base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_STORAGE, true);
    }
    launchcore_image_status_t image_status = launchcore_image_validate_header(&header);
    if (image_status != LAUNCHCORE_IMAGE_OK) return fail_install(md, validation_reason(image_status), false);
    if (header.header_size > md->staged_app_size || header.image_size != md->staged_app_size - header.header_size ||
        md->staged_app_size > layout->app_staging_size || md->staged_app_size > target_size ||
        header.slot_base_addr != target_base ||
        header.vector_table_addr < target_base + header.header_size ||
        header.vector_table_addr >= (uint64_t)target_base + md->staged_app_size) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_SIZE, false);
    }
    image_status = launchcore_image_validate_stored(&header, staging_base + header.header_size);
    if (image_status != LAUNCHCORE_IMAGE_OK) return fail_install(md, validation_reason(image_status), false);
    if (memcmp(md->staged_app_hash, header.image_sha256, sizeof(md->staged_app_hash)) != 0) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_METADATA_HASH, false);
    }

    if (!launchcore_storage_range_preserves_persistent(target_base, target_size)) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_SIZE, false);
    }
    if (storage->erase(target_base, target_size) != LAUNCHCORE_STORAGE_OK) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_FLASH, true);
    }
    uint8_t chunk[LAUNCHCORE_VALIDATE_CHUNK_SIZE];
    uint32_t offset = 0u;
    while (offset < md->staged_app_size) {
        uint32_t chunk_size = md->staged_app_size - offset;
        if (chunk_size > sizeof(chunk)) chunk_size = sizeof(chunk);
        if (storage->read(staging_base + offset, chunk, chunk_size) != LAUNCHCORE_STORAGE_OK ||
            storage->write(target_base + offset, chunk, chunk_size) != LAUNCHCORE_STORAGE_OK) {
            return fail_install(md, LAUNCHCORE_UPDATE_REASON_FLASH, true);
        }
        offset += chunk_size;
    }
    launchcore_image_header_t installed;
    if (storage->read(target_base, &installed, sizeof(installed)) != LAUNCHCORE_STORAGE_OK) {
        return fail_install(md, LAUNCHCORE_UPDATE_REASON_STORAGE, true);
    }
    image_status = launchcore_image_validate_stored(&installed, target_base + installed.header_size);
    if (image_status != LAUNCHCORE_IMAGE_OK ||
        memcmp(md->staged_app_hash, installed.image_sha256, sizeof(md->staged_app_hash)) != 0) {
        return fail_install(md, validation_reason(image_status), true);
    }

    md->flags &= ~LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING;
    md->pending_slot = (uint8_t)target;
    md->update_slot = LAUNCHCORE_SLOT_NONE;
    md->boot_attempts = 0u;
    if (target == LAUNCHCORE_SLOT_A) {
        md->slot_a_version = md->staged_app_version;
        memcpy(md->slot_a_hash, md->staged_app_hash, sizeof(md->slot_a_hash));
    } else {
        md->slot_b_version = md->staged_app_version;
        memcpy(md->slot_b_hash, md->staged_app_hash, sizeof(md->slot_b_hash));
    }
    launchcore_metadata_set_slot_state(md, target, LAUNCHCORE_SLOT_PENDING);
    md->update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md->update_attempted = 1u;
    md->update_result = LAUNCHCORE_UPDATE_RESULT_VALIDATED;
    md->update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    if (launchcore_metadata_commit(md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_APP_INSTALL_RETRY;
    return LAUNCHCORE_APP_INSTALL_READY;
}
