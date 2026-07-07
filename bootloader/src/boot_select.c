#include "launchcore/boot.h"
#include "launchcore/metadata.h"
#include "launchcore/platform.h"
#include <stddef.h>

static bool slot_bootable(const launchcore_metadata_t *md, launchcore_slot_id_t slot)
{
    launchcore_slot_state_t state = launchcore_metadata_slot_state(md, slot);
    return state == LAUNCHCORE_SLOT_PENDING ||
           state == LAUNCHCORE_SLOT_CONFIRMED ||
           state == LAUNCHCORE_SLOT_VALID_ROLLBACK;
}

__attribute__((weak)) bool platform_recovery_requested(void)
{
    return false;
}

launchcore_slot_id_t launchcore_select_slot(const launchcore_metadata_t *md)
{
    if (md == NULL || platform_recovery_requested() ||
        (md->flags & LAUNCHCORE_METADATA_FLAG_RECOVERY_REQUESTED) != 0u) {
        return LAUNCHCORE_SLOT_NONE;
    }

    if (launchcore_slot_id_is_valid((launchcore_slot_id_t)md->pending_slot)) {
        launchcore_slot_id_t pending = (launchcore_slot_id_t)md->pending_slot;
        if (slot_bootable(md, pending) && md->boot_attempts < md->max_boot_attempts) {
            return pending;
        }
        if (slot_bootable(md, (launchcore_slot_id_t)md->confirmed_slot)) {
            return (launchcore_slot_id_t)md->confirmed_slot;
        }
        return LAUNCHCORE_SLOT_NONE;
    }

    if (slot_bootable(md, (launchcore_slot_id_t)md->confirmed_slot)) {
        return (launchcore_slot_id_t)md->confirmed_slot;
    }

    bool a = slot_bootable(md, LAUNCHCORE_SLOT_A);
    bool b = slot_bootable(md, LAUNCHCORE_SLOT_B);
    if (a && b) return md->slot_b_version > md->slot_a_version ? LAUNCHCORE_SLOT_B : LAUNCHCORE_SLOT_A;
    if (a) return LAUNCHCORE_SLOT_A;
    if (b) return LAUNCHCORE_SLOT_B;
    return LAUNCHCORE_SLOT_NONE;
}
