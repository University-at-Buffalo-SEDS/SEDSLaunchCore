#include "update_state.h"
#include "launchcore/image.h"
#include "launchcore/storage.h"
#include <string.h>

launchcore_status_t launchcore_update_finish(void)
{
    launchcore_update_state_t *state = launchcore_update_state();
    if (!state->active) return LAUNCHCORE_ERR_BAD_STATE;
    uint32_t base = state->write_base;
    launchcore_image_header_t header;
    if (launchcore_storage_driver()->read(base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_ERR_FLASH;
    if (launchcore_image_validate_header(&header) != LAUNCHCORE_IMAGE_OK) return LAUNCHCORE_ERR_BAD_IMAGE;
    if (header.header_size > state->info.image_size ||
        header.image_size != state->info.image_size - header.header_size ||
        state->offset != state->info.image_size) return LAUNCHCORE_ERR_BAD_IMAGE;
    launchcore_image_status_t image_status = launchcore_image_validate_stored(&header, base + header.header_size);
    if (image_status == LAUNCHCORE_IMAGE_ERR_STORAGE) return LAUNCHCORE_ERR_FLASH;
    if (image_status != LAUNCHCORE_IMAGE_OK) return LAUNCHCORE_ERR_BAD_IMAGE;
    if (launchcore_expected_hash_is_set(state->info.expected_sha256) &&
        memcmp(state->info.expected_sha256, header.image_sha256, sizeof(header.image_sha256)) != 0) {
        return LAUNCHCORE_ERR_VERIFY;
    }
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.boot_attempts = 0u;
    md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md.update_attempted = 0u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_PENDING;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    if (state->staged_install) {
        md.flags |= LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING;
        md.update_slot = LAUNCHCORE_SLOT_A;
        md.staged_app_size = state->info.image_size;
        md.staged_app_version = launchcore_image_packed_version(&header);
        md.staged_app_addr = state->write_base;
        memcpy(md.staged_app_hash, header.image_sha256, sizeof(md.staged_app_hash));
    } else {
        md.pending_slot = (uint8_t)state->slot;
        md.update_slot = LAUNCHCORE_SLOT_NONE;
        launchcore_metadata_set_slot_state(&md, state->slot, LAUNCHCORE_SLOT_PENDING);
    }
    if (!state->staged_install && state->slot == LAUNCHCORE_SLOT_A) {
        md.slot_a_version = launchcore_image_packed_version(&header);
        memcpy(md.slot_a_hash, header.image_sha256, 32);
    } else if (!state->staged_install) {
        md.slot_b_version = launchcore_image_packed_version(&header);
        memcpy(md.slot_b_hash, header.image_sha256, 32);
    }
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    state->active = false;
    return LAUNCHCORE_OK;
}
