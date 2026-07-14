#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "launchcore/metadata.h"

typedef enum {
    LAUNCHCORE_STORAGE_OK = 0,
    LAUNCHCORE_STORAGE_ERR_INIT,
    LAUNCHCORE_STORAGE_ERR_RANGE,
    LAUNCHCORE_STORAGE_ERR_ERASE,
    LAUNCHCORE_STORAGE_ERR_WRITE,
    LAUNCHCORE_STORAGE_ERR_READ,
    LAUNCHCORE_STORAGE_ERR_VERIFY,
} launchcore_storage_status_t;

typedef struct {
    uint32_t slot_a_base;
    uint32_t slot_a_size;
    uint32_t slot_b_base;
    uint32_t slot_b_size;
    uint32_t metadata0_base;
    uint32_t metadata1_base;
    uint32_t metadata_size;
    uint32_t bootloader_base;
    uint32_t bootloader_size;
    uint32_t bootloader_staging_base;
    uint32_t bootloader_staging_size;
    uint32_t app_staging_base;
    uint32_t app_staging_size;
    uint32_t persistent_data_base;
    uint32_t persistent_data_size;
    uint32_t persistent_data_erase_size;
    uint32_t slot_erase_size;
    bool slot_b_is_delta;
    bool supports_xip;
} launchcore_storage_layout_t;

typedef struct {
    launchcore_storage_status_t (*init)(void);
    launchcore_storage_status_t (*erase)(uint32_t addr, uint32_t len);
    launchcore_storage_status_t (*write)(uint32_t addr, const void *data, uint32_t len);
    launchcore_storage_status_t (*read)(uint32_t addr, void *data, uint32_t len);
    launchcore_storage_status_t (*enable_memory_mapped)(void);
    bool (*is_executable_addr)(uint32_t addr);
    const launchcore_storage_layout_t *(*layout)(void);
} launchcore_storage_driver_t;

void launchcore_storage_set_driver(const launchcore_storage_driver_t *driver);
const launchcore_storage_driver_t *launchcore_storage_driver(void);
launchcore_storage_status_t launchcore_storage_init(void);
uint32_t launchcore_slot_base(launchcore_slot_id_t slot);
uint32_t launchcore_slot_size(launchcore_slot_id_t slot);
bool launchcore_storage_range_preserves_persistent(uint32_t addr, uint32_t len);
