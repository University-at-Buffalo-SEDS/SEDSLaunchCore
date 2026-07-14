#include "launchcore/delta.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include "launchcore/update.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define STORAGE_SIZE 0x00010000u
#define SLOT_A_BASE 0x00002000u
#define SLOT_A_SIZE 0x00004000u
#define SLOT_B_BASE 0x00006000u
#define SLOT_B_SIZE 0x00004000u
#define ERASE_SIZE 256u
#define PAYLOAD_SIZE 4096u

static uint8_t flash[STORAGE_SIZE];
static bool initialized;
static bool fail_next_app_write;

static const launchcore_storage_layout_t layout = {
    .slot_a_base = SLOT_A_BASE,
    .slot_a_size = SLOT_A_SIZE,
    .slot_b_base = SLOT_B_BASE,
    .slot_b_size = SLOT_B_SIZE,
    .metadata0_base = 0x00000000u,
    .metadata1_base = 0x00001000u,
    .metadata_size = 0x00001000u,
    .slot_erase_size = ERASE_SIZE,
    .slot_b_is_delta = true,
    .supports_xip = true,
};

static launchcore_storage_status_t init(void)
{
    if (!initialized) {
        memset(flash, 0xFF, sizeof(flash));
        initialized = true;
    }
    return LAUNCHCORE_STORAGE_OK;
}

static bool range_ok(uint32_t address, uint32_t size)
{
    return address <= STORAGE_SIZE && size <= STORAGE_SIZE - address;
}

