#include "launchcore/metadata.h"
#include "launchcore/update.h"

launchcore_status_t launchcore_confirm_boot(void)
{
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    launchcore_slot_id_t active = launchcore_slot_id_is_valid((launchcore_slot_id_t)md.active_slot) ?
        (launchcore_slot_id_t)md.active_slot : (launchcore_slot_id_t)md.pending_slot;
    if (!launchcore_slot_id_is_valid(active)) return LAUNCHCORE_ERR_BAD_STATE;
    launchcore_slot_id_t old = (launchcore_slot_id_t)md.confirmed_slot;
    if (launchcore_slot_id_is_valid(old) && old != active) {
        launchcore_metadata_set_slot_state(&md, old, LAUNCHCORE_SLOT_VALID_ROLLBACK);
    }
    launchcore_metadata_set_slot_state(&md, active, LAUNCHCORE_SLOT_CONFIRMED);
    md.active_slot = (uint8_t)active;
    md.confirmed_slot = (uint8_t)active;
    md.pending_slot = LAUNCHCORE_SLOT_NONE;
    md.boot_attempts = 0u;
    if (md.update_kind == LAUNCHCORE_UPDATE_KIND_APPLICATION &&
        md.update_result == LAUNCHCORE_UPDATE_RESULT_VALIDATED) {
        md.update_result = LAUNCHCORE_UPDATE_RESULT_SUCCESS;
        md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    }
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_request_recovery(void)
{
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.flags |= LAUNCHCORE_METADATA_FLAG_RECOVERY_REQUESTED;
    return launchcore_metadata_commit(&md) == LAUNCHCORE_METADATA_OK ? LAUNCHCORE_OK : LAUNCHCORE_ERR_METADATA;
}
