#include "board_config.h"
#include "launchcore/platform.h"
#include "launchcore/storage.h"

extern const launchcore_storage_driver_t launchcore_board_storage_driver;

void platform_early_init(void) { launchcore_storage_set_driver(&launchcore_board_storage_driver); }
void platform_clock_init(void) {}
void platform_external_ram_init(void) {}
void platform_external_flash_init(void) {}
void platform_deinit_before_jump(void) {}
bool platform_recovery_requested(void) { return false; }
void platform_system_reset(void) {}
uint32_t platform_get_reset_reason(void) { return 0u; }
void platform_feed_watchdog(void) {}

bool platform_validate_app_vector(uint32_t vector_table_addr, uint32_t stack_pointer, uint32_t reset_handler)
{
    bool vector_ok = vector_table_addr >= 0x08020100u && vector_table_addr < 0x081E0000u;
    bool stack_ok = stack_pointer >= LAUNCHCORE_INTERNAL_SRAM_BASE &&
                    stack_pointer < LAUNCHCORE_INTERNAL_SRAM_BASE + LAUNCHCORE_INTERNAL_SRAM_SIZE;
    bool reset_ok = reset_handler >= 0x08020100u && reset_handler < 0x081E0000u && (reset_handler & 1u) != 0u;
    return vector_ok && stack_ok && reset_ok;
}

bool platform_verify_bootloader_signature(const uint8_t hash[32], const uint8_t *signature, uint32_t signature_len)
{
    (void)hash;
    (void)signature;
    (void)signature_len;
    return false;
}

bool platform_flash_bootloader_image(uint32_t source_addr, uint32_t image_size)
{
    (void)source_addr;
    (void)image_size;
    return false;
}
