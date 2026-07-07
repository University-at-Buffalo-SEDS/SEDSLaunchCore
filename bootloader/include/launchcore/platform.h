#pragma once

#include <stdbool.h>
#include <stdint.h>

void platform_early_init(void);
void platform_clock_init(void);
void platform_external_ram_init(void);
void platform_external_flash_init(void);
void platform_deinit_before_jump(void);
bool platform_recovery_requested(void);
void platform_system_reset(void);
uint32_t platform_get_reset_reason(void);
void platform_feed_watchdog(void);
bool platform_validate_app_vector(uint32_t vector_table_addr, uint32_t stack_pointer, uint32_t reset_handler);
bool platform_verify_bootloader_signature(const uint8_t hash[32], const uint8_t *signature, uint32_t signature_len);
bool platform_flash_bootloader_image(uint32_t source_addr, uint32_t image_size);
