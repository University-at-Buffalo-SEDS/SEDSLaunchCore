#include "update_state.h"
#include "launchcore/image.h"
#include "launchcore/storage.h"
#include <stdlib.h>
#include <string.h>

launchcore_status_t launchcore_update_finish(void)
{
    launchcore_update_state_t *state = launchcore_update_state();
    if (!state->active) return LAUNCHCORE_ERR_BAD_STATE;
    uint32_t base = launchcore_slot_base(state->slot);
    launchcore_image_header_t header;
    if (launchcore_storage_driver()->read(base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_ERR_FLASH;
    if (launchcore_image_validate_header(&header) != LAUNCHCORE_IMAGE_OK) return LAUNCHCORE_ERR_BAD_IMAGE;
    if (state->offset != sizeof(header) + header.image_size) return LAUNCHCORE_ERR_BAD_IMAGE;
    uint8_t *payload = (uint8_t *)malloc(header.image_size);
    if (payload == NULL) return LAUNCHCORE_ERR_BAD_STATE;
    if (launchcore_storage_driver()->read(base + header.header_size, payload, header.image_size) != LAUNCHCORE_STORAGE_OK) {
        free(payload);
        return LAUNCHCORE_ERR_FLASH;
    }
    launchcore_image_status_t image_status = launchcore_image_validate(&header, payload);
    if (image_status != LAUNCHCORE_IMAGE_OK) {
        free(payload);
        return LAUNCHCORE_ERR_BAD_IMAGE;
    }
    if (launchcore_expected_hash_is_set(state->info.expected_sha256) &&
        memcmp(state->info.expected_sha256, header.image_sha256, sizeof(header.image_sha256)) != 0) {
        free(payload);
        return LAUNCHCORE_ERR_VERIFY;
    }
    free(payload);
    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.pending_slot = (uint8_t)state->slot;
    md.update_slot = LAUNCHCORE_SLOT_NONE;
    md.boot_attempts = 0u;
    launchcore_metadata_set_slot_state(&md, state->slot, LAUNCHCORE_SLOT_PENDING);
    if (state->slot == LAUNCHCORE_SLOT_A) {
        md.slot_a_version = launchcore_image_packed_version(&header);
        memcpy(md.slot_a_hash, header.image_sha256, 32);
    } else {
        md.slot_b_version = launchcore_image_packed_version(&header);
        memcpy(md.slot_b_hash, header.image_sha256, 32);
    }
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    state->active = false;
    return LAUNCHCORE_OK;
}
