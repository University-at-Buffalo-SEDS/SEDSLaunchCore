#include "launchcore/config.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include <stddef.h>
#include <string.h>

void launchcore_metadata_init_default(launchcore_metadata_t *md)
{
    memset(md, 0, sizeof(*md));
    md->magic = LAUNCHCORE_METADATA_MAGIC;
    md->version = LAUNCHCORE_METADATA_VERSION;
    md->size = sizeof(*md);
    md->active_slot = LAUNCHCORE_SLOT_NONE;
    md->confirmed_slot = LAUNCHCORE_SLOT_NONE;
    md->pending_slot = LAUNCHCORE_SLOT_NONE;
    md->update_slot = LAUNCHCORE_SLOT_NONE;
    md->max_boot_attempts = LAUNCHCORE_DEFAULT_MAX_BOOT_ATTEMPTS;
    md->metadata_crc32 = launchcore_metadata_crc(md);
}

uint32_t launchcore_metadata_crc(const launchcore_metadata_t *md)
{
    return launchcore_crc32(md, offsetof(launchcore_metadata_t, metadata_crc32));
}

bool launchcore_metadata_is_valid(const launchcore_metadata_t *md)
{
    return md != NULL &&
           md->magic == LAUNCHCORE_METADATA_MAGIC &&
           md->version == LAUNCHCORE_METADATA_VERSION &&
           md->size == sizeof(*md) &&
           md->metadata_crc32 == launchcore_metadata_crc(md);
}

const launchcore_metadata_t *launchcore_metadata_select_newest(const launchcore_metadata_t *a, const launchcore_metadata_t *b)
{
    bool av = launchcore_metadata_is_valid(a);
    bool bv = launchcore_metadata_is_valid(b);
    if (av && bv) return a->sequence >= b->sequence ? a : b;
    if (av) return a;
    if (bv) return b;
    return NULL;
}

launchcore_metadata_status_t launchcore_metadata_read_latest(launchcore_metadata_t *out)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    launchcore_metadata_t a;
    launchcore_metadata_t b;
    if (storage->read(layout->metadata0_base, &a, sizeof(a)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_METADATA_ERR_STORAGE;
    if (storage->read(layout->metadata1_base, &b, sizeof(b)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_METADATA_ERR_STORAGE;
    const launchcore_metadata_t *newest = launchcore_metadata_select_newest(&a, &b);
    if (newest == NULL) {
        launchcore_metadata_init_default(out);
        return LAUNCHCORE_METADATA_ERR_INVALID;
    }
    *out = *newest;
    return LAUNCHCORE_METADATA_OK;
}

launchcore_metadata_status_t launchcore_metadata_commit(const launchcore_metadata_t *next)
{
    if (next == NULL) return LAUNCHCORE_METADATA_ERR_INVALID;
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    launchcore_metadata_t a;
    launchcore_metadata_t b;
    (void)storage->read(layout->metadata0_base, &a, sizeof(a));
    (void)storage->read(layout->metadata1_base, &b, sizeof(b));
    const launchcore_metadata_t *newest = launchcore_metadata_select_newest(&a, &b);
    uint32_t dst = layout->metadata0_base;
    if (newest == &a) dst = layout->metadata1_base;
    launchcore_metadata_t record = *next;
    record.magic = LAUNCHCORE_METADATA_MAGIC;
    record.version = LAUNCHCORE_METADATA_VERSION;
    record.size = sizeof(record);
    record.sequence = newest == NULL ? 1u : newest->sequence + 1u;
    record.metadata_crc32 = launchcore_metadata_crc(&record);
    if (storage->erase(dst, layout->metadata_size) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_METADATA_ERR_STORAGE;
    if (storage->write(dst, &record, sizeof(record)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_METADATA_ERR_STORAGE;
    launchcore_metadata_t verify;
    if (storage->read(dst, &verify, sizeof(verify)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_METADATA_ERR_STORAGE;
    if (!launchcore_metadata_is_valid(&verify) || verify.sequence != record.sequence) return LAUNCHCORE_METADATA_ERR_VERIFY;
    return LAUNCHCORE_METADATA_OK;
}

launchcore_slot_state_t launchcore_metadata_slot_state(const launchcore_metadata_t *md, launchcore_slot_id_t slot)
{
    if (slot == LAUNCHCORE_SLOT_A) return (launchcore_slot_state_t)md->slot_a_state;
    if (slot == LAUNCHCORE_SLOT_B) return (launchcore_slot_state_t)md->slot_b_state;
    return LAUNCHCORE_SLOT_EMPTY;
}

void launchcore_metadata_set_slot_state(launchcore_metadata_t *md, launchcore_slot_id_t slot, launchcore_slot_state_t state)
{
    if (slot == LAUNCHCORE_SLOT_A) md->slot_a_state = (uint8_t)state;
    if (slot == LAUNCHCORE_SLOT_B) md->slot_b_state = (uint8_t)state;
}

bool launchcore_slot_id_is_valid(launchcore_slot_id_t slot)
{
    return slot == LAUNCHCORE_SLOT_A || slot == LAUNCHCORE_SLOT_B;
}
