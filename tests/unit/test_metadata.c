#include "launchcore/metadata.h"
#include "launchcore/image.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint8_t active_slot;
    uint8_t confirmed_slot;
    uint8_t pending_slot;
    uint8_t update_slot;
    uint8_t slot_a_state;
    uint8_t slot_b_state;
    uint8_t boot_attempts;
    uint8_t max_boot_attempts;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint8_t slot_a_hash[32];
    uint8_t slot_b_hash[32];
    uint32_t flags;
    uint32_t metadata_crc32;
} test_metadata_v1_t;

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

    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    assert(launchcore_storage_driver()->erase(layout->metadata0_base, layout->metadata_size) == LAUNCHCORE_STORAGE_OK);
    assert(launchcore_storage_driver()->erase(layout->metadata1_base, layout->metadata_size) == LAUNCHCORE_STORAGE_OK);
    test_metadata_v1_t old;
    memset(&old, 0, sizeof(old));
    old.magic = LAUNCHCORE_METADATA_MAGIC;
    old.version = 1u;
    old.size = sizeof(old);
    old.sequence = 9u;
    old.confirmed_slot = LAUNCHCORE_SLOT_A;
    old.pending_slot = LAUNCHCORE_SLOT_NONE;
    old.update_slot = LAUNCHCORE_SLOT_NONE;
    old.slot_a_state = LAUNCHCORE_SLOT_CONFIRMED;
    old.max_boot_attempts = 3u;
    old.metadata_crc32 = launchcore_crc32(&old, offsetof(test_metadata_v1_t, metadata_crc32));
    assert(launchcore_storage_driver()->write(layout->metadata0_base, &old, sizeof(old)) == LAUNCHCORE_STORAGE_OK);
    assert(launchcore_metadata_read_latest(&latest) == LAUNCHCORE_METADATA_OK);
    assert(latest.version == LAUNCHCORE_METADATA_VERSION);
    assert(latest.sequence == 9u);
    assert(latest.confirmed_slot == LAUNCHCORE_SLOT_A);
    assert(latest.update_result == LAUNCHCORE_UPDATE_RESULT_NONE);
    return 0;
}
