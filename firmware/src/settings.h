// User settings, kept in flash (sector 2, see common/memory_map.h) with a CRC.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_MAGIC   0x4D435631u // "MCV1"
#define SETTINGS_VERSION 2u

#define CHANNEL_OMNI 0xFFu

// What drives an analog output
typedef enum {
  SRC_OFF = 0,     // fixed 0 V
  SRC_PITCH,       // last note, 1 V/oct + pitch bend
  SRC_VELOCITY,    // velocity of the last Note On
  SRC_CC,          // controller number cc
  SRC_AFTERTOUCH,  // channel pressure
  SRC_BEND,        // pitch bend, centre = half range
  SRC_COUNT
} cv_source_t;

typedef enum {
  LFO_SINE = 0,
  LFO_TRIANGLE,
  LFO_SQUARE,
  LFO_SAW_UP,
  LFO_SAW_DOWN,
  LFO_RANDOM,      // sample & hold, a new random value every cycle
  LFO_SHAPE_COUNT
} lfo_shape_t;

typedef enum {
  GLIDE_RATE = 0,  // glide_ms = time per octave (constant rate, spec default)
  GLIDE_TIME,      // glide_ms = time of the whole transition (constant time)
} glide_mode_t;

// Modulation added on top of the filter output's source (CC74 by default):
// envelope (ADSR) on every note, LFO and key tracking.
typedef struct {
  uint16_t attack_ms;  // 0..10000
  uint16_t decay_ms;   // 0..10000
  uint16_t release_ms; // 0..10000
  uint8_t sustain;     // 0..127
  uint8_t env_vel;     // 0..100 %: how much velocity scales the envelope
  int16_t env_mv;      // envelope depth, -10000..10000 mV (negative = closes the filter)
  uint8_t env_retrig;  // 1 = restart the envelope on legato notes too
  uint8_t lfo_shape;   // lfo_shape_t
  uint16_t lfo_rate;   // Hz x100, 1..3000 (0.01..30 Hz)
  uint16_t lfo_mv;     // LFO depth (peak), 0..10000 mV
  uint8_t lfo_sync;    // 1 = restart the LFO on every new note
  uint8_t lfo_wheel;   // 1 = mod wheel (CC1) scales the LFO depth
  uint8_t keytrack;    // 0..100 %: 100 = filter follows the note at 1 V/oct
  uint8_t reserved[3];
} filter_mod_t;

typedef struct {
  uint8_t source;      // cv_source_t
  uint8_t cc;          // controller number for SRC_CC
  uint8_t smooth_ms;   // one-pole smoothing time constant, 0 = off (never for pitch)
  uint8_t reserved;
  uint16_t range_mv;   // output voltage at value 127 (CC/velocity/aftertouch/bend)
  int16_t cal_zero;    // DAC code that gives 0.000 V on the output
  uint16_t cal_gain;   // DAC codes per volt x100 (nominal 40545 for x3.06 stage)
  uint16_t reserved2;
} cv_cfg_t;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size;

  uint8_t channel;        // 0..15, CHANNEL_OMNI = all channels
  uint8_t zero_note;      // MIDI note that gives 0 V on a pitch output
  uint8_t bend_range;     // semitones, 0..24
  uint8_t gate_retrigger; // 1 = short gate drop on legato notes
  uint8_t clock_div;      // MIDI clocks (24 PPQN) per clock pulse
  uint8_t clock_pulse_ms; // clock pulse width
  uint8_t gate_invert;    // 1 = flip GATE polarity (in case of hardware differences)
  uint8_t reserved;

  cv_cfg_t cv[2];         // [CV_OUT_JACK], [CV_OUT_FILTER]

  filter_mod_t fmod;      // envelope / LFO / key tracking on the filter output

  uint16_t glide_ms;      // portamento on pitch outputs, 0 = off
  uint8_t glide_mode;     // glide_mode_t
  uint8_t glide_legato;   // 1 = glide only between overlapping notes (spec default)

  uint32_t crc;
} settings_t;

extern settings_t g_settings;

void settings_defaults(settings_t* s);
bool settings_load(settings_t* s);   // false -> defaults were applied
bool settings_save(const settings_t* s);
uint32_t settings_crc(const settings_t* s);

#endif
