#include "launchcore/delta.h"
#include "launchcore/storage.h"
#include "launchcore/update.h"

typedef struct {
    bool active;
    uint32_t size;
    uint32_t offset;
} delta_update_state_t;

static delta_update_state_t state;

launchcore_status_t launchcore_delta_update_begin(uint32_t patch_size)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    if (state.active || !layout->slot_b_is_delta || patch_size < sizeof(launchcore_delta_header_t) ||
        patch_size > layout->slot_b_size || layout->slot_erase_size == 0u) return LAUNCHCORE_ERR_BAD_STATE;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    if (md.pending_slot != LAUNCHCORE_SLOT_NONE ||
        (md.flags & (LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING |
                     LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING)) != 0u ||
        md.confirmed_slot != LAUNCHCORE_SLOT_A) return LAUNCHCORE_ERR_BAD_STATE;
    if (!launchcore_storage_range_preserves_persistent(layout->slot_b_base, layout->slot_b_size)) {
        return LAUNCHCORE_ERR_NO_SPACE;
    }
    if (storage->erase(layout->slot_b_base, layout->slot_b_size) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_ERR_FLASH;
    }
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_WRITING);
    md.flags &= ~LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_AVAILABLE;
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    state = (delta_update_state_t){true, patch_size, 0u};
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_delta_update_write(const void *data, size_t len)
{
    if (!state.active || data == NULL) return LAUNCHCORE_ERR_BAD_STATE;
    if (state.offset > state.size || len > state.size - state.offset) return LAUNCHCORE_ERR_NO_SPACE;
    uint32_t base = launchcore_storage_driver()->layout()->slot_b_base;
    if (launchcore_storage_driver()->write(base + state.offset, data, (uint32_t)len) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_ERR_FLASH;
    }
    state.offset += (uint32_t)len;
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_delta_update_finish(void)
{
    if (!state.active || state.offset != state.size) return LAUNCHCORE_ERR_BAD_STATE;
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    launchcore_delta_header_t header;
    if (launchcore_storage_driver()->read(layout->slot_b_base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_ERR_FLASH;
    }
    if (!launchcore_delta_header_is_valid(&header, state.size) || header.total_size != state.size ||
        header.erase_size != layout->slot_erase_size || header.base_image_size > layout->slot_a_size ||
        header.target_image_size > layout->slot_a_size) return LAUNCHCORE_ERR_BAD_IMAGE;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.flags &= ~(LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED |
        LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING |
        LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED);
    md.flags |= LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING;
    md.staged_app_addr = layout->slot_b_base;
    md.staged_app_size = state.size;
    md.update_slot = LAUNCHCORE_SLOT_A;
    md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md.update_attempted = 0u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_PENDING;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_PENDING);
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    state.active = false;
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_delta_update_abort(void)
{
    if (!state.active) return LAUNCHCORE_ERR_BAD_STATE;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_BAD);
    md.update_slot = LAUNCHCORE_SLOT_NONE;
    (void)launchcore_metadata_commit(&md);
    state.active = false;
    return LAUNCHCORE_OK;
}
