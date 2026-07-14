#include "launchcore/delta.h"
#include "launchcore/image.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <stddef.h>
#include <string.h>

typedef enum { DELTA_IO_OK, DELTA_IO_FORMAT, DELTA_IO_STORAGE } delta_io_status_t;

uint32_t launchcore_delta_header_crc(const launchcore_delta_header_t *header)
{
    return launchcore_crc32(header, offsetof(launchcore_delta_header_t, header_crc32));
}

uint32_t launchcore_delta_record_crc(const launchcore_delta_record_t *record)
{
    return launchcore_crc32(record, offsetof(launchcore_delta_record_t, header_crc32));
}

bool launchcore_delta_header_is_valid(const launchcore_delta_header_t *header, uint32_t available_size)
{
    return header != NULL && header->magic == LAUNCHCORE_DELTA_MAGIC &&
        header->version == LAUNCHCORE_DELTA_VERSION && header->header_size >= sizeof(*header) &&
        header->records_offset >= header->header_size && header->records_offset <= header->total_size &&
        header->total_size <= available_size && header->record_count != 0u &&
        header->erase_size != 0u && header->base_image_size != 0u && header->target_image_size != 0u &&
        header->header_crc32 == launchcore_delta_header_crc(header);
}

static delta_io_status_t stored_hash(uint32_t address, uint32_t size, uint8_t hash[32])
{
    uint8_t chunk[LAUNCHCORE_VALIDATE_CHUNK_SIZE];
    launchcore_sha256_ctx_t ctx;
    launchcore_sha256_init(&ctx);
    while (size != 0u) {
        uint32_t count = size < sizeof(chunk) ? size : sizeof(chunk);
        if (launchcore_storage_driver()->read(address, chunk, count) != LAUNCHCORE_STORAGE_OK) {
            return DELTA_IO_STORAGE;
        }
        launchcore_sha256_update(&ctx, chunk, count);
        address += count;
        size -= count;
    }
    launchcore_sha256_final(&ctx, hash);
    return DELTA_IO_OK;
}

static delta_io_status_t decode_to_slot(uint32_t source, uint32_t encoded_size, uint8_t codec,
    uint32_t destination, uint32_t output_size, uint32_t expected_crc, bool write_output)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    uint8_t output[LAUNCHCORE_VALIDATE_CHUNK_SIZE];
    uint32_t input_offset = 0u;
    uint32_t output_offset = 0u;
    uint32_t buffered = 0u;
    uint32_t crc = 0u;

#define FLUSH_OUTPUT() do { \
    if (buffered != 0u) { \
        if (write_output && storage->write(destination + output_offset - buffered, output, buffered) != LAUNCHCORE_STORAGE_OK) \
            return DELTA_IO_STORAGE; \
        crc = launchcore_crc32_update(crc, output, buffered); \
        buffered = 0u; \
    } \
} while (0)
#define EMIT_BYTE(value) do { \
    if (output_offset >= output_size) return DELTA_IO_FORMAT; \
    output[buffered++] = (uint8_t)(value); \
    ++output_offset; \
    if (buffered == sizeof(output)) FLUSH_OUTPUT(); \
} while (0)

    if (codec == LAUNCHCORE_DELTA_CODEC_RAW) {
        if (encoded_size != output_size) return DELTA_IO_FORMAT;
        while (input_offset < encoded_size) {
            uint32_t count = encoded_size - input_offset;
            if (count > sizeof(output)) count = sizeof(output);
            if (storage->read(source + input_offset, output, count) != LAUNCHCORE_STORAGE_OK ||
                (write_output && storage->write(destination + input_offset, output, count) != LAUNCHCORE_STORAGE_OK)) {
                return DELTA_IO_STORAGE;
            }
            crc = launchcore_crc32_update(crc, output, count);
            input_offset += count;
        }
        output_offset = output_size;
    } else if (codec == LAUNCHCORE_DELTA_CODEC_RLE) {
        while (input_offset < encoded_size) {
            uint8_t token;
            if (storage->read(source + input_offset++, &token, 1u) != LAUNCHCORE_STORAGE_OK) return DELTA_IO_STORAGE;
            if ((token & 0x80u) != 0u) {
                uint8_t value;
                uint32_t count = (uint32_t)(token & 0x7Fu) + 3u;
                if (input_offset >= encoded_size ||
                    storage->read(source + input_offset++, &value, 1u) != LAUNCHCORE_STORAGE_OK) {
                    return DELTA_IO_FORMAT;
                }
                while (count-- != 0u) EMIT_BYTE(value);
            } else {
                uint32_t count = (uint32_t)token + 1u;
                if (count > encoded_size - input_offset) return DELTA_IO_FORMAT;
                while (count-- != 0u) {
                    uint8_t value;
                    if (storage->read(source + input_offset++, &value, 1u) != LAUNCHCORE_STORAGE_OK) {
                        return DELTA_IO_STORAGE;
                    }
                    EMIT_BYTE(value);
                }
            }
        }
        FLUSH_OUTPUT();
    } else {
        return DELTA_IO_FORMAT;
    }
