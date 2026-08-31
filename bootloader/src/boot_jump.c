#include "launchcore/boot.h"
#include "launchcore/cortex_m.h"
#include "launchcore/platform.h"

#if defined(__arm__) || defined(__thumb__)

/* This must be a separate naked, non-returning routine. Changing MSP inside a
 * normal C function leaves its compiler-generated epilogue using the
 * application's pristine stack. On a full-descending Cortex-M stack that can
 * make the epilogue read immediately above SRAM before the reset handler is
 * reached. Keep both the stack switch and branch in one assembly-only tail. */
__attribute__((naked, noreturn))
static void launchcore_cortex_start_application(uint32_t app_sp, uint32_t app_pc)
{
    __asm volatile(
        "msr msp, r0\n"
        "dsb 0xF\n"
        "isb 0xF\n"
        "cpsie i\n"
        "bx r1\n");
}

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
    /* Reset entry normally starts with PRIMASK clear. Restore that contract
     * after the bootloader's critical handoff section so application timer and
     * peripheral interrupts can run immediately. The assembly tail changes
     * MSP only after all C stack access has ended. */
    launchcore_cortex_start_application(app_sp, app_pc);
}
#else
void launchcore_jump_to_image(uint32_t vector_table_addr)
{
    (void)vector_table_addr;
}
#endif
