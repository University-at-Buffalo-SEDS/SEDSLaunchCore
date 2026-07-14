#include "update_state.h"
#include "launchcore/storage.h"

launchcore_status_t launchcore_update_write(const void *data, size_t len)
{
    launchcore_update_state_t *state = launchcore_update_state();
    if (!state->active || data == 0) return LAUNCHCORE_ERR_BAD_STATE;
    if (state->offset > state->info.image_size || len > (size_t)(state->info.image_size - state->offset)) {
        return LAUNCHCORE_ERR_NO_SPACE;
    }
    if (launchcore_storage_driver()->write(state->write_base + state->offset, data, (uint32_t)len) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_ERR_FLASH;
    }
    state->offset += (uint32_t)len;
    return LAUNCHCORE_OK;
}
