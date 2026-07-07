#include "launchcore/metadata.h"
#include "launchcore/storage.h"
#include <string.h>

#define MOCK_STORAGE_SIZE (16u * 1024u * 1024u)

static uint8_t mock_storage[MOCK_STORAGE_SIZE];
static const launchcore_storage_driver_t *active_driver;

static const launchcore_storage_layout_t mock_layout = {
    .slot_a_base = 0x00000000u,
    .slot_a_size = 0x00400000u,
    .slot_b_base = 0x00400000u,
    .slot_b_size = 0x00400000u,
    .metadata0_base = 0x00F00000u,
    .metadata1_base = 0x00F10000u,
    .metadata_size = 0x00001000u,
    .supports_xip = true,
};

static bool range_ok(uint32_t addr, uint32_t len)
{
    return addr <= MOCK_STORAGE_SIZE && len <= MOCK_STORAGE_SIZE - addr;
}

static launchcore_storage_status_t mock_init(void)
{
    static bool initialized;
    if (!initialized) {
        memset(mock_storage, 0xFF, sizeof(mock_storage));
        initialized = true;
    }
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t mock_erase(uint32_t addr, uint32_t len)
{
    if (!range_ok(addr, len)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memset(&mock_storage[addr], 0xFF, len);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t mock_write(uint32_t addr, const void *data, uint32_t len)
{
    if (data == NULL || !range_ok(addr, len)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memcpy(&mock_storage[addr], data, len);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t mock_read(uint32_t addr, void *data, uint32_t len)
{
    if (data == NULL || !range_ok(addr, len)) return LAUNCHCORE_STORAGE_ERR_RANGE;
    memcpy(data, &mock_storage[addr], len);
    return LAUNCHCORE_STORAGE_OK;
}

static launchcore_storage_status_t mock_enable_memory_mapped(void) { return LAUNCHCORE_STORAGE_OK; }
static bool mock_is_executable_addr(uint32_t addr) { return range_ok(addr, 4u); }
static const launchcore_storage_layout_t *mock_layout_fn(void) { return &mock_layout; }

static const launchcore_storage_driver_t mock_driver = {
    .init = mock_init,
    .erase = mock_erase,
    .write = mock_write,
    .read = mock_read,
    .enable_memory_mapped = mock_enable_memory_mapped,
    .is_executable_addr = mock_is_executable_addr,
    .layout = mock_layout_fn,
};

void launchcore_storage_set_driver(const launchcore_storage_driver_t *driver)
{
    active_driver = driver;
}

const launchcore_storage_driver_t *launchcore_storage_driver(void)
{
    return active_driver != NULL ? active_driver : &mock_driver;
}

launchcore_storage_status_t launchcore_storage_init(void)
{
    return launchcore_storage_driver()->init();
}

uint32_t launchcore_slot_base(launchcore_slot_id_t slot)
{
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    return slot == LAUNCHCORE_SLOT_A ? layout->slot_a_base : layout->slot_b_base;
}

uint32_t launchcore_slot_size(launchcore_slot_id_t slot)
{
    const launchcore_storage_layout_t *layout = launchcore_storage_driver()->layout();
    return slot == LAUNCHCORE_SLOT_A ? layout->slot_a_size : layout->slot_b_size;
}
