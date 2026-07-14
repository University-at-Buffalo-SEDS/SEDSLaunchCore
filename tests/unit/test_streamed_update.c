#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include "launchcore/update.h"
#include <assert.h>
#include <string.h>

#define PAYLOAD_SIZE (LAUNCHCORE_VALIDATE_CHUNK_SIZE * 3u + 17u)

static void make_image(launchcore_image_header_t *header, uint8_t payload[PAYLOAD_SIZE])
{
    for (size_t i = 0; i < PAYLOAD_SIZE; ++i) payload[i] = (uint8_t)(i * 31u + 7u);
    memset(header, 0, sizeof(*header));
    header->magic = LAUNCHCORE_IMAGE_MAGIC;
    header->header_version = LAUNCHCORE_IMAGE_HEADER_VERSION;
    header->header_size = sizeof(*header);
    header->image_size = PAYLOAD_SIZE;
    header->image_crc32 = launchcore_crc32(payload, PAYLOAD_SIZE);
    launchcore_sha256(payload, PAYLOAD_SIZE, header->image_sha256);
    header->image_flags = LAUNCHCORE_IMAGE_FLAG_APPLICATION;
    header->slot_base_addr = 0u;
    header->vector_table_addr = sizeof(*header);
    header->entry_addr = sizeof(*header) + 1u;
    header->header_crc32 = launchcore_image_header_crc(header);
}

static void write_image_in_chunks(const launchcore_image_header_t *header, const uint8_t payload[PAYLOAD_SIZE])
{
    const uint8_t *header_bytes = (const uint8_t *)header;
    for (size_t offset = 0; offset < sizeof(*header); offset += 37u) {
        size_t len = sizeof(*header) - offset;
        if (len > 37u) len = 37u;
        assert(launchcore_update_write(header_bytes + offset, len) == LAUNCHCORE_OK);
    }
    for (size_t offset = 0; offset < PAYLOAD_SIZE; offset += 73u) {
        size_t len = PAYLOAD_SIZE - offset;
        if (len > 73u) len = 73u;
        assert(launchcore_update_write(payload + offset, len) == LAUNCHCORE_OK);
    }
}

int main(void)
{
    launchcore_image_header_t header;
    uint8_t payload[PAYLOAD_SIZE];
    make_image(&header, payload);

    launchcore_update_info_t info;
    memset(&info, 0, sizeof(info));
    info.image_size = sizeof(header) + sizeof(payload);
    memcpy(info.expected_sha256, header.image_sha256, sizeof(info.expected_sha256));

    assert(launchcore_update_begin(&info) == LAUNCHCORE_OK);
    write_image_in_chunks(&header, payload);
    uint8_t corrupt = (uint8_t)(payload[10] ^ 0x80u);
    assert(launchcore_storage_driver()->write(sizeof(header) + 10u, &corrupt, 1u) == LAUNCHCORE_STORAGE_OK);
    assert(launchcore_update_finish() == LAUNCHCORE_ERR_BAD_IMAGE);
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    assert(md.pending_slot == LAUNCHCORE_SLOT_NONE);
    assert(launchcore_metadata_slot_state(&md, LAUNCHCORE_SLOT_A) == LAUNCHCORE_SLOT_WRITING);
    assert(launchcore_update_abort() == LAUNCHCORE_OK);

    assert(launchcore_update_begin(&info) == LAUNCHCORE_OK);
    write_image_in_chunks(&header, payload);
    assert(launchcore_update_finish() == LAUNCHCORE_OK);
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert(md.pending_slot == LAUNCHCORE_SLOT_A);
    assert(launchcore_metadata_slot_state(&md, LAUNCHCORE_SLOT_A) == LAUNCHCORE_SLOT_PENDING);
    assert(memcmp(md.slot_a_hash, header.image_sha256, sizeof(md.slot_a_hash)) == 0);
    launchcore_update_report_t report;
    assert(launchcore_get_update_report(&report) == LAUNCHCORE_OK);
    assert(report.kind == LAUNCHCORE_UPDATE_KIND_APPLICATION);
    assert(!report.attempted);
    assert(report.result == LAUNCHCORE_UPDATE_RESULT_PENDING);
    assert(report.reason == LAUNCHCORE_UPDATE_REASON_NONE);

    md.update_attempted = 1u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_VALIDATED;
    assert(launchcore_metadata_commit(&md) == LAUNCHCORE_METADATA_OK);
    assert(launchcore_confirm_boot() == LAUNCHCORE_OK);
    assert(launchcore_get_update_report(&report) == LAUNCHCORE_OK);
    assert(report.attempted);
    assert(report.result == LAUNCHCORE_UPDATE_RESULT_SUCCESS);
    return 0;
}