#undef EMIT_BYTE
#undef FLUSH_OUTPUT
    return output_offset == output_size && crc == expected_crc ? DELTA_IO_OK : DELTA_IO_FORMAT;
}

static delta_io_status_t validate_records(const launchcore_delta_header_t *header)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    uint32_t cursor = layout->slot_b_base + header->records_offset;
    uint32_t patch_end = layout->slot_b_base + header->total_size;
    for (uint32_t index = 0u; index < header->record_count; ++index) {
        launchcore_delta_record_t record;
        if (cursor > patch_end || sizeof(record) > patch_end - cursor ||
            storage->read(cursor, &record, sizeof(record)) != LAUNCHCORE_STORAGE_OK) return DELTA_IO_STORAGE;
        cursor += sizeof(record);
        uint64_t data_size = (uint64_t)record.old_data_size + record.new_data_size;
        if (record.header_crc32 != launchcore_delta_record_crc(&record) ||
            record.output_size != header->erase_size || record.target_offset % header->erase_size != 0u ||
            record.target_offset > layout->slot_a_size || record.output_size > layout->slot_a_size - record.target_offset ||
            data_size > patch_end - cursor) return DELTA_IO_FORMAT;
        delta_io_status_t old_status = decode_to_slot(cursor, record.old_data_size, record.old_codec,
            0u, record.output_size, record.old_crc32, false);
        delta_io_status_t new_status = decode_to_slot(cursor + record.old_data_size, record.new_data_size,
            record.new_codec, 0u, record.output_size, record.new_crc32, false);
        if (old_status != DELTA_IO_OK) return old_status;
        if (new_status != DELTA_IO_OK) return new_status;
        cursor += (uint32_t)data_size;
    }
    return cursor == patch_end ? DELTA_IO_OK : DELTA_IO_FORMAT;
}

static delta_io_status_t apply_records(const launchcore_delta_header_t *header, bool reverse)
{
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    delta_io_status_t validation = validate_records(header);
    if (validation != DELTA_IO_OK) return validation;
    uint32_t cursor = layout->slot_b_base + header->records_offset;
    uint32_t patch_end = layout->slot_b_base + header->total_size;
    for (uint32_t index = 0u; index < header->record_count; ++index) {
        launchcore_delta_record_t record;
        if (cursor > patch_end || sizeof(record) > patch_end - cursor ||
            storage->read(cursor, &record, sizeof(record)) != LAUNCHCORE_STORAGE_OK) return DELTA_IO_STORAGE;
        cursor += sizeof(record);
        uint64_t data_size = (uint64_t)record.old_data_size + record.new_data_size;
        if (record.header_crc32 != launchcore_delta_record_crc(&record) ||
            record.output_size != header->erase_size || record.target_offset % header->erase_size != 0u ||
            record.target_offset > layout->slot_a_size || record.output_size > layout->slot_a_size - record.target_offset ||
            data_size > patch_end - cursor) return DELTA_IO_FORMAT;
        uint32_t source = reverse ? cursor : cursor + record.old_data_size;
        uint32_t size = reverse ? record.old_data_size : record.new_data_size;
        uint8_t codec = reverse ? record.old_codec : record.new_codec;
        uint32_t crc = reverse ? record.old_crc32 : record.new_crc32;
        uint32_t target = layout->slot_a_base + record.target_offset;
        if (storage->erase(target, record.output_size) != LAUNCHCORE_STORAGE_OK) return DELTA_IO_STORAGE;
        delta_io_status_t status = decode_to_slot(source, size, codec, target, record.output_size, crc, true);
        if (status != DELTA_IO_OK) return status;
        cursor += (uint32_t)data_size;
    }
    return cursor == patch_end ? DELTA_IO_OK : DELTA_IO_FORMAT;
}

static launchcore_delta_status_t reject(launchcore_metadata_t *md, launchcore_update_reason_t reason)
{
    md->flags &= ~(LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING |
        LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED);
    md->update_attempted = 1u;
    md->update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md->update_reason = (uint8_t)reason;
    return launchcore_metadata_commit(md) == LAUNCHCORE_METADATA_OK ?
        LAUNCHCORE_DELTA_REJECTED : LAUNCHCORE_DELTA_RETRY;
}

