#include "launchcore/boot.h"
#include "launchcore/bundle.h"
#include "launchcore/delta.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/platform.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <stddef.h>
#include <string.h>

static launchcore_update_reason_t image_failure_reason(launchcore_image_status_t status)
{
    if (status == LAUNCHCORE_IMAGE_ERR_SIZE) return LAUNCHCORE_UPDATE_REASON_SIZE;
    if (status == LAUNCHCORE_IMAGE_ERR_IMAGE_CRC) return LAUNCHCORE_UPDATE_REASON_CRC32;
    if (status == LAUNCHCORE_IMAGE_ERR_IMAGE_HASH) return LAUNCHCORE_UPDATE_REASON_SHA256;
    if (status == LAUNCHCORE_IMAGE_ERR_STORAGE) return LAUNCHCORE_UPDATE_REASON_STORAGE;
    return LAUNCHCORE_UPDATE_REASON_HEADER;
}

static bool reject_pending(launchcore_metadata_t *md, launchcore_slot_id_t slot,
    launchcore_update_reason_t reason)
{
    launchcore_metadata_set_slot_state(md, slot, LAUNCHCORE_SLOT_BAD);
    md->pending_slot = LAUNCHCORE_SLOT_NONE;
    md->boot_attempts = 0u;
    md->update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md->update_attempted = 1u;
    md->update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md->update_reason = (uint8_t)reason;
    return launchcore_metadata_commit(md) == LAUNCHCORE_METADATA_OK;
}

int main(void)
{
    platform_early_init();
    platform_clock_init();
    platform_external_ram_init();
    platform_external_flash_init();
    if (launchcore_storage_init() != LAUNCHCORE_STORAGE_OK) {
        launchcore_recovery_run();
        return 0;
    }

    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    if (!launchcore_process_staged_bundle(&md)) {
        launchcore_recovery_run();
        return 0;
    }
    launchcore_delta_status_t delta_status = launchcore_process_delta_update(&md);
    if (delta_status == LAUNCHCORE_DELTA_RETRY) {
        launchcore_recovery_run();
        return 0;
    }
    launchcore_app_install_status_t install_status = launchcore_install_staged_application(&md);
    if (install_status == LAUNCHCORE_APP_INSTALL_RETRY) {
        launchcore_recovery_run();
        return 0;
    }
    launchcore_slot_id_t pending_slot = (launchcore_slot_id_t)md.pending_slot;
    if (launchcore_slot_id_is_valid(pending_slot) &&
        launchcore_metadata_slot_state(&md, pending_slot) == LAUNCHCORE_SLOT_PENDING &&
        md.boot_attempts >= md.max_boot_attempts) {
        if (pending_slot == LAUNCHCORE_SLOT_A &&
            (md.flags & LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_AVAILABLE) != 0u) {
            md.flags |= LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING;
            if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK ||
                launchcore_process_delta_update(&md) != LAUNCHCORE_DELTA_ROLLED_BACK) {
                launchcore_recovery_run();
                return 0;
            }
            pending_slot = LAUNCHCORE_SLOT_NONE;
        }
    }
    if (launchcore_slot_id_is_valid(pending_slot) &&
        launchcore_metadata_slot_state(&md, pending_slot) == LAUNCHCORE_SLOT_PENDING &&
        md.boot_attempts >= md.max_boot_attempts) {
        if (!reject_pending(&md, pending_slot, LAUNCHCORE_UPDATE_REASON_NOT_CONFIRMED)) {
            launchcore_recovery_run();
            return 0;
        }
    }
    for (unsigned int attempt = 0u; attempt < 2u; ++attempt) {
        launchcore_slot_id_t slot = launchcore_select_slot(&md);
        if (slot == LAUNCHCORE_SLOT_NONE) break;

        launchcore_image_header_t header;
        uint32_t base = launchcore_slot_base(slot);
        uint32_t slot_size = launchcore_slot_size(slot);
        bool pending = md.pending_slot == (uint8_t)slot &&
            launchcore_metadata_slot_state(&md, slot) == LAUNCHCORE_SLOT_PENDING;
        launchcore_update_reason_t failure_reason = LAUNCHCORE_UPDATE_REASON_NONE;
        launchcore_image_status_t image_status = LAUNCHCORE_IMAGE_OK;
        if (storage->read(base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK) {
            failure_reason = LAUNCHCORE_UPDATE_REASON_STORAGE;
        } else if ((image_status = launchcore_image_validate_header(&header)) != LAUNCHCORE_IMAGE_OK) {
            failure_reason = image_failure_reason(image_status);
        } else if (header.header_size > slot_size || header.image_size > slot_size - header.header_size) {
            failure_reason = LAUNCHCORE_UPDATE_REASON_SIZE;
        } else if (pending) {
            image_status = launchcore_image_validate_stored(&header, base + header.header_size);
            if (image_status != LAUNCHCORE_IMAGE_OK) {
                failure_reason = image_failure_reason(image_status);
            } else {
                const uint8_t *expected_hash = slot == LAUNCHCORE_SLOT_A ? md.slot_a_hash : md.slot_b_hash;
                if (memcmp(expected_hash, header.image_sha256, LAUNCHCORE_HASH_SHA256_SIZE) != 0) {
                    failure_reason = LAUNCHCORE_UPDATE_REASON_METADATA_HASH;
                }
            }
        }

        if (failure_reason != LAUNCHCORE_UPDATE_REASON_NONE) {
            if (!pending) break;
            if (!reject_pending(&md, slot, failure_reason)) break;
            continue;
        }
        if (pending) {
            md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
            md.update_attempted = 1u;
            md.update_result = LAUNCHCORE_UPDATE_RESULT_VALIDATED;
            md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
            ++md.boot_attempts;
            if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) break;
        }
        if ((header.image_flags & LAUNCHCORE_IMAGE_FLAG_XIP) != 0u) {
            (void)storage->enable_memory_mapped();
        }
        launchcore_jump_to_image(header.vector_table_addr);
        break;
    }
    launchcore_recovery_run();
    return 0;
}
