// Hardware abstraction used by the MIDI engine.
// Firmware implements it in board.c, host tests implement it with stubs,
// so the MIDI logic can be tested on a PC without the board.
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stdint.h>

#define DAC_MAX 4095u

// Analog outputs of the MIDI Analog Filter alpha board
enum {
  CV_OUT_JACK = 0,   // STM32 DAC1 (PA4) -> U1.1 x3.06 -> J1 "CV" output, 0..10 V
  CV_OUT_FILTER = 1, // STM32 DAC2 (PA5) -> U1.2 x3.06 -> CV_FREQ_CTRL (filter cutoff)
  CV_OUT_COUNT
};

uint32_t hal_millis(void);

void hal_dac_write(uint8_t ch, uint16_t code);
void hal_gate_write(bool on);   // logical level on the GATE jack
void hal_clock_write(bool on);  // logical level on the CLOCK test point (J31)
void hal_led_note(bool on);     // NOTEON LED
void hal_led_aux(bool on);      // BACKLIGHT LED test point (J17)

// Persistent storage for settings (one small blob)
bool hal_storage_read(void* dst, uint32_t len);
bool hal_storage_write(const void* src, uint32_t len);

#endif
