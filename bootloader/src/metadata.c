#include "launchcore/config.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/storage.h"
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
} launchcore_metadata_v1_t;

static bool metadata_upgrade_v1(const launchcore_metadata_v1_t *old, launchcore_metadata_t *out)
{
    if (old->magic != LAUNCHCORE_METADATA_MAGIC || old->version != 1u ||
        old->size != sizeof(*old) ||
        old->metadata_crc32 != launchcore_crc32(old, offsetof(launchcore_metadata_v1_t, metadata_crc32))) {
        return false;
    }
    launchcore_metadata_init_default(out);
    out->sequence = old->sequence;
    out->active_slot = old->active_slot;
    out->confirmed_slot = old->confirmed_slot;
    out->pending_slot = old->pending_slot;
    out->update_slot = old->update_slot;
    out->slot_a_state = old->slot_a_state;
    out->slot_b_state = old->slot_b_state;
    out->boot_attempts = old->boot_attempts;
    out->max_boot_attempts = old->max_boot_attempts;
    out->slot_a_version = old->slot_a_version;
    out->slot_b_version = old->slot_b_version;
    memcpy(out->slot_a_hash, old->slot_a_hash, sizeof(out->slot_a_hash));
    memcpy(out->slot_b_hash, old->slot_b_hash, sizeof(out->slot_b_hash));
    out->flags = old->flags;
    out->metadata_crc32 = launchcore_metadata_crc(out);
    return true;
}

static launchcore_metadata_status_t metadata_read_record(uint32_t address, launchcore_metadata_t *out)
{
    launchcore_metadata_t raw;
    if (launchcore_storage_driver()->read(address, &raw, sizeof(raw)) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_METADATA_ERR_STORAGE;
    }
    if (launchcore_metadata_is_valid(&raw)) {
        *out = raw;
        return LAUNCHCORE_METADATA_OK;
    }
    return metadata_upgrade_v1((const launchcore_metadata_v1_t *)&raw, out) ?
        LAUNCHCORE_METADATA_OK : LAUNCHCORE_METADATA_ERR_INVALID;
}

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
    launchcore_metadata_status_t a_status = metadata_read_record(layout->metadata0_base, &a);
    launchcore_metadata_status_t b_status = metadata_read_record(layout->metadata1_base, &b);
    if (a_status == LAUNCHCORE_METADATA_ERR_STORAGE || b_status == LAUNCHCORE_METADATA_ERR_STORAGE) {
        return LAUNCHCORE_METADATA_ERR_STORAGE;
    }
    if (a_status != LAUNCHCORE_METADATA_OK) memset(&a, 0, sizeof(a));
    if (b_status != LAUNCHCORE_METADATA_OK) memset(&b, 0, sizeof(b));
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
    if (metadata_read_record(layout->metadata0_base, &a) != LAUNCHCORE_METADATA_OK) memset(&a, 0, sizeof(a));
    if (metadata_read_record(layout->metadata1_base, &b) != LAUNCHCORE_METADATA_OK) memset(&b, 0, sizeof(b));
    const launchcore_metadata_t *newest = launchcore_metadata_select_newest(&a, &b);
    uint32_t dst = layout->metadata0_base;
    if (newest == &a) dst = layout->metadata1_base;
    if (!launchcore_storage_range_preserves_persistent(dst, layout->metadata_size)) {
        return LAUNCHCORE_METADATA_ERR_INVALID;
    }
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
