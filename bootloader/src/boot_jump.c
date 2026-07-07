#include "launchcore/boot.h"
#include "launchcore/platform.h"

#if defined(__arm__) || defined(__thumb__)
#include "stm32xxxx.h"
typedef void (*launchcore_entry_t)(void);

void launchcore_jump_to_image(uint32_t vector_table_addr)
{
    uint32_t app_sp = *(volatile uint32_t *)(vector_table_addr + 0u);
    uint32_t app_pc = *(volatile uint32_t *)(vector_table_addr + 4u);
    if (!platform_validate_app_vector(vector_table_addr, app_sp, app_pc)) return;

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (uint32_t i = 0; i < 8u; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    platform_deinit_before_jump();
    SCB->VTOR = vector_table_addr;
    __set_MSP(app_sp);
    __DSB();
    __ISB();
    ((launchcore_entry_t)app_pc)();
}
#else
void launchcore_jump_to_image(uint32_t vector_table_addr)
{
    (void)vector_table_addr;
}
#endif
