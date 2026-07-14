#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include "launchcore/update_status.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    bool active;
    uint32_t image_size;
    uint32_t offset;
} recovery_install_state_t;

static recovery_install_state_t state;

static launchcore_recovery_status_t fail_install(launchcore_update_reason_t reason,
    launchcore_recovery_status_t status)
{
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.update_slot = LAUNCHCORE_SLOT_NONE;
    md.pending_slot = LAUNCHCORE_SLOT_NONE;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_BAD);
    md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md.update_attempted = 1u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md.update_reason = (uint8_t)reason;
    (void)launchcore_metadata_commit(&md);
    state.active = false;
    return status;
}

launchcore_recovery_status_t launchcore_recovery_install_begin(uint32_t image_size)
{
    if (state.active) return LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
    if (image_size == 0u || image_size > launchcore_slot_size(LAUNCHCORE_SLOT_A)) {
        return LAUNCHCORE_RECOVERY_ERR_NO_SPACE;
    }
    uint32_t base = launchcore_slot_base(LAUNCHCORE_SLOT_A);
    uint32_t size = launchcore_slot_size(LAUNCHCORE_SLOT_A);
    if (!launchcore_storage_range_preserves_persistent(base, size)) return LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.update_slot = LAUNCHCORE_SLOT_A;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_WRITING);
    md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md.update_attempted = 1u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_PENDING;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
    if (launchcore_storage_driver()->erase(base, size) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
    state.active = true;
    state.image_size = image_size;
    state.offset = 0u;
    return LAUNCHCORE_RECOVERY_OK;
}

launchcore_recovery_status_t launchcore_recovery_install_write(const void *data, size_t len)
{
    if (!state.active || data == NULL) return LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
    if (state.offset > state.image_size || len > state.image_size - state.offset) {
        return LAUNCHCORE_RECOVERY_ERR_NO_SPACE;
    }
    if (launchcore_storage_driver()->write(launchcore_slot_base(LAUNCHCORE_SLOT_A) + state.offset,
        data, (uint32_t)len) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
    state.offset += (uint32_t)len;
    return LAUNCHCORE_RECOVERY_OK;
}

launchcore_recovery_status_t launchcore_recovery_install_finish(void)
{
    if (!state.active || state.offset != state.image_size) return LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
    uint32_t base = launchcore_slot_base(LAUNCHCORE_SLOT_A);
    launchcore_image_header_t header;
    if (launchcore_storage_driver()->read(base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK) {
        return fail_install(LAUNCHCORE_UPDATE_REASON_STORAGE, LAUNCHCORE_RECOVERY_ERR_STORAGE);
    }
    launchcore_image_status_t image_status = launchcore_image_validate_header(&header);
    if (image_status != LAUNCHCORE_IMAGE_OK) {
        return fail_install(LAUNCHCORE_UPDATE_REASON_HEADER, LAUNCHCORE_RECOVERY_ERR_BAD_IMAGE);
    }
    if (header.header_size > state.image_size || header.image_size != state.image_size - header.header_size ||
        header.slot_base_addr != base) {
        return fail_install(LAUNCHCORE_UPDATE_REASON_SIZE, LAUNCHCORE_RECOVERY_ERR_BAD_IMAGE);
    }
    image_status = launchcore_image_validate_stored(&header, base + header.header_size);
    if (image_status != LAUNCHCORE_IMAGE_OK) {
        launchcore_update_reason_t reason = image_status == LAUNCHCORE_IMAGE_ERR_IMAGE_CRC ?
            LAUNCHCORE_UPDATE_REASON_CRC32 : LAUNCHCORE_UPDATE_REASON_SHA256;
        return fail_install(reason, LAUNCHCORE_RECOVERY_ERR_BAD_IMAGE);
    }
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.pending_slot = LAUNCHCORE_SLOT_A;
    md.update_slot = LAUNCHCORE_SLOT_NONE;
    md.boot_attempts = 0u;
    md.slot_a_version = launchcore_image_packed_version(&header);
    memcpy(md.slot_a_hash, header.image_sha256, sizeof(md.slot_a_hash));
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_PENDING);
    md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    md.update_attempted = 1u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_VALIDATED;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
    state.active = false;
    return LAUNCHCORE_RECOVERY_OK;
}

launchcore_recovery_status_t launchcore_recovery_install_abort(void)
{
    if (!state.active) return LAUNCHCORE_RECOVERY_ERR_BAD_STATE;
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.update_slot = LAUNCHCORE_SLOT_NONE;
    md.pending_slot = LAUNCHCORE_SLOT_NONE;
    launchcore_metadata_set_slot_state(&md, LAUNCHCORE_SLOT_A, LAUNCHCORE_SLOT_BAD);
    md.update_result = LAUNCHCORE_UPDATE_RESULT_FAILED;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_STORAGE;
    (void)launchcore_metadata_commit(&md);
    state.active = false;
    return LAUNCHCORE_RECOVERY_OK;
}
