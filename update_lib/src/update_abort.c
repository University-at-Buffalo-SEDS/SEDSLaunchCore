#include "update_state.h"
#include "launchcore/metadata.h"

launchcore_status_t launchcore_update_abort(void)
{
    launchcore_update_state_t *state = launchcore_update_state();
    if (!state->active) return LAUNCHCORE_ERR_BAD_STATE;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    if (!state->staged_install) launchcore_metadata_set_slot_state(&md, state->slot, LAUNCHCORE_SLOT_BAD);
    md.update_slot = LAUNCHCORE_SLOT_NONE;
    (void)launchcore_metadata_commit(&md);
    state->active = false;
    return LAUNCHCORE_OK;
}
