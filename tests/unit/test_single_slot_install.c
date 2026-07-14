#include "launchcore/boot.h"
#include "launchcore/bundle.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/persist.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include "launchcore/update.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define STORAGE_SIZE 0x00100000u
#define APP_BASE 0x00010000u
#define APP_SIZE 0x00070000u
#define STAGING_BASE 0x00080000u
#define STAGING_SIZE 0x00080000u
#define PAYLOAD_SIZE (LAUNCHCORE_VALIDATE_CHUNK_SIZE * 3u + 11u)

static uint8_t storage_bytes[STORAGE_SIZE];
static bool initialized;
static unsigned int app_writes_before_failure;
static unsigned int bootloader_flash_count;

bool platform_verify_bootloader_signature(const uint8_t hash[32], const uint8_t *signature, uint32_t signature_len)
{
    (void)hash;
    return signature_len == 1u && signature[0] == 0xA5u;
}

bool platform_flash_bootloader_image(uint32_t source_addr, uint32_t image_size)
{
    assert(source_addr >= STAGING_BASE);
    assert(image_size != 0u);
    ++bootloader_flash_count;
    return true;
}

void platform_system_reset(void)
{
}

static const launchcore_storage_layout_t layout = {
    .slot_a_base = APP_BASE,
    .slot_a_size = APP_SIZE,
    .slot_b_base = 0u,
    .slot_b_size = 0u,
    .metadata0_base = 0x00001000u,
    .metadata1_base = 0x00002000u,
    .metadata_size = 0x00001000u,
    .app_staging_base = STAGING_BASE,
    .app_staging_size = STAGING_SIZE,
    .persistent_data_base = 0x00004000u,
    .persistent_data_size = 0x00008000u,
    .supports_xip = true,
};

static bool range_ok(uint32_t addr, uint32_t len)
{
    return addr <= STORAGE_SIZE && len <= STORAGE_SIZE - addr;
}

