// MIDI Analog Filter "alpha" board (Pribore prototype): pins and peripherals.
// Pin map is taken from MIDI_Analog_Filter_alpha_SCH.pdf, see docs/HARDWARE.md.
//
// Works on any of the MCUs the schematic allows on DD1:
// STM32F205RB, STM32F405RG, STM32F446RE (same pinout, same peripherals used).
// The binary is built for Cortex-M3 so it runs on all of them.
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "clock.h"
#include "stm32f4xx.h"

// ---- Pins -----------------------------------------------------------------
// PA2  USART2_TX   -> J19 test point (future DIN MIDI OUT)
// PA3  USART2_RX   <- DIN MIDI IN (6N137 optocoupler + buffers)
// PA4  DAC1        -> U1.1 (x3.06) -> J1 "CV" output 0..10 V
// PA5  DAC2        -> U1.2 (x3.06) -> CV_FREQ_CTRL -> filter cutoff
// PA6  GATE1_3v3   -> R67 -> Q18 (N-MOSFET, open drain, 10k to +5V) -> GATE_OUT
//                     MOSFET inverts: PA6 high = GATE low
// PA9  USART1_TX   -> J22 pin 1 (debug UART, 115200 8N1)
// PA10 USART1_RX   <- J22 pin 2
// PA13 SWDIO, PA14 SWCLK -> J13 "SWD" (1=+3V3 2=SWCLK 3=GND 4=SWDIO)
// PB8  CLOCK_3v3   -> J31 test point, active low like GATE1_3v3
// PB14 OTG_HS_DM, PB15 OTG_HS_DP -> USB-B connector (internal FS PHY)
// PC0  BACKLIGTH_LED -> J17 test point
// PC1  MIDI_NOTEON_LED -> R66 -> Q17 -> NOTEON LED (active high)
// PC3  MIDI_LEARN  <- LEARN button S1, 10k pull-up, pressed = low
// Y1   HSE crystal (frequency auto-detected at boot)

typedef enum {
  CHIP_UNKNOWN = 0,
  CHIP_F205,   // STM32F2xx
  CHIP_F405,   // STM32F405/407/415/417
  CHIP_F446,
} chip_t;

typedef struct {
  chip_t chip;
  uint16_t dev_id;     // DBGMCU IDCODE[11:0]
  uint16_t rev_id;
  uint32_t hse_hz;     // measured crystal frequency, 0 = no crystal (running on HSI)
  uint32_t reset_flags;
} board_info_t;

extern board_info_t g_board;

void board_init(void);
const char* board_chip_name(void);

bool board_button_pressed(void);

// DIN MIDI (USART2)
bool midi_uart_read(uint8_t* b);
void midi_uart_write(uint8_t b); // drops bytes if the TX buffer is full
bool midi_uart_try_write(uint8_t b); // false if the TX buffer is full
uint32_t midi_uart_errors(void);

// Debug UART (USART1 on J22)
bool debug_uart_read(uint8_t* b);
void debug_uart_write(const uint8_t* p, uint32_t n);

void board_watchdog_init(void);
void board_watchdog_feed(void);
void board_reboot(void);
void board_reboot_to_bootloader(void); // USB DFU mode for firmware updates

#endif