static launchcore_storage_status_t erase(uint32_t address, uint32_t size)
{
    if (!range_ok(address, size)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memset(flash + address, 0xFF, size);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t write(uint32_t address, const void *data, uint32_t size)
{
    if (data == NULL || !range_ok(address, size)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    if (fail_next_app_write && address >= SLOT_A_BASE && address < SLOT_A_BASE + SLOT_A_SIZE) {
        fail_next_app_write = false;
        return LAUNCHCORE_STORAGE_ERR_WRITE;
    }
    memcpy(flash + address, data, size);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t read(uint32_t address, void *data, uint32_t size)
{
    if (data == NULL || !range_ok(address, size)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memcpy(data, flash + address, size);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t memory_mapped(void) { return LAUNCHCORE_STORAGE_OK; }
static bool executable(uint32_t address) { return address >= SLOT_A_BASE && address < SLOT_A_BASE + SLOT_A_SIZE; }
static const launchcore_storage_layout_t *get_layout(void) { return &layout; }

static const launchcore_storage_driver_t driver = {
    .init = init,
    .erase = erase,
    .write = write,
    .read = read,
    .enable_memory_mapped = memory_mapped,
    .is_executable_addr = executable,
    .layout = get_layout,
};

static uint32_t make_image(uint8_t *image, uint8_t fill, uint32_t version)
{
    launchcore_image_header_t header;
    uint8_t payload[PAYLOAD_SIZE];
    memset(payload, fill, sizeof(payload));
    memset(&header, 0, sizeof(header));
    header.magic = LAUNCHCORE_IMAGE_MAGIC;
    header.header_version = LAUNCHCORE_IMAGE_HEADER_VERSION;
    header.header_size = sizeof(header);
    header.image_size = sizeof(payload);
    header.image_crc32 = launchcore_crc32(payload, sizeof(payload));
    launchcore_sha256(payload, sizeof(payload), header.image_sha256);
    header.version_major = version;
    header.image_flags = LAUNCHCORE_IMAGE_FLAG_APPLICATION | LAUNCHCORE_IMAGE_FLAG_XIP;
    header.slot_base_addr = SLOT_A_BASE;
    header.vector_table_addr = SLOT_A_BASE + sizeof(header);
    header.entry_addr = SLOT_A_BASE + sizeof(header) + 1u;
    header.header_crc32 = launchcore_image_header_crc(&header);
    memcpy(image, &header, sizeof(header));
    memcpy(image + sizeof(header), payload, sizeof(payload));
    return sizeof(header) + sizeof(payload);
}

static void refresh_image(uint8_t *image)
{
    launchcore_image_header_t *header = (launchcore_image_header_t *)image;
    uint8_t *payload = image + header->header_size;
    header->image_crc32 = launchcore_crc32(payload, header->image_size);
    launchcore_sha256(payload, header->image_size, header->image_sha256);
    header->header_crc32 = launchcore_image_header_crc(header);
}

static uint32_t encode_block(const uint8_t *block, uint8_t *encoded, uint8_t *codec)
{
    bool same = true;
    for (uint32_t i = 1u; i < ERASE_SIZE; ++i) same = same && block[i] == block[0];
    if (!same) {
        memcpy(encoded, block, ERASE_SIZE);
        *codec = LAUNCHCORE_DELTA_CODEC_RAW;
        return ERASE_SIZE;
    }
    encoded[0] = 0xFFu;
    encoded[1] = block[0];
    encoded[2] = 0xFBu;
    encoded[3] = block[0];
    *codec = LAUNCHCORE_DELTA_CODEC_RLE;
    return 4u;
}

static uint32_t make_patch(uint8_t *patch, const uint8_t *base, uint32_t base_size,
    const uint8_t *target, uint32_t target_size)
{
    launchcore_delta_header_t *header = (launchcore_delta_header_t *)patch;
    uint32_t cursor = sizeof(*header);
    uint32_t records = 0u;
    uint32_t covered = base_size > target_size ? base_size : target_size;
    covered = (covered + ERASE_SIZE - 1u) & ~(ERASE_SIZE - 1u);
    for (uint32_t offset = 0u; offset < covered; offset += ERASE_SIZE) {
        uint8_t old_block[ERASE_SIZE];
        uint8_t new_block[ERASE_SIZE];
        memset(old_block, 0xFF, sizeof(old_block));
        memset(new_block, 0xFF, sizeof(new_block));
        uint32_t old_count = base_size > offset ? base_size - offset : 0u;
        uint32_t new_count = target_size > offset ? target_size - offset : 0u;
        if (old_count > ERASE_SIZE) old_count = ERASE_SIZE;
        if (new_count > ERASE_SIZE) new_count = ERASE_SIZE;
        memcpy(old_block, base + offset, old_count);
        memcpy(new_block, target + offset, new_count);
        if (memcmp(old_block, new_block, ERASE_SIZE) == 0) continue;

        launchcore_delta_record_t record;
        uint8_t old_encoded[ERASE_SIZE];
        uint8_t new_encoded[ERASE_SIZE];
        memset(&record, 0, sizeof(record));
        record.target_offset = offset;
        record.output_size = ERASE_SIZE;
        record.old_data_size = encode_block(old_block, old_encoded, &record.old_codec);
        record.new_data_size = encode_block(new_block, new_encoded, &record.new_codec);
        record.old_crc32 = launchcore_crc32(old_block, sizeof(old_block));
        record.new_crc32 = launchcore_crc32(new_block, sizeof(new_block));
        record.header_crc32 = launchcore_delta_record_crc(&record);
        memcpy(patch + cursor, &record, sizeof(record));
        cursor += sizeof(record);
        memcpy(patch + cursor, old_encoded, record.old_data_size);
        cursor += record.old_data_size;
        memcpy(patch + cursor, new_encoded, record.new_data_size);
        cursor += record.new_data_size;
        ++records;
    }
    memset(header, 0, sizeof(*header));
    header->magic = LAUNCHCORE_DELTA_MAGIC;
    header->version = LAUNCHCORE_DELTA_VERSION;
    header->header_size = sizeof(*header);
    header->total_size = cursor;
    header->erase_size = ERASE_SIZE;
    header->base_image_size = base_size;
    header->target_image_size = target_size;
    header->record_count = records;
    header->records_offset = sizeof(*header);
    launchcore_sha256(base, base_size, header->base_sha256);
    launchcore_sha256(target, target_size, header->target_sha256);
    header->header_crc32 = launchcore_delta_header_crc(header);
    return cursor;
}

int main(void)
{
    launchcore_storage_set_driver(&driver);
    assert(launchcore_storage_init() == LAUNCHCORE_STORAGE_OK);
    uint8_t base[8192];
    uint8_t target[8192];
    uint8_t patch[8192];
    uint32_t base_size = make_image(base, 0x11u, 1u);
    uint32_t target_size = make_image(target, 0x11u, 2u);
    memset(target + 2u * ERASE_SIZE, 0x22, ERASE_SIZE);
    refresh_image(target);
    memcpy(flash + SLOT_A_BASE, base, base_size);

    launchcore_metadata_t md;
    launchcore_metadata_init_default(&md);
    md.active_slot = LAUNCHCORE_SLOT_A;
    md.confirmed_slot = LAUNCHCORE_SLOT_A;
    md.slot_a_state = LAUNCHCORE_SLOT_CONFIRMED;
    memcpy(md.slot_a_hash, ((launchcore_image_header_t *)base)->image_sha256, 32u);
    assert(launchcore_metadata_commit(&md) == LAUNCHCORE_METADATA_OK);

    uint32_t patch_size = make_patch(patch, base, base_size, target, target_size);
    assert(patch_size < target_size);
    assert(launchcore_delta_update_begin(patch_size) == LAUNCHCORE_OK);
    for (uint32_t offset = 0u; offset < patch_size; offset += 73u) {
        uint32_t count = patch_size - offset;
        if (count > 73u) count = 73u;
        assert(launchcore_delta_update_write(patch + offset, count) == LAUNCHCORE_OK);
    }
    assert(launchcore_delta_update_finish() == LAUNCHCORE_OK);
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);

    fail_next_app_write = true;
    assert(launchcore_process_delta_update(&md) == LAUNCHCORE_DELTA_RETRY);
    assert(launchcore_process_delta_update(&md) == LAUNCHCORE_DELTA_READY);
    assert(memcmp(flash + SLOT_A_BASE, target, target_size) == 0);
    assert(md.pending_slot == LAUNCHCORE_SLOT_A);
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_AVAILABLE) != 0u);

    md.flags |= LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING;
    assert(launchcore_metadata_commit(&md) == LAUNCHCORE_METADATA_OK);
    assert(launchcore_process_delta_update(&md) == LAUNCHCORE_DELTA_ROLLED_BACK);
    assert(memcmp(flash + SLOT_A_BASE, base, base_size) == 0);
    assert(md.confirmed_slot == LAUNCHCORE_SLOT_A);
    assert(md.update_result == LAUNCHCORE_UPDATE_RESULT_FAILED);

    patch_size = make_patch(patch, base, base_size, target, target_size);
    launchcore_delta_header_t *delta_header = (launchcore_delta_header_t *)patch;
    launchcore_delta_record_t *first_record =
        (launchcore_delta_record_t *)(patch + delta_header->records_offset);
    uint32_t corrupt_offset = delta_header->records_offset + sizeof(*first_record) +
        first_record->old_data_size;
    patch[corrupt_offset] ^= 0x01u;
    assert(launchcore_delta_update_begin(patch_size) == LAUNCHCORE_OK);
    assert(launchcore_delta_update_write(patch, patch_size) == LAUNCHCORE_OK);
    assert(launchcore_delta_update_finish() == LAUNCHCORE_OK);
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert(launchcore_process_delta_update(&md) == LAUNCHCORE_DELTA_REJECTED);
    assert(memcmp(flash + SLOT_A_BASE, base, base_size) == 0);
    return 0;
}
