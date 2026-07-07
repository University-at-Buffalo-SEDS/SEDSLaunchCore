#include "launchcore/boot.h"
#include "launchcore/metadata.h"
#include <assert.h>

int main(void)
{
    launchcore_metadata_t md;
    launchcore_metadata_init_default(&md);
    assert(launchcore_select_slot(&md) == LAUNCHCORE_SLOT_NONE);
    md.confirmed_slot = LAUNCHCORE_SLOT_A;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_CONFIRMED);
    assert(launchcore_select_slot(&md) == LAUNCHCORE_SLOT_A);
    md.pending_slot = LAUNCHCORE_SLOT_B;
    md.max_boot_attempts = 3u;
    md.boot_attempts = 0u;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_PENDING);
    assert(launchcore_select_slot(&md) == LAUNCHCORE_SLOT_B);
    md.boot_attempts = 3u;
    assert(launchcore_select_slot(&md) == LAUNCHCORE_SLOT_A);
    md.pending_slot = LAUNCHCORE_SLOT_NONE;
    md.confirmed_slot = LAUNCHCORE_SLOT_NONE;
    md.slot_a_version = 1u;
    md.slot_b_version = 2u;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_VALID_ROLLBACK);
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_VALID_ROLLBACK);
    assert(launchcore_select_slot(&md) == LAUNCHCORE_SLOT_B);
    return 0;
}