launchcore_delta_status_t launchcore_process_delta_update(launchcore_metadata_t *md)
{
    if (md == NULL) return LAUNCHCORE_DELTA_REJECTED;
    bool reverse = (md->flags & LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING) != 0u;
    bool forward = (md->flags & LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING) != 0u;
    if (!forward && !reverse) return LAUNCHCORE_DELTA_NONE;

    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();
    launchcore_delta_header_t header;
    if (!layout->slot_b_is_delta || layout->slot_erase_size == 0u ||
        storage->read(layout->slot_b_base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK ||
        !launchcore_delta_header_is_valid(&header, layout->slot_b_size) ||
        header.erase_size != layout->slot_erase_size ||
        header.base_image_size > layout->slot_a_size || header.target_image_size > layout->slot_a_size) {
        return reverse ? LAUNCHCORE_DELTA_RETRY : reject(md, LAUNCHCORE_UPDATE_REASON_HEADER);
    }
    delta_io_status_t preflight = validate_records(&header);
    if (preflight == DELTA_IO_STORAGE) return LAUNCHCORE_DELTA_RETRY;
    if (preflight != DELTA_IO_OK) {
        bool application_started = (md->flags & (LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED |
            LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED)) != 0u;
        return reverse || application_started ? LAUNCHCORE_DELTA_RETRY :
            reject(md, LAUNCHCORE_UPDATE_REASON_DECOMPRESSION);
    }

    uint32_t current_size = reverse ? header.target_image_size : header.base_image_size;
    const uint8_t *expected = reverse ? header.target_sha256 : header.base_sha256;
    const uint8_t *finished = reverse ? header.base_sha256 : header.target_sha256;
    uint32_t finished_size = reverse ? header.base_image_size : header.target_image_size;
    uint32_t started_flag = reverse ? LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED :
        LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED;
    uint8_t hash[32];
    delta_io_status_t hash_status = stored_hash(layout->slot_a_base, current_size, hash);
    if (hash_status == DELTA_IO_STORAGE) return LAUNCHCORE_DELTA_RETRY;
    bool already_finished = stored_hash(layout->slot_a_base, finished_size, hash) == DELTA_IO_OK &&
        memcmp(hash, finished, sizeof(hash)) == 0;
    if (!already_finished) {
        if ((md->flags & started_flag) == 0u) {
            if (stored_hash(layout->slot_a_base, current_size, hash) != DELTA_IO_OK) return LAUNCHCORE_DELTA_RETRY;
            if (memcmp(hash, expected, sizeof(hash)) != 0) {
                return reverse ? LAUNCHCORE_DELTA_RETRY :
                    reject(md, LAUNCHCORE_UPDATE_REASON_BASE_MISMATCH);
            }
            md->flags |= started_flag;
            if (launchcore_metadata_commit(md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_DELTA_RETRY;
        }
        delta_io_status_t apply = apply_records(&header, reverse);
        if (apply != DELTA_IO_OK) {
            if (apply == DELTA_IO_FORMAT && !reverse) {
                md->flags &= ~LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING;
                md->flags |= LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING |
                    LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED;
                md->update_reason = LAUNCHCORE_UPDATE_REASON_DECOMPRESSION;
                (void)launchcore_metadata_commit(md);
            }
            return LAUNCHCORE_DELTA_RETRY;
        }
    }

    if (stored_hash(layout->slot_a_base, finished_size, hash) != DELTA_IO_OK) return LAUNCHCORE_DELTA_RETRY;
    if (memcmp(hash, finished, sizeof(hash)) != 0) {
        if (!reverse) {
            md->flags &= ~LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING;
            md->flags |= LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING |
                LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED;
            (void)launchcore_metadata_commit(md);
        }
        return LAUNCHCORE_DELTA_RETRY;
    }
    launchcore_image_header_t image;
    if (storage->read(layout->slot_a_base, &image, sizeof(image)) != LAUNCHCORE_STORAGE_OK ||
        image.header_size > finished_size || image.image_size != finished_size - image.header_size ||
        launchcore_image_validate_stored(&image, layout->slot_a_base + image.header_size) != LAUNCHCORE_IMAGE_OK) {
        if (!reverse) {
            md->flags &= ~LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING;
            md->flags |= LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING |
                LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED;
            (void)launchcore_metadata_commit(md);
        }
        return LAUNCHCORE_DELTA_RETRY;
    }

    md->slot_a_version = launchcore_image_packed_version(&image);
    memcpy(md->slot_a_hash, image.image_sha256, sizeof(md->slot_a_hash));
    md->boot_attempts = 0u;
    if (reverse) {
        md->flags &= ~(LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING |
            LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED |
            LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_AVAILABLE |
            LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED);
        md->pending_slot = LAUNCHCORE_SLOT_NONE;
        md->active_slot = LAUNCHCORE_SLOT_A;
        md->confirmed_slot = LAUNCHCORE_SLOT_A;
        launchcore_metadata_set_slot_state(md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_CONFIRMED);
        launchcore_metadata_set_slot_state(md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_EMPTY);
        md->update_attempted = 1u;
        md->update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
        md->update_reason = LAUNCHCORE_UPDATE_REASON_NOT_CONFIRMED;
    } else {
        md->flags &= ~(LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING |
            LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED);
        md->flags |= LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_AVAILABLE;
        md->pending_slot = LAUNCHCORE_SLOT_A;
        md->active_slot = LAUNCHCORE_SLOT_A;
        launchcore_metadata_set_slot_state(md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_PENDING);
        launchcore_metadata_set_slot_state(md, LAUNCHCORE_SLOT_B, LAUNCHCORE_SLOT_VALID_ROLLBACK);
        md->update_attempted = 1u;
        md->update_result = LAUNCHCORE_UPDATE_RESULT_PENDING;
        md->update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    }
    if (launchcore_metadata_commit(md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_DELTA_RETRY;
    return reverse ? LAUNCHCORE_DELTA_ROLLED_BACK : LAUNCHCORE_DELTA_READY;
}
