#include "launchcore/storage.h"

static const launchcore_storage_layout_t layout = {
    .slot_a_base = 0x08020000u,
    .slot_a_size = 0x00060000u,
    .slot_b_base = 0x08080000u,
    .slot_b_size = 0x00060000u,
    .metadata0_base = 0x081E0000u,
    .metadata1_base = 0x081E2000u,
    .metadata_size = 0x00002000u,
    .bootloader_base = 0x08000000u,
    .bootloader_size = 0x00020000u,
    .bootloader_staging_base = 0x08140000u,
    .bootloader_staging_size = 0x00080000u,
    .app_staging_base = 0u,
    .app_staging_size = 0u,
    .supports_xip = false,
};

static launchcore_storage_status_t ok(void) { return LAUNCHCORE_STORAGE_OK; }
static launchcore_storage_status_t erase_stub(uint32_t addr, uint32_t len) { (void)addr; (void)len; return LAUNCHCORE_STORAGE_ERR_ERASE; }
static launchcore_storage_status_t write_stub(uint32_t addr, const void *data, uint32_t len) { (void)addr; (void)data; (void)len; return LAUNCHCORE_STORAGE_ERR_WRITE; }
static launchcore_storage_status_t read_stub(uint32_t addr, void *data, uint32_t len) { (void)addr; (void)data; (void)len; return LAUNCHCORE_STORAGE_ERR_READ; }
static bool executable(uint32_t addr) { return addr >= layout.slot_a_base && addr < layout.metadata0_base; }
static const launchcore_storage_layout_t *layout_fn(void) { return &layout; }

const launchcore_storage_driver_t launchcore_board_storage_driver = {
    .init = ok,
    .erase = erase_stub,
    .write = write_stub,
    .read = read_stub,
    .enable_memory_mapped = ok,
    .is_executable_addr = executable,
    .layout = layout_fn,
};
