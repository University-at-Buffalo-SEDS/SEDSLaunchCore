#include "launchcore/storage.h"

static const launchcore_storage_layout_t layout = {
    .slot_a_base = 0x70000000u,
    .slot_a_size = 0x00400000u,
    .slot_b_base = 0x70400000u,
    .slot_b_size = 0x00400000u,
    .metadata0_base = 0x70F00000u,
    .metadata1_base = 0x70F10000u,
    .metadata_size = 0x00010000u,
    .supports_xip = true,
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
