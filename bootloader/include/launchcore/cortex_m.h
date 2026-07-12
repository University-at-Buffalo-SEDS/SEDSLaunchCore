#pragma once

#include <stdint.h>

#if defined(__arm__) || defined(__thumb__)

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} launchcore_systick_t;

typedef struct {
    volatile uint32_t CPUID;
    volatile uint32_t ICSR;
    volatile uint32_t VTOR;
} launchcore_scb_t;

#define LAUNCHCORE_SYSTICK ((launchcore_systick_t *)0xE000E010u)
#define LAUNCHCORE_NVIC_ICER ((volatile uint32_t *)0xE000E180u)
#define LAUNCHCORE_NVIC_ICPR ((volatile uint32_t *)0xE000E280u)
#define LAUNCHCORE_SCB ((launchcore_scb_t *)0xE000ED00u)

static inline void launchcore_cortex_disable_irq(void)
{
    __asm volatile("cpsid i" ::: "memory");
}

static inline void launchcore_cortex_set_msp(uint32_t value)
{
    __asm volatile("msr msp, %0" : : "r"(value) : "memory");
}

static inline void launchcore_cortex_dsb(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
}

static inline void launchcore_cortex_isb(void)
{
    __asm volatile("isb 0xF" ::: "memory");
}

#endif
