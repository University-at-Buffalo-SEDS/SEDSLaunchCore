#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include <assert.h>

int main(void)
{
    assert(launchcore_storage_init() == LAUNCHCORE_STORAGE_OK);
    launchcore_metadata_t md;
    launchcore_metadata_init_default(&md);
    md.confirmed_slot = LAUNCHCORE_SLOT_A;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_CONFIRMED);
    assert(launchcore_metadata_commit(&md) == LAUNCHCORE_METADATA_OK);
    launchcore_metadata_t latest;
    assert(launchcore_metadata_read_latest(&latest) == LAUNCHCORE_METADATA_OK);
    assert(latest.sequence == 1u);
    assert(latest.confirmed_slot == LAUNCHCORE_SLOT_A);
    md.confirmed_slot = LAUNCHCORE_SLOT_B;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_CONFIRMED);
    assert(launchcore_metadata_commit(&md) == LAUNCHCORE_METADATA_OK);
    assert(launchcore_metadata_read_latest(&latest) == LAUNCHCORE_METADATA_OK);
    assert(latest.sequence == 2u);
    assert(latest.confirmed_slot == LAUNCHCORE_SLOT_B);
    return 0;
}
