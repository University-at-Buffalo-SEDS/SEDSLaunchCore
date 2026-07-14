#include "launchcore/bundle.h"
#include "launchcore/config.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include "launchcore/update.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    bool active;
    uint32_t base;
    uint32_t size;
    uint32_t offset;
} bundle_update_state_t;

static bundle_update_state_t state;

static bool validate_bootloader_artifact(const launchcore_bundle_header_t *bundle)
{
    uint8_t chunk[LAUNCHCORE_VALIDATE_CHUNK_SIZE];
    launchcore_sha256_ctx_t sha256;
    uint32_t crc32 = 0u;
    uint32_t remaining = bundle->bootloader_size;
    uint32_t address = state.base + bundle->bootloader_offset;
    launchcore_sha256_init(&sha256);
    while (remaining > 0u) {
        uint32_t size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (launchcore_storage_driver()->read(address, chunk, size) != LAUNCHCORE_STORAGE_OK) return false;
        crc32 = launchcore_crc32_update(crc32, chunk, size);
        launchcore_sha256_update(&sha256, chunk, size);
        address += size;
        remaining -= size;
    }
    uint8_t hash[32];
    launchcore_sha256_final(&sha256, hash);
    return crc32 == bundle->bootloader_crc32 &&
        memcmp(hash, bundle->bootloader_sha256, sizeof(hash)) == 0;
}

launchcore_status_t launchcore_bundle_update_begin(uint32_t bundle_size)
{
    if (state.active || bundle_size == 0u) return LAUNCHCORE_ERR_BAD_STATE;
    if (launchcore_storage_init() != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_ERR_FLASH;
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    uint32_t base;
    uint32_t capacity;
    if (bundle_size <= layout->app_staging_size) {
        base = layout->app_staging_base;
        capacity = layout->app_staging_size;
    } else if (bundle_size <= layout->bootloader_staging_size) {
        base = layout->bootloader_staging_base;
        capacity = layout->bootloader_staging_size;
    } else {
        return LAUNCHCORE_ERR_NO_SPACE;
    }
    if (!launchcore_storage_range_preserves_persistent(base, capacity)) return LAUNCHCORE_ERR_BAD_STATE;
    if (launchcore_storage_driver()->erase(base, capacity) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_ERR_FLASH;
    state = (bundle_update_state_t){true, base, bundle_size, 0u};
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_bundle_update_write(const void *data, size_t len)
{
    if (!state.active || data == NULL) return LAUNCHCORE_ERR_BAD_STATE;
    if (state.offset > state.size || len > state.size - state.offset) return LAUNCHCORE_ERR_NO_SPACE;
    if (launchcore_storage_driver()->write(state.base + state.offset, data, (uint32_t)len) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_ERR_FLASH;
    }
    state.offset += (uint32_t)len;
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_bundle_update_finish(void)
{
    if (!state.active || state.offset != state.size) return LAUNCHCORE_ERR_BAD_STATE;
    launchcore_bundle_header_t bundle;
    if (launchcore_storage_driver()->read(state.base, &bundle, sizeof(bundle)) != LAUNCHCORE_STORAGE_OK) {
        return LAUNCHCORE_ERR_FLASH;
    }
    if (!launchcore_bundle_header_is_valid(&bundle, state.size) || bundle.total_size != state.size) {
        return LAUNCHCORE_ERR_BAD_IMAGE;
    }
    if (bundle.bootloader_size != 0u && !validate_bootloader_artifact(&bundle)) return LAUNCHCORE_ERR_BAD_IMAGE;

    launchcore_image_header_t app_header;
    if (bundle.app_size != 0u) {
        const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
        if (state.base != layout->app_staging_base) return LAUNCHCORE_ERR_BAD_STATE;
        uint32_t app_addr = state.base + bundle.app_offset;
        if (launchcore_storage_driver()->read(app_addr, &app_header, sizeof(app_header)) != LAUNCHCORE_STORAGE_OK) {
            return LAUNCHCORE_ERR_FLASH;
        }
        if (launchcore_image_validate_header(&app_header) != LAUNCHCORE_IMAGE_OK ||
            app_header.header_size > bundle.app_size ||
            app_header.image_size != bundle.app_size - app_header.header_size ||
            launchcore_image_validate_stored(&app_header, app_addr + app_header.header_size) != LAUNCHCORE_IMAGE_OK ||
            memcmp(bundle.app_sha256, app_header.image_sha256, sizeof(bundle.app_sha256)) != 0) {
            return LAUNCHCORE_ERR_BAD_IMAGE;
        }
    }

    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    md.staged_bundle_base = state.base;
    md.staged_bundle_size = state.size;
    md.update_attempted = 0u;
    md.update_result = LAUNCHCORE_UPDATE_RESULT_PENDING;
    md.update_reason = LAUNCHCORE_UPDATE_REASON_NONE;
    launchcore_slot_id_t active = launchcore_slot_id_is_valid((launchcore_slot_id_t)md.active_slot) ?
        (launchcore_slot_id_t)md.active_slot : (launchcore_slot_id_t)md.confirmed_slot;
    launchcore_slot_id_t app_target = launchcore_storage_driver()->layout()->slot_b_size == 0u ? LAUNCHCORE_SLOT_A :
        (active == LAUNCHCORE_SLOT_A ? LAUNCHCORE_SLOT_B : LAUNCHCORE_SLOT_A);
    if (bundle.bootloader_size != 0u && bundle.bootloader_version > LAUNCHCORE_BOOTLOADER_VERSION) {
        md.flags |= LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING;
        md.update_kind = LAUNCHCORE_UPDATE_KIND_BOOTLOADER;
    } else if (bundle.app_size != 0u) {
        md.flags |= LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING;
        md.update_slot = (uint8_t)app_target;
        md.staged_app_addr = state.base + bundle.app_offset;
        md.staged_app_size = bundle.app_size;
        md.staged_app_version = launchcore_image_packed_version(&app_header);
        memcpy(md.staged_app_hash, bundle.app_sha256, sizeof(md.staged_app_hash));
        md.update_kind = LAUNCHCORE_UPDATE_KIND_APPLICATION;
    } else {
        md.update_kind = LAUNCHCORE_UPDATE_KIND_BOOTLOADER;
        md.update_result = LAUNCHCORE_UPDATE_RESULT_SUCCESS;
    }
    if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_ERR_METADATA;
    state.active = false;
    return LAUNCHCORE_OK;
}

launchcore_status_t launchcore_bundle_update_abort(void)
{
    if (!state.active) return LAUNCHCORE_ERR_BAD_STATE;
    state.active = false;
    return LAUNCHCORE_OK;
}
