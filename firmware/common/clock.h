// System clock (120 MHz, USB 48 MHz) with HSE crystal auto-detection,
// 1 ms SysTick. Shared by the bootloader and the application.
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#define SYSCLK_HZ 120000000u // safe for F205 (max 120 MHz) and the F4 parts
#define APB1_HZ   (SYSCLK_HZ / 4)
#define APB2_HZ   (SYSCLK_HZ / 2)

extern volatile uint32_t g_ms;

// Must be called while the core still runs on HSI (right after reset).
// Returns the measured crystal frequency in Hz, 0 = no crystal (PLL on HSI).
uint32_t clock_init(void);

void systick_init(void);

#endif
