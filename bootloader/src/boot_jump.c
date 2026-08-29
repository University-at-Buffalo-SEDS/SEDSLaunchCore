#include "launchcore/boot.h"
#include "launchcore/cortex_m.h"
#include "launchcore/platform.h"

#if defined(__arm__) || defined(__thumb__)
typedef void (*launchcore_entry_t)(void);

void launchcore_jump_to_image(uint32_t vector_table_addr)
{
    uint32_t app_sp = *(volatile uint32_t *)(vector_table_addr + 0u);
    uint32_t app_pc = *(volatile uint32_t *)(vector_table_addr + 4u);
    if (!platform_validate_app_vector(vector_table_addr, app_sp, app_pc)) return;

    launchcore_cortex_disable_irq();
    LAUNCHCORE_SYSTICK->CTRL = 0;
    LAUNCHCORE_SYSTICK->LOAD = 0;
    LAUNCHCORE_SYSTICK->VAL = 0;
    for (uint32_t i = 0; i < 8u; i++) {
        LAUNCHCORE_NVIC_ICER[i] = 0xFFFFFFFFu;
        LAUNCHCORE_NVIC_ICPR[i] = 0xFFFFFFFFu;
    }
    platform_deinit_before_jump();
    LAUNCHCORE_SCB->VTOR = vector_table_addr;
    launchcore_cortex_set_msp(app_sp);
    launchcore_cortex_dsb();
    launchcore_cortex_isb();
    /* Reset entry normally starts with PRIMASK clear. Restore that contract
     * after the bootloader's critical handoff section so application timer and
     * peripheral interrupts can run immediately. */
    launchcore_cortex_enable_irq();
    ((launchcore_entry_t)app_pc)();
}
#else
void launchcore_jump_to_image(uint32_t vector_table_addr)
{
    (void)vector_table_addr;
}
#endif