static launchcore_storage_status_t test_init(void)
{
    if (!initialized) {
        memset(storage_bytes, 0xFF, sizeof(storage_bytes));
        initialized = true;
    }
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t test_erase(uint32_t addr, uint32_t len)
{
    if (!range_ok(addr, len)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memset(storage_bytes + addr, 0xFF, len);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t test_write(uint32_t addr, const void *data, uint32_t len)
{
    if (!range_ok(addr, len) || data == NULL) return LAUNCHCORE_STORAGE_ERR_RANGE;
    if (addr >= APP_BASE && addr < APP_BASE + APP_SIZE && app_writes_before_failure > 0u) {
        if (--app_writes_before_failure == 0u) return LAUNCHCORE_STORAGE_ERR_WRITE;
    }
    memcpy(storage_bytes + addr, data, len);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t test_read(uint32_t addr, void *data, uint32_t len)
{
    if (!range_ok(addr, len) || data == NULL) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memcpy(data, storage_bytes + addr, len);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t test_enable_memory_mapped(void) { return LAUNCHCORE_STORAGE_OK; }
static bool test_executable(uint32_t addr) { return addr >= APP_BASE && addr < APP_BASE + APP_SIZE; }
static const launchcore_storage_layout_t *test_layout(void) { return &layout; }

static const launchcore_storage_driver_t driver = {
    .init = test_init,
    .erase = test_erase,
    .write = test_write,
    .read = test_read,
    .enable_memory_mapped = test_enable_memory_mapped,
    .is_executable_addr = test_executable,
    .layout = test_layout,
};

int main(void)
{
    launchcore_storage_set_driver(&driver);
    assert(launchcore_persist_init() == LAUNCHCORE_PERSIST_OK);
    uint32_t persistent_value = 0xC0DEC0DEu;
    assert(launchcore_persist_set(1u, &persistent_value, sizeof(persistent_value)) == LAUNCHCORE_PERSIST_OK);

    uint8_t payload[PAYLOAD_SIZE];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i * 13u + 9u);
    launchcore_image_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = LAUNCHCORE_IMAGE_MAGIC;
    header.header_version = LAUNCHCORE_IMAGE_HEADER_VERSION;
    header.header_size = sizeof(header);
    header.image_size = sizeof(payload);
    header.image_crc32 = launchcore_crc32(payload, sizeof(payload));
    launchcore_sha256(payload, sizeof(payload), header.image_sha256);
    header.image_flags = LAUNCHCORE_IMAGE_FLAG_APPLICATION;
    header.slot_base_addr = APP_BASE;
    header.vector_table_addr = APP_BASE + sizeof(header);
    header.entry_addr = APP_BASE + sizeof(header) + 1u;
    header.header_crc32 = launchcore_image_header_crc(&header);

    launchcore_update_info_t info;
    memset(&info, 0, sizeof(info));
    info.image_size = sizeof(header) + sizeof(payload);
    memcpy(info.expected_sha256, header.image_sha256, sizeof(info.expected_sha256));
    assert(launchcore_update_begin(&info) == LAUNCHCORE_OK);
    assert(launchcore_update_write(&header, sizeof(header)) == LAUNCHCORE_OK);
    for (size_t offset = 0; offset < sizeof(payload); offset += 79u) {
        size_t len = sizeof(payload) - offset;
        if (len > 79u) len = 79u;
        assert(launchcore_update_write(payload + offset, len) == LAUNCHCORE_OK);
    }
    assert(launchcore_update_finish() == LAUNCHCORE_OK);

    launchcore_metadata_t md;
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING) != 0u);
    assert(md.pending_slot == LAUNCHCORE_SLOT_NONE);

    app_writes_before_failure = 3u;
    assert(launchcore_install_staged_application(&md) == LAUNCHCORE_APP_INSTALL_RETRY);
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING) != 0u);
    app_writes_before_failure = 0u;
    assert(launchcore_install_staged_application(&md) == LAUNCHCORE_APP_INSTALL_READY);
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING) == 0u);
    assert(md.pending_slot == LAUNCHCORE_SLOT_A);
    assert(memcmp(storage_bytes + APP_BASE, &header, sizeof(header)) == 0);
    assert(memcmp(storage_bytes + APP_BASE + sizeof(header), payload, sizeof(payload)) == 0);

    uint32_t persisted = 0u;
    size_t persisted_size = sizeof(persisted);
    assert(launchcore_persist_get(1u, &persisted, &persisted_size) == LAUNCHCORE_PERSIST_OK);
    assert(persisted == persistent_value);

    assert(launchcore_recovery_install_begin(info.image_size) == LAUNCHCORE_RECOVERY_OK);
    assert(launchcore_recovery_install_write(&header, sizeof(header)) == LAUNCHCORE_RECOVERY_OK);
    assert(launchcore_recovery_install_write(payload, sizeof(payload)) == LAUNCHCORE_RECOVERY_OK);
    assert(launchcore_recovery_install_finish() == LAUNCHCORE_RECOVERY_OK);
    persisted = 0u;
    persisted_size = sizeof(persisted);
    assert(launchcore_persist_get(1u, &persisted, &persisted_size) == LAUNCHCORE_PERSIST_OK);
    assert(persisted == persistent_value);

    uint8_t bootloader_image[300];
    for (size_t i = 0; i < sizeof(bootloader_image); ++i) bootloader_image[i] = (uint8_t)(i * 7u + 1u);
    uint32_t bootloader_offset = 256u;
    uint32_t app_offset = 768u;
    uint32_t bundle_size = app_offset + sizeof(header) + sizeof(payload);
    uint8_t bundle_bytes[2048];
    assert(bundle_size <= sizeof(bundle_bytes));
    memset(bundle_bytes, 0, bundle_size);
    launchcore_bundle_header_t *bundle = (launchcore_bundle_header_t *)bundle_bytes;
    bundle->magic = LAUNCHCORE_BUNDLE_MAGIC;
    bundle->version = LAUNCHCORE_BUNDLE_VERSION;
    bundle->header_size = sizeof(*bundle);
    bundle->total_size = bundle_size;
    bundle->bootloader_offset = bootloader_offset;
    bundle->bootloader_size = sizeof(bootloader_image);
    bundle->bootloader_version = 2u;
    bundle->bootloader_crc32 = launchcore_crc32(bootloader_image, sizeof(bootloader_image));
    launchcore_sha256(bootloader_image, sizeof(bootloader_image), bundle->bootloader_sha256);
    bundle->bootloader_signature[0] = 0xA5u;
    bundle->bootloader_signature_size = 1u;
    bundle->app_offset = app_offset;
    bundle->app_size = sizeof(header) + sizeof(payload);
    memcpy(bundle->app_sha256, header.image_sha256, sizeof(bundle->app_sha256));
    memcpy(bundle_bytes + bootloader_offset, bootloader_image, sizeof(bootloader_image));
    memcpy(bundle_bytes + app_offset, &header, sizeof(header));
    memcpy(bundle_bytes + app_offset + sizeof(header), payload, sizeof(payload));
    bundle->header_crc32 = launchcore_bundle_header_crc(bundle);

    assert(launchcore_bundle_update_begin(bundle_size) == LAUNCHCORE_OK);
    for (uint32_t offset = 0u; offset < bundle_size; offset += 83u) {
        uint32_t len = bundle_size - offset;
        if (len > 83u) len = 83u;
        assert(launchcore_bundle_update_write(bundle_bytes + offset, len) == LAUNCHCORE_OK);
    }
    assert(launchcore_bundle_update_finish() == LAUNCHCORE_OK);
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING) != 0u);
    assert(!launchcore_process_staged_bundle(&md));
    assert(bootloader_flash_count == 1u);

    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP) != 0u);
    assert(launchcore_process_staged_bundle(&md));
    assert((md.flags & LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING) != 0u);
    assert(launchcore_install_staged_application(&md) == LAUNCHCORE_APP_INSTALL_READY);
    persisted = 0u;
    persisted_size = sizeof(persisted);
    assert(launchcore_persist_get(1u, &persisted, &persisted_size) == LAUNCHCORE_PERSIST_OK);
    assert(persisted == persistent_value);

    memset(bundle_bytes, 0, bundle_size);
    bundle = (launchcore_bundle_header_t *)bundle_bytes;
    bundle->magic = LAUNCHCORE_BUNDLE_MAGIC;
    bundle->version = LAUNCHCORE_BUNDLE_VERSION;
    bundle->header_size = sizeof(*bundle);
    bundle->total_size = bootloader_offset + sizeof(bootloader_image);
    bundle->bootloader_offset = bootloader_offset;
    bundle->bootloader_size = sizeof(bootloader_image);
    bundle->bootloader_version = 2u;
    bundle->bootloader_crc32 = launchcore_crc32(bootloader_image, sizeof(bootloader_image));
    launchcore_sha256(bootloader_image, sizeof(bootloader_image), bundle->bootloader_sha256);
    bundle->bootloader_signature[0] = 0xA5u;
    bundle->bootloader_signature_size = 1u;
    memcpy(bundle_bytes + bootloader_offset, bootloader_image, sizeof(bootloader_image));
    bundle->header_crc32 = launchcore_bundle_header_crc(bundle);
    assert(launchcore_bundle_update_begin(bundle->total_size) == LAUNCHCORE_OK);
    assert(launchcore_bundle_update_write(bundle_bytes, bundle->total_size) == LAUNCHCORE_OK);
    assert(launchcore_bundle_update_finish() == LAUNCHCORE_OK);
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert(!launchcore_process_staged_bundle(&md));
    assert(bootloader_flash_count == 2u);
    assert(launchcore_metadata_read_latest(&md) == LAUNCHCORE_METADATA_OK);
    assert(launchcore_process_staged_bundle(&md));
    assert(md.update_kind == LAUNCHCORE_UPDATE_KIND_BOOTLOADER);
    assert(md.update_result == LAUNCHCORE_UPDATE_RESULT_SUCCESS);
    return 0;
}
