#include "launchcore/platform.h"
#include "launchcore/storage.h"

extern const launchcore_storage_driver_t launchcore_board_storage_driver;

void platform_early_init(void) { launchcore_storage_set_driver(&launchcore_board_storage_driver); }
void platform_clock_init(void) {}
void platform_external_ram_init(void) {}
void platform_external_flash_init(void) {}
void platform_deinit_before_jump(void) {}
void platform_system_reset(void) {}
uint32_t platform_get_reset_reason(void) { return 0u; }
void platform_feed_watchdog(void) {}
bool platform_validate_app_vector(uint32_t vector_table_addr, uint32_t stack_pointer, uint32_t reset_handler)
{
    return vector_table_addr != 0u && stack_pointer != 0u && (reset_handler & 1u) != 0u;
}
