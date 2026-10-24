#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "hal.h"

settings_t g_settings;

// DAC 0..3.3 V through the non-inverting stage 1 + 68k/33k = 3.0606
// -> 0..10.10 V, i.e. 4095 / 10.10 = 405.45 codes per volt.
#define NOMINAL_GAIN_X100 40545u

void settings_defaults(settings_t* s) {
  memset(s, 0, sizeof(*s));
  s->magic = SETTINGS_MAGIC;
  s->version = SETTINGS_VERSION;
  s->size = sizeof(*s);

  s->channel = CHANNEL_OMNI;
  s->zero_note = 36; // C2 = 0 V, as in the MIDI-CV/Gate spec
  s->bend_range = 2;
  s->gate_retrigger = 0;
  s->clock_div = 6; // 4 pulses per quarter note (16ths)
  s->clock_pulse_ms = 5;
  s->gate_invert = 0;

  for (int i = 0; i < 2; i++) {
    s->cv[i].range_mv = 10000;
    s->cv[i].cal_zero = 0;
    s->cv[i].cal_gain = NOMINAL_GAIN_X100;
  }

  // J1 "CV" jack: pitch 1 V/oct
  s->cv[0].source = SRC_PITCH;
  s->cv[0].cc = 1;
  s->cv[0].smooth_ms = 0;

  // Filter cutoff: CC74 (the usual "brightness/cutoff" controller)
  s->cv[1].source = SRC_CC;
  s->cv[1].cc = 74;
  s->cv[1].smooth_ms = 5;
}

// CRC-32 (IEEE), bitwise: settings are tiny, speed does not matter
static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

uint32_t settings_crc(const settings_t* s) {
  return crc32((const uint8_t*) s, offsetof(settings_t, crc));
}

static bool settings_valid(const settings_t* s) {
  if (s->magic != SETTINGS_MAGIC || s->version != SETTINGS_VERSION || s->size != sizeof(*s)) return false;
  if (s->crc != settings_crc(s)) return false;
  if (s->channel > 15 && s->channel != CHANNEL_OMNI) return false;
  if (s->zero_note > 127 || s->bend_range > 24 || s->clock_div == 0 || s->clock_div > 96) return false;
  for (int i = 0; i < 2; i++) {
    if (s->cv[i].source >= SRC_COUNT || s->cv[i].cc > 127 || s->cv[i].cal_gain == 0) return false;
  }
  return true;
}

bool settings_load(settings_t* s) {
  settings_t tmp;
  if (hal_storage_read(&tmp, sizeof(tmp)) && settings_valid(&tmp)) {
    *s = tmp;
    return true;
  }
  settings_defaults(s);
  return false;
}

bool settings_save(const settings_t* s) {
  settings_t tmp = *s;
  tmp.magic = SETTINGS_MAGIC;
  tmp.version = SETTINGS_VERSION;
  tmp.size = sizeof(tmp);
  tmp.crc = settings_crc(&tmp);
  return hal_storage_write(&tmp, sizeof(tmp));
}
