/*
 * Startup for the updater (Cortex-M4, STM32L443): vector table and Reset_Handler.
 *
 * No C library start-up: point VTOR at this image, copy .data (which holds the
 * RAM-resident flash code, .RamFunc), zero .bss, call main(). The secure bootloader
 * has already set up the clocks. Interrupts stay masked for the whole run; every
 * exception ends in the FAIL pattern rather than a silent lock-up.
 */
#include <stdint.h>

#include "stm32l4xx.h"
#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

void Reset_Handler(void) __attribute__((noreturn));
void Default_Handler(void) __attribute__((noreturn));

/* 16 core entries + 83 STM32L443 interrupts (CRS_IRQn = 82). */
#define VECTOR_COUNT 99U
typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[VECTOR_COUNT] = {
    (vector_t)(uintptr_t)&_estack,
    Reset_Handler,
    [2 ... VECTOR_COUNT - 1U] = Default_Handler,
};

void Reset_Handler(void)
{
    __disable_irq();
    SCB->VTOR = (uint32_t)(uintptr_t)g_vectors;
    __DSB();
    __ISB();
    const uint32_t *src = &_sidata;
    /* The bounds are linker symbols: one region each, so comparing them is defined. */
    // cppcheck-suppress comparePointers
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }
    // cppcheck-suppress comparePointers
    for (uint32_t *dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0U;
    }
    (void)main();
    for (;;) { }
}

void Default_Handler(void)
{
    for (;;) {
        board_iwdg_refresh();
        board_leds_set(LED_RUN);
    }
}
