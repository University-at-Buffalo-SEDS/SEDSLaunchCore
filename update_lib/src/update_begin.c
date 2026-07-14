#include "update_state.h"
#include "launchcore/storage.h"
#include <string.h>

static launchcore_update_state_t state;

launchcore_update_state_t *launchcore_update_state(void)
{
    return &state;
}

bool launchcore_expected_hash_is_set(const uint8_t hash[32])
{
    uint8_t zero[32] = {0};
    return memcmp(hash, zero, 32) != 0;
}

launchcore_status_t launchcore_update_begin(const launchcore_update_info_t *info)
{
    if (info == NULL || state.active) return LAUNCHCORE_ERR_BAD_STATE;
    if (launchcore_storage_init() != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_ERR_FLASH;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    if (layout->slot_b_is_delta) return LAUNCHCORE_ERR_BAD_STATE;
    bool staged_install = layout->slot_b_size == 0u;
    launchcore_slot_id_t active = launchcore_slot_id_is_valid((launchcore_slot_id_t)md.active_slot) ?
        (launchcore_slot_id_t)md.active_slot : (launchcore_slot_id_t)md.confirmed_slot;
    launchcore_slot_id_t update = staged_install ? LAUNCHCORE_SLOT_A :
        (active == LAUNCHCORE_SLOT_A ? LAUNCHCORE_SLOT_B : LAUNCHCORE_SLOT_A);
    uint32_t write_base = staged_install ? layout->app_staging_base : launchcore_slot_base(update);
    uint32_t write_size = staged_install ? layout->app_staging_size : launchcore_slot_size(update);
    if (info->image_size > write_size || info->image_size > launchcore_slot_size(update)) return LAUNCHCORE_ERR_NO_SPACE;
    if (!launchcore_storage_range_preserves_persistent(write_base, write_size)) return LAUNCHCORE_ERR_BAD_STATE;
    if (storage->erase(write_base, write_size) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_ERR_FLASH;
    md.update_slot = (uint8_t)update;
    if (!staged_install) launchcore_metadata_set_slot_state(&md, update, LAUNCHCORE_SLOT_WRITING);
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    state.active = true;
    state.slot = update;
    state.write_base = write_base;
    state.offset = 0u;
    state.staged_install = staged_install;
    state.info = *info;
    return LAUNCHCORE_OK;
}
