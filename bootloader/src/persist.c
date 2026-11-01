#include "launchcore/image.h"
#include "launchcore/persist.h"
#include "launchcore/storage.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define PERSIST_BANK_MAGIC 0x4C435042u
#define PERSIST_RECORD_MAGIC 0x4C435052u
#define PERSIST_VERSION 1u
#define PERSIST_COMMITTED 0x434F4D4Du
#define PERSIST_FLAG_DELETED 1u
#define PERSIST_LEGACY_WRITE_SIZE 4u
#define PERSIST_MAX_WRITE_SIZE 32u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t generation;
    uint32_t header_crc32;
    uint32_t committed;
} persist_bank_header_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t key;
    uint32_t sequence;
    uint16_t value_size;
    uint16_t flags;
    uint32_t value_crc32;
    uint32_t header_crc32;
} persist_record_header_t;

typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t generation;
} persist_bank_t;

typedef enum {
    RECORD_READ_END,
    RECORD_READ_VALID,
    RECORD_READ_INVALID_VALUE,
    RECORD_READ_CORRUPT,
} record_read_status_t;

static uint32_t write_size(void)
{
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    return layout == NULL || layout->persistent_data_write_size == 0u ?
        PERSIST_LEGACY_WRITE_SIZE : layout->persistent_data_write_size;
}

static uint32_t align_write(uint32_t value)
{
    const uint32_t alignment = write_size();
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool bank_header_valid(const persist_bank_header_t *header)
{
    return header->magic == PERSIST_BANK_MAGIC && header->version == PERSIST_VERSION &&
        header->size == sizeof(*header) && header->committed == PERSIST_COMMITTED &&
        header->header_crc32 == launchcore_crc32(header, offsetof(persist_bank_header_t, header_crc32));
}

static bool record_header_valid(const persist_record_header_t *header, uint32_t available)
{
    return header->magic == PERSIST_RECORD_MAGIC &&
        header->value_size <= LAUNCHCORE_PERSIST_MAX_VALUE_SIZE &&
        header->value_size <= available - sizeof(*header) &&
        (header->flags & ~PERSIST_FLAG_DELETED) == 0u &&
        header->header_crc32 == launchcore_crc32(header, offsetof(persist_record_header_t, header_crc32));
}

static record_read_status_t read_record(uint32_t address, uint32_t available, persist_record_header_t *header,
    uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE])
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    if (available < sizeof(*header)) return RECORD_READ_END;
    uint32_t magic;
    if (storage->read(address, &magic, sizeof(magic)) != LAUNCHCORE_STORAGE_OK) return RECORD_READ_CORRUPT;
    if (magic == 0xFFFFFFFFu) return RECORD_READ_END;
    if (storage->read(address, header, sizeof(*header)) != LAUNCHCORE_STORAGE_OK ||
        !record_header_valid(header, available)) return RECORD_READ_CORRUPT;
    if (header->value_size > 0u &&
        storage->read(address + sizeof(*header), value, header->value_size) != LAUNCHCORE_STORAGE_OK) {
        return RECORD_READ_INVALID_VALUE;
    }
    return launchcore_crc32(value, header->value_size) == header->value_crc32 ?
        RECORD_READ_VALID : RECORD_READ_INVALID_VALUE;
}

static launchcore_persist_status_t layout_banks(persist_bank_t banks[2])
{
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    const uint32_t alignment = write_size();
    if (layout == NULL || layout->persistent_data_size < 2u * (sizeof(persist_bank_header_t) + sizeof(persist_record_header_t))) {
        return LAUNCHCORE_PERSIST_NO_SPACE;
    }
    if (alignment < PERSIST_LEGACY_WRITE_SIZE || alignment > PERSIST_MAX_WRITE_SIZE ||
        (alignment & (alignment - 1u)) != 0u ||
        layout->persistent_data_base % alignment != 0u) {
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    }
    uint32_t bank_size = layout->persistent_data_size / 2u;
    if (layout->persistent_data_erase_size != 0u &&
        (layout->persistent_data_base % layout->persistent_data_erase_size != 0u ||
         bank_size % layout->persistent_data_erase_size != 0u)) {
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    }
    banks[0] = (persist_bank_t){layout->persistent_data_base, bank_size, 0u};
    banks[1] = (persist_bank_t){layout->persistent_data_base + bank_size, bank_size, 0u};
    return LAUNCHCORE_PERSIST_OK;
}

