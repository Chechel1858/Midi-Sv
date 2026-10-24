// User settings, kept in flash (sector 2, see common/memory_map.h) with a CRC.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_MAGIC   0x4D435631u // "MCV1"
#define SETTINGS_VERSION 1u

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

  uint32_t crc;
} settings_t;

extern settings_t g_settings;

void settings_defaults(settings_t* s);
bool settings_load(settings_t* s);   // false -> defaults were applied
bool settings_save(const settings_t* s);
uint32_t settings_crc(const settings_t* s);

#endif
