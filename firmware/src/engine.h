// MIDI -> CV/Gate logic: mono note stack, CC/velocity mapping, gate,
// MIDI clock output, LEARN button. Hardware access goes through hal.h.
// Everything here runs from the main loop (no interrupts), so no locking.
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "midi_parser.h"
#include "settings.h"

typedef enum {
  LEARN_NONE = 0,
  LEARN_CC_FILTER, // next CC is assigned to the filter cutoff output
  LEARN_CC_JACK,   // next CC is assigned to the CV jack
  LEARN_CHANNEL,   // next Note On selects the MIDI channel
} learn_mode_t;

typedef struct {
  bool gate;
  int8_t note;          // current note, -1 = none
  uint8_t velocity;
  uint16_t bend;        // 0..16383
  uint8_t aftertouch;
  bool sustain;
  uint8_t stack_len;
  learn_mode_t learn;
  uint16_t dac[2];      // codes currently written
  int32_t override_mv[2]; // <0 = no override
  uint32_t msg_count;
  uint32_t clock_count;
  int32_t mod_mv;       // envelope + LFO + key tracking added to the filter output
  uint8_t env_stage;    // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
} engine_status_t;

void engine_init(void);

// Re-applies g_settings (call after changing them)
void engine_settings_changed(void);

void engine_midi(const midi_msg_t* m);

// Call once per millisecond
void engine_tick_1ms(void);

// Raw button level (true = pressed), call once per millisecond
void engine_button(bool pressed);

// Forces an output to a voltage for testing/calibration; mv < 0 releases it
void engine_override(uint8_t ch, int32_t mv);

// Converts a voltage to a DAC code using the output calibration
uint16_t engine_mv_to_code(uint8_t ch, int32_t mv);

void engine_learn(learn_mode_t mode);
void engine_panic(void);

const engine_status_t* engine_status(void);

// True once when settings changed and should be written to flash
bool engine_take_save_request(void);

#endif
