#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "hal.h"
#include "params.h"

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

  // Filter modulation: shapes ready to use, depths at 0 (off) so the filter
  // behaves exactly like a plain CC74 output until a depth is turned up
  s->fmod.attack_ms = 5;
  s->fmod.decay_ms = 300;
  s->fmod.sustain = 64;
  s->fmod.release_ms = 300;
  s->fmod.env_vel = 50;
  s->fmod.env_mv = 0;
  s->fmod.env_retrig = 0;
  s->fmod.lfo_shape = LFO_SINE;
  s->fmod.lfo_rate = 200; // 2 Hz
  s->fmod.lfo_mv = 0;
  s->fmod.lfo_sync = 0;
  s->fmod.lfo_wheel = 0;
  s->fmod.keytrack = 0;

  // Portamento: off; when enabled, constant rate and legato only (spec defaults)
  s->glide_ms = 0;
  s->glide_mode = GLIDE_RATE;
  s->glide_legato = 1;
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
  // Every field must be inside the range the parameter table allows
  for (unsigned i = 0; i < param_count(); i++) {
    const param_t* p = param_at(i);
    if (p->flags & PARAM_READONLY) continue;
    int32_t v = param_get(s, p);
    if (v < p->min || v > p->max) return false;
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