static bool read_bank_header(persist_bank_t *bank)
{
    persist_bank_header_t header;
    if (launchcore_storage_driver()->read(bank->base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK ||
        !bank_header_valid(&header)) return false;
    bank->generation = header.generation;
    return true;
}

static launchcore_persist_status_t prepare_bank(persist_bank_t *bank, uint32_t generation)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    if (storage->erase(bank->base, bank->size) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_PERSIST_STORAGE_ERROR;
    persist_bank_header_t header = {
        .magic = PERSIST_BANK_MAGIC,
        .version = PERSIST_VERSION,
        .size = sizeof(header),
        .generation = generation,
        .committed = 0xFFFFFFFFu,
    };
    header.header_crc32 = launchcore_crc32(&header, offsetof(persist_bank_header_t, header_crc32));
    if (storage->write(bank->base, &header, offsetof(persist_bank_header_t, committed)) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_PERSIST_STORAGE_ERROR;
    }
    bank->generation = generation;
    return LAUNCHCORE_PERSIST_OK;
}

static launchcore_persist_status_t commit_bank(const persist_bank_t *bank)
{
    uint32_t committed = PERSIST_COMMITTED;
    return launchcore_storage_driver()->write(bank->base + offsetof(persist_bank_header_t, committed),
        &committed, sizeof(committed)) == LAUNCHCORE_STORAGE_OK ?
        LAUNCHCORE_PERSIST_OK : LAUNCHCORE_PERSIST_STORAGE_ERROR;
}

static launchcore_persist_status_t format_bank(persist_bank_t *bank, uint32_t generation)
{
    launchcore_persist_status_t status = prepare_bank(bank, generation);
    return status == LAUNCHCORE_PERSIST_OK ? commit_bank(bank) : status;
}

static launchcore_persist_status_t active_bank(persist_bank_t banks[2], unsigned int *active)
{
    launchcore_persist_status_t status = layout_banks(banks);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    bool valid0 = read_bank_header(&banks[0]);
    bool valid1 = read_bank_header(&banks[1]);
    if (!valid0 && !valid1) {
        status = format_bank(&banks[0], 1u);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
        *active = 0u;
    } else if (!valid1 || (valid0 && banks[0].generation >= banks[1].generation)) {
        *active = 0u;
    } else {
        *active = 1u;
    }
    return LAUNCHCORE_PERSIST_OK;
}

static uint32_t scan_end(const persist_bank_t *bank, uint32_t *max_sequence)
{
    uint32_t offset = align_write(sizeof(persist_bank_header_t));
    uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    *max_sequence = 0u;
    while (offset + sizeof(persist_record_header_t) <= bank->size) {
        persist_record_header_t header;
        record_read_status_t status = read_record(bank->base + offset, bank->size - offset, &header, value);
        if (status == RECORD_READ_END) break;
        if (status == RECORD_READ_CORRUPT) return bank->size;
        if (status == RECORD_READ_VALID && header.sequence > *max_sequence) *max_sequence = header.sequence;
        offset += align_write(sizeof(header) + header.value_size);
    }
    return offset;
}

static launchcore_persist_status_t append_record(const persist_bank_t *bank, uint32_t offset,
    uint32_t key, const void *value, uint16_t value_size, uint16_t flags, uint32_t sequence)
{
    uint32_t record_size = align_write(sizeof(persist_record_header_t) + value_size);
    if (record_size > bank->size - offset) return LAUNCHCORE_PERSIST_NO_SPACE;
    persist_record_header_t header = {
        .magic = PERSIST_RECORD_MAGIC,
        .key = key,
        .sequence = sequence,
        .value_size = value_size,
        .flags = flags,
        .value_crc32 = launchcore_crc32(value, value_size),
    };
    header.header_crc32 = launchcore_crc32(&header, offsetof(persist_record_header_t, header_crc32));
    uint8_t record[sizeof(persist_record_header_t) + LAUNCHCORE_PERSIST_MAX_VALUE_SIZE +
        PERSIST_MAX_WRITE_SIZE - 1u];
    memset(record, 0xFF, record_size);
    memcpy(record, &header, sizeof(header));
    if (value_size > 0u) memcpy(record + sizeof(header), value, value_size);
    if (launchcore_storage_driver()->write(bank->base + offset, record, record_size) !=
        LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_PERSIST_STORAGE_ERROR;
    }
    return LAUNCHCORE_PERSIST_OK;
}

static bool has_later_key(const persist_bank_t *bank, uint32_t offset, uint32_t key)
{
    uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    while (offset + sizeof(persist_record_header_t) <= bank->size) {
        persist_record_header_t header;
        record_read_status_t status = read_record(bank->base + offset, bank->size - offset, &header, value);
        if (status == RECORD_READ_END || status == RECORD_READ_CORRUPT) return false;
        if (status == RECORD_READ_VALID && header.key == key) return true;
        offset += align_write(sizeof(header) + header.value_size);
    }
    return false;
}

static launchcore_persist_status_t compact(persist_bank_t banks[2], unsigned int active)
{
    persist_bank_t *source = &banks[active];
    persist_bank_t *target = &banks[active ^ 1u];
    launchcore_persist_status_t status = prepare_bank(target, source->generation + 1u);
    if (status != LAUNCHCORE_PERSIST_OK) return status;

    uint32_t source_offset = align_write(sizeof(persist_bank_header_t));
    uint32_t target_offset = align_write(sizeof(persist_bank_header_t));
    uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    while (source_offset + sizeof(persist_record_header_t) <= source->size) {
        persist_record_header_t header;
        record_read_status_t read_status = read_record(source->base + source_offset,
            source->size - source_offset, &header, value);
        if (read_status == RECORD_READ_END) break;
        if (read_status == RECORD_READ_CORRUPT) return LAUNCHCORE_PERSIST_CORRUPT;
        uint32_t next = source_offset + align_write(sizeof(header) + header.value_size);
        if (read_status == RECORD_READ_VALID && !has_later_key(source, next, header.key) &&
            (header.flags & PERSIST_FLAG_DELETED) == 0u) {
            status = append_record(target, target_offset, header.key, value, header.value_size, 0u, header.sequence);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            target_offset += align_write(sizeof(header) + header.value_size);
        }
        source_offset = next;
    }
    return commit_bank(target);
}

launchcore_persist_status_t launchcore_persist_init(void)
{
    if (launchcore_storage_init() != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_PERSIST_STORAGE_ERROR;
    persist_bank_t banks[2];
    unsigned int active;
    return active_bank(banks, &active);
}

launchcore_persist_status_t launchcore_persist_get(uint32_t key, void *value, size_t *value_size)
{
    if (value_size == NULL || (value == NULL && *value_size != 0u)) return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    persist_bank_t banks[2];
    unsigned int active;
    launchcore_persist_status_t status = active_bank(banks, &active);
    if (status != LAUNCHCORE_PERSIST_OK) return status;

    persist_record_header_t latest;
    bool found = false;
    uint8_t latest_value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    uint8_t candidate[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    uint32_t offset = align_write(sizeof(persist_bank_header_t));
    while (offset + sizeof(persist_record_header_t) <= banks[active].size) {
        persist_record_header_t header;
        record_read_status_t read_status = read_record(banks[active].base + offset,
            banks[active].size - offset, &header, candidate);
        if (read_status == RECORD_READ_END) break;
        if (read_status == RECORD_READ_CORRUPT) return LAUNCHCORE_PERSIST_CORRUPT;
        if (read_status == RECORD_READ_VALID && header.key == key) {
            latest = header;
            memcpy(latest_value, candidate, header.value_size);
            found = true;
        }
        offset += align_write(sizeof(header) + header.value_size);
    }
    if (!found || (latest.flags & PERSIST_FLAG_DELETED) != 0u) return LAUNCHCORE_PERSIST_NOT_FOUND;
    if (*value_size < latest.value_size) {
        *value_size = latest.value_size;
        return LAUNCHCORE_PERSIST_NO_SPACE;
    }
    memcpy(value, latest_value, latest.value_size);
    *value_size = latest.value_size;
    return LAUNCHCORE_PERSIST_OK;
}

static launchcore_persist_status_t write_value(uint32_t key, const void *value, size_t value_size, uint16_t flags)
{
    if ((value == NULL && value_size != 0u) || value_size > LAUNCHCORE_PERSIST_MAX_VALUE_SIZE) {
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    }
    persist_bank_t banks[2];
    unsigned int active;
    launchcore_persist_status_t status = active_bank(banks, &active);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    uint32_t max_sequence;
    uint32_t offset = scan_end(&banks[active], &max_sequence);
    uint32_t required = align_write(sizeof(persist_record_header_t) + (uint32_t)value_size);
    if (required > banks[active].size - offset) {
        status = compact(banks, active);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
        active ^= 1u;
        offset = scan_end(&banks[active], &max_sequence);
    }
    return append_record(&banks[active], offset, key, value, (uint16_t)value_size, flags, max_sequence + 1u);
}

launchcore_persist_status_t launchcore_persist_set(uint32_t key, const void *value, size_t value_size)
{
    return write_value(key, value, value_size, 0u);
}

launchcore_persist_status_t launchcore_persist_delete(uint32_t key)
{
    return write_value(key, NULL, 0u, PERSIST_FLAG_DELETED);
}

/* Strict opt-in reader. Keep legacy recovery/format behavior above unchanged. */
static launchcore_persist_status_t strict_fault(launchcore_persist_strict_info_t *info,
    launchcore_persist_health_t health)
{
    info->health = health;
    return health == LAUNCHCORE_PERSIST_HEALTH_UNKNOWN ?
        LAUNCHCORE_PERSIST_STORAGE_ERROR : LAUNCHCORE_PERSIST_CORRUPT;
}

static launchcore_persist_status_t strict_erased(uint32_t address, uint32_t size, bool *erased)
{
    uint8_t bytes[PERSIST_MAX_WRITE_SIZE];
    *erased = true;
    while (size != 0u) {
        const uint32_t take = size < sizeof(bytes) ? size : sizeof(bytes);
        if (launchcore_storage_driver()->read(address, bytes, take) != LAUNCHCORE_STORAGE_OK)
            return LAUNCHCORE_PERSIST_STORAGE_ERROR;
        for (uint32_t i = 0u; i < take; ++i) {
            if (bytes[i] != 0xFFu) *erased = false;
        }
        address += take;
        size -= take;
    }
    return LAUNCHCORE_PERSIST_OK;
}

typedef struct { uint32_t offset, sequence; } strict_cursor_t;
/* One checked traversal primitive shared by strict reads and writes. A blank
 * tail reports end without consuming its append location. No legacy caller is
 * redirected through this stricter policy. */
static launchcore_persist_status_t strict_next(const persist_bank_t *bank,
    strict_cursor_t *cursor, persist_record_header_t *header, uint8_t *value,
    bool *end, launchcore_persist_strict_info_t *info)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const uint32_t available = bank->size - cursor->offset;
    *end = false;
    bool erased;
    if (available < sizeof(*header)) {
        if (strict_erased(bank->base + cursor->offset, available, &erased) != LAUNCHCORE_PERSIST_OK)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
        *end = true;
        return erased ? LAUNCHCORE_PERSIST_OK : strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    }
    uint32_t magic;
    if (storage->read(bank->base + cursor->offset, &magic, sizeof(magic)) != LAUNCHCORE_STORAGE_OK)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
    if (magic == UINT32_MAX) {
        if (strict_erased(bank->base + cursor->offset, available, &erased) != LAUNCHCORE_PERSIST_OK)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
        *end = true;
        return erased ? LAUNCHCORE_PERSIST_OK : strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    }
    if (storage->read(bank->base + cursor->offset, header, sizeof(*header)) != LAUNCHCORE_STORAGE_OK)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
    if (!record_header_valid(header, available)) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    const uint32_t span = align_write(sizeof(*header) + header->value_size);
    if (span > available) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    if (header->value_size != 0u &&
        storage->read(bank->base + cursor->offset + sizeof(*header), value, header->value_size) != LAUNCHCORE_STORAGE_OK)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
    if (launchcore_crc32(value, header->value_size) != header->value_crc32)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    if (header->sequence == 0u || header->sequence == UINT32_MAX || header->sequence <= cursor->sequence)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    if (strict_erased(bank->base + cursor->offset + sizeof(*header) + header->value_size,
            span - sizeof(*header) - header->value_size, &erased) != LAUNCHCORE_PERSIST_OK)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
    if (!erased) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    cursor->sequence = header->sequence;
    cursor->offset += span;
    return LAUNCHCORE_PERSIST_OK;
}

static launchcore_persist_status_t strict_scan(const persist_bank_t *bank,
    launchcore_persist_strict_info_t *info, const uint32_t *key,
    persist_record_header_t *latest, uint8_t *latest_value, uint32_t *max_sequence)
{
    strict_cursor_t cursor = {align_write(sizeof(persist_bank_header_t)), 0u};
    *max_sequence = 0u;
    for (;;) {
        persist_record_header_t header;
        uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
        bool end;
        launchcore_persist_status_t status = strict_next(bank, &cursor, &header, value, &end, info);
        if (status != LAUNCHCORE_PERSIST_OK || end) return status;
        *max_sequence = cursor.sequence;
        if (key != NULL && header.key == *key) {
            *latest = header;
            if (latest_value != NULL && header.value_size != 0u) memcpy(latest_value, value, header.value_size);
            info->record = (header.flags & PERSIST_FLAG_DELETED) != 0u ?
                LAUNCHCORE_PERSIST_RECORD_DELETED : LAUNCHCORE_PERSIST_RECORD_PRESENT;
        }
    }
}

static launchcore_persist_status_t strict_select(persist_bank_t banks[2], unsigned *active,
    launchcore_persist_strict_info_t *info)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    if (storage == NULL || storage->layout == NULL || storage->read == NULL)
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    launchcore_persist_status_t status = layout_banks(banks);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    const launchcore_storage_layout_t *layout = storage->layout();
    /* Legacy layout_banks intentionally retains its historical permissive
     * geometry. Strict inspection must cover every byte in two aligned banks. */
    if ((layout->persistent_data_size & 1u) != 0u ||
        (banks[0].size & (write_size() - 1u)) != 0u ||
        layout->persistent_data_base > UINT32_MAX - (layout->persistent_data_size - 1u))
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    bool blank[2];
    uint32_t sequences[2] = {0u, 0u};
    for (unsigned i = 0u; i < 2u; ++i) {
        if (strict_erased(banks[i].base, banks[i].size, &blank[i]) != LAUNCHCORE_PERSIST_OK)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
        if (blank[i]) continue;
        persist_bank_header_t header;
        if (storage->read(banks[i].base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
        if (!bank_header_valid(&header))
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
        if (header.generation == 0u || header.generation == UINT32_MAX)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
        banks[i].generation = header.generation;
        bool erased;
        if (strict_erased(banks[i].base + sizeof(header),
                align_write(sizeof(header)) - sizeof(header), &erased) != LAUNCHCORE_PERSIST_OK)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
        if (!erased) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
        status = strict_scan(&banks[i], info, NULL, NULL, NULL, &sequences[i]);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
    }
    if (blank[0] && blank[1]) {
        info->health = LAUNCHCORE_PERSIST_HEALTH_VIRGIN;
        return LAUNCHCORE_PERSIST_OK;
    }
    if (!blank[0] && !blank[1]) {
        const uint32_t a = banks[0].generation, b = banks[1].generation;
        if (a == b || (a > b ? a - b : b - a) != 1u)
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    }
    *active = blank[0] || (!blank[1] && banks[1].generation > banks[0].generation) ? 1u : 0u;
    info->health = LAUNCHCORE_PERSIST_HEALTH_HEALTHY;
    info->bank_generation = banks[*active].generation;
    info->max_sequence = sequences[*active];
    return LAUNCHCORE_PERSIST_OK;
}

launchcore_persist_status_t launchcore_persist_probe_strict(launchcore_persist_strict_info_t *info)
{
    if (info == NULL) return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    *info = (launchcore_persist_strict_info_t){0};
    persist_bank_t banks[2];
    unsigned active = 0u;
    return strict_select(banks, &active, info);
}

launchcore_persist_status_t launchcore_persist_get_strict(uint32_t key,
    void *value, size_t *value_size, launchcore_persist_strict_info_t *info)
{
    if (info == NULL) return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    *info = (launchcore_persist_strict_info_t){0};
    if (value_size == NULL || (value == NULL && *value_size != 0u))
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    persist_bank_t banks[2];
    unsigned active = 0u;
    launchcore_persist_status_t status = strict_select(banks, &active, info);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    info->record = LAUNCHCORE_PERSIST_RECORD_ABSENT;
    if (info->health == LAUNCHCORE_PERSIST_HEALTH_VIRGIN) return LAUNCHCORE_PERSIST_NOT_FOUND;
    persist_record_header_t latest = {0};
    uint8_t latest_value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    uint32_t sequence;
    status = strict_scan(&banks[active], info, &key, &latest, latest_value, &sequence);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    /* A committed successor must not silently erase a still-live predecessor
     * key or replace the same sequence with different data. This checks only
     * observable adjacent-bank history, not arbitrary whole-image rollback. */
    if (banks[active ^ 1u].generation != 0u) {
        launchcore_persist_strict_info_t older_info = *info;
        older_info.record = LAUNCHCORE_PERSIST_RECORD_ABSENT;
        persist_record_header_t older = {0};
        status = strict_scan(&banks[active ^ 1u], &older_info, &key, &older, NULL, &sequence);
        if (status != LAUNCHCORE_PERSIST_OK) {
            info->health = older_info.health;
            return status;
        }
        const bool absent = info->record == LAUNCHCORE_PERSIST_RECORD_ABSENT;
        if ((absent && older_info.record == LAUNCHCORE_PERSIST_RECORD_PRESENT) ||
            (!absent && older_info.record != LAUNCHCORE_PERSIST_RECORD_ABSENT &&
             (latest.sequence < older.sequence ||
              (latest.sequence == older.sequence &&
               (latest.flags != older.flags || latest.value_size != older.value_size ||
                latest.value_crc32 != older.value_crc32)))))
            return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    }
    if (info->record != LAUNCHCORE_PERSIST_RECORD_PRESENT) return LAUNCHCORE_PERSIST_NOT_FOUND;
    if (*value_size < latest.value_size) return LAUNCHCORE_PERSIST_NO_SPACE;
    if (latest.value_size != 0u) memcpy(value, latest_value, latest.value_size);
    *value_size = latest.value_size;
    return LAUNCHCORE_PERSIST_OK;
}

/* Strict writers are opt-in and require exclusive caller-owned serialization
 * from selection through verification. They never invoke active_bank/init. */
static launchcore_persist_status_t strict_append_checked(const persist_bank_t *bank,
    uint32_t offset, uint32_t key, const void *value, uint16_t size,
    uint16_t flags, uint32_t sequence)
{
    launchcore_persist_status_t status = append_record(bank, offset, key, value, size, flags, sequence);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    persist_record_header_t header;
    uint8_t observed[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    if (storage->read(bank->base + offset, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK ||
        (size != 0u && storage->read(bank->base + offset + sizeof(header), observed, size) != LAUNCHCORE_STORAGE_OK))
        return LAUNCHCORE_PERSIST_STORAGE_ERROR;
    if (!record_header_valid(&header, bank->size - offset) || header.key != key ||
        header.sequence != sequence || header.flags != flags || header.value_size != size ||
        header.value_crc32 != launchcore_crc32(value, size) ||
        (size != 0u && memcmp(observed, value, size) != 0))
        return LAUNCHCORE_PERSIST_CORRUPT;
    bool erased;
    status = strict_erased(bank->base + offset + sizeof(header) + size,
        align_write(sizeof(header) + size) - sizeof(header) - size, &erased);
    return status != LAUNCHCORE_PERSIST_OK ? status :
        erased ? LAUNCHCORE_PERSIST_OK : LAUNCHCORE_PERSIST_CORRUPT;
}

/* Reuse the strict parser for every candidate, including overwritten/deleted
 * records. Unlike legacy has_later_key, IO/CRC failures never mean "not later".
 * This intentionally trades bounded O(records^2) reads for no key-index arena. */
static launchcore_persist_status_t strict_copy_latest(const persist_bank_t *source,
    const persist_bank_t *target, uint32_t *end, launchcore_persist_strict_info_t *info)
{
    strict_cursor_t cursor = {align_write(sizeof(persist_bank_header_t)), 0u};
    *end = cursor.offset;
    for (;;) {
        persist_record_header_t header;
        uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
        bool finished;
        launchcore_persist_status_t status = strict_next(source, &cursor, &header, value, &finished, info);
        if (status != LAUNCHCORE_PERSIST_OK || finished) return status;
        persist_record_header_t latest = {0};
        uint32_t sequence;
        const uint32_t key = header.key;
        info->record = LAUNCHCORE_PERSIST_RECORD_ABSENT;
        status = strict_scan(source, info, &key, &latest, value, &sequence);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
        if (latest.sequence == header.sequence) {
            const uint32_t span = align_write(sizeof(latest) + latest.value_size);
            if (span > source->size - *end) return LAUNCHCORE_PERSIST_NO_SPACE;
            if (target != NULL) {
                status = strict_append_checked(target, *end, latest.key, value,
                    latest.value_size, latest.flags, latest.sequence);
                if (status != LAUNCHCORE_PERSIST_OK) return status;
            }
            *end += span;
        }
    }
}

static launchcore_persist_status_t strict_commit_checked(const persist_bank_t *bank,
    uint32_t sequence, launchcore_persist_strict_info_t *info)
{
    persist_bank_header_t header;
    if (launchcore_storage_driver()->read(bank->base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
    if (header.magic != PERSIST_BANK_MAGIC || header.version != PERSIST_VERSION ||
        header.size != sizeof(header) || header.generation != bank->generation ||
        header.committed != UINT32_MAX ||
        header.header_crc32 != launchcore_crc32(&header, offsetof(persist_bank_header_t, header_crc32)))
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    bool erased;
    launchcore_persist_status_t status = strict_erased(bank->base + sizeof(header),
        align_write(sizeof(header)) - sizeof(header), &erased);
    if (status != LAUNCHCORE_PERSIST_OK) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
    if (!erased) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    uint32_t observed;
    status = strict_scan(bank, info, NULL, NULL, NULL, &observed);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    if (observed != sequence) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    return commit_bank(bank);
}

static launchcore_persist_status_t strict_verify_write(uint32_t key, const void *value,
    size_t size, uint16_t flags, launchcore_persist_strict_info_t *info)
{
    uint8_t observed[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
    size_t observed_size = sizeof(observed);
    launchcore_persist_status_t status = launchcore_persist_get_strict(key, observed, &observed_size, info);
    if (flags == PERSIST_FLAG_DELETED) {
        return status == LAUNCHCORE_PERSIST_NOT_FOUND &&
            info->health == LAUNCHCORE_PERSIST_HEALTH_HEALTHY &&
            info->record == LAUNCHCORE_PERSIST_RECORD_DELETED ? LAUNCHCORE_PERSIST_OK :
            status == LAUNCHCORE_PERSIST_OK || status == LAUNCHCORE_PERSIST_NOT_FOUND ?
                LAUNCHCORE_PERSIST_CORRUPT : status;
    }
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    return observed_size == size && (size == 0u || memcmp(observed, value, size) == 0) ?
        LAUNCHCORE_PERSIST_OK : LAUNCHCORE_PERSIST_CORRUPT;
}

/* Check observable adjacent history for every predecessor key, not only the
 * requested key, before an erase could destroy that evidence. */
static launchcore_persist_status_t strict_history(const persist_bank_t *older,
    launchcore_persist_strict_info_t *info)
{
    if (older->generation == 0u) return LAUNCHCORE_PERSIST_OK;
    strict_cursor_t cursor = {align_write(sizeof(persist_bank_header_t)), 0u};
    for (;;) {
        persist_record_header_t header;
        uint8_t value[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
        bool end;
        launchcore_persist_status_t status = strict_next(older, &cursor, &header, value, &end, info);
        if (status != LAUNCHCORE_PERSIST_OK || end) return status;
        size_t size = sizeof(value);
        status = launchcore_persist_get_strict(header.key, value, &size, info);
        if (status != LAUNCHCORE_PERSIST_OK && status != LAUNCHCORE_PERSIST_NOT_FOUND) return status;
    }
}

static launchcore_persist_status_t strict_write_value(uint32_t key, const void *value,
    size_t size, uint16_t flags, bool provision, launchcore_persist_strict_info_t *info)
{
    if (info == NULL) return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    *info = (launchcore_persist_strict_info_t){0};
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    if ((value == NULL && size != 0u) || size > LAUNCHCORE_PERSIST_MAX_VALUE_SIZE ||
        storage == NULL || storage->write == NULL || storage->erase == NULL)
        return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
    persist_bank_t banks[2];
    unsigned active = 0u;
    launchcore_persist_status_t status = strict_select(banks, &active, info);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    if (align_write(sizeof(persist_bank_header_t)) +
        align_write(sizeof(persist_record_header_t) + (uint32_t)size) > banks[0].size)
        return LAUNCHCORE_PERSIST_NO_SPACE;
    if (provision) {
        /* An explicit application/operator authorization is required. No
         * missing-key or unrecognized storage path calls this implicitly. */
        if (info->health != LAUNCHCORE_PERSIST_HEALTH_VIRGIN)
            return LAUNCHCORE_PERSIST_INVALID_ARGUMENT;
        status = prepare_bank(&banks[0], 1u);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
    } else if (info->health != LAUNCHCORE_PERSIST_HEALTH_HEALTHY) {
        return LAUNCHCORE_PERSIST_NOT_FOUND;
    }
    const uint32_t source_sequence = info->max_sequence;
    const uint32_t sequence = provision ? 1u : source_sequence + 1u;
    if (sequence == UINT32_MAX || sequence == 0u)
        return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    uint32_t offset = align_write(sizeof(persist_bank_header_t));
    if (!provision) {
        status = strict_history(&banks[active ^ 1u], info);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
        uint32_t last_sequence;
        /* Strict selection already scanned all bytes. scan_end is NOT used:
         * its error-as-full behavior belongs only to the legacy writer. */
        strict_cursor_t cursor = {offset, 0u};
        for (;;) {
            persist_record_header_t header;
            uint8_t observed[LAUNCHCORE_PERSIST_MAX_VALUE_SIZE];
            bool end;
            status = strict_next(&banks[active], &cursor, &header, observed, &end, info);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            if (end) break;
        }
        offset = cursor.offset;
        const uint32_t span = align_write(sizeof(persist_record_header_t) + size);
        if (span > banks[active].size - offset) {
            if (banks[active].generation >= UINT32_MAX - 1u)
                return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
            uint32_t compacted;
            status = strict_copy_latest(&banks[active], NULL, &compacted, info);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            if (span > banks[active].size - compacted) return LAUNCHCORE_PERSIST_NO_SPACE;
            status = prepare_bank(&banks[active ^ 1u], banks[active].generation + 1u);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            status = strict_copy_latest(&banks[active], &banks[active ^ 1u], &offset, info);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            status = strict_append_checked(&banks[active ^ 1u], offset, key, value, (uint16_t)size, flags, sequence);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            status = strict_scan(&banks[active], info, NULL, NULL, NULL, &last_sequence);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            if (last_sequence != source_sequence) return strict_fault(info, LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
            status = strict_commit_checked(&banks[active ^ 1u], sequence, info);
            if (status != LAUNCHCORE_PERSIST_OK) return status;
            return strict_verify_write(key, value, size, flags, info);
        }
    }
    status = strict_append_checked(&banks[active], offset, key, value, (uint16_t)size, flags, sequence);
    if (status != LAUNCHCORE_PERSIST_OK) return status;
    if (provision) {
        status = strict_commit_checked(&banks[active], sequence, info);
        if (status != LAUNCHCORE_PERSIST_OK) return status;
    }
    return strict_verify_write(key, value, size, flags, info);
}

static launchcore_persist_status_t strict_write_result(launchcore_persist_status_t status,
    launchcore_persist_strict_info_t *info)
{
    if (info != NULL) {
        if (status == LAUNCHCORE_PERSIST_STORAGE_ERROR) info->health = LAUNCHCORE_PERSIST_HEALTH_UNKNOWN;
        else if (status == LAUNCHCORE_PERSIST_CORRUPT && info->health != LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS)
            info->health = LAUNCHCORE_PERSIST_HEALTH_CORRUPT;
    }
    return status;
}

launchcore_persist_status_t launchcore_persist_set_strict(uint32_t key, const void *value,
    size_t size, launchcore_persist_strict_info_t *info)
{
    return strict_write_result(strict_write_value(key, value, size, 0u, false, info), info);
}
launchcore_persist_status_t launchcore_persist_delete_strict(uint32_t key,
    launchcore_persist_strict_info_t *info)
{
    return strict_write_result(strict_write_value(key, NULL, 0u, PERSIST_FLAG_DELETED, false, info), info);
}
launchcore_persist_status_t launchcore_persist_provision_virgin_strict(uint32_t key,
    const void *value, size_t size, launchcore_persist_strict_info_t *info)
{
    return strict_write_result(strict_write_value(key, value, size, 0u, true, info), info);
}
