#include "params.h"

#include <stddef.h>
#include <string.h>

static const char* const k_onoff[] = {"off", "on", NULL};
static const char* const k_sources[] = {"off", "pitch", "vel", "cc", "at", "bend", NULL};
static const char* const k_shapes[] = {"sine", "tri", "square", "sawup", "sawdown", "random", NULL};
static const char* const k_glide[] = {"rate", "time", NULL};

#define OFS(field) ((uint16_t) offsetof(settings_t, field))

static const param_t k_params[] = {
  // id  name            offset                   type        flags       min     max    names      unit
  {1,  "channel",      OFS(channel),            PT_CHANNEL, 0,          0,      16,    NULL,      "(0 = omni)"},
  {2,  "zero",         OFS(zero_note),          PT_U8,      0,          0,      127,   NULL,      "note"},
  {3,  "bend",         OFS(bend_range),         PT_U8,      0,          0,      24,    NULL,      "semitones"},
  {4,  "retrig",       OFS(gate_retrigger),     PT_U8,      0,          0,      1,     k_onoff,   ""},
  {5,  "clockdiv",     OFS(clock_div),          PT_U8,      0,          1,      96,    NULL,      "MIDI clocks"},
  {6,  "clockpulse",   OFS(clock_pulse_ms),     PT_U8,      0,          1,      50,    NULL,      "ms"},
  {7,  "gateinv",      OFS(gate_invert),        PT_U8,      0,          0,      1,     k_onoff,   ""},

  {10, "jack.src",     OFS(cv[0].source),       PT_U8,      0,          0,      SRC_COUNT - 1, k_sources, ""},
  {11, "jack.cc",      OFS(cv[0].cc),           PT_U8,      0,          0,      127,   NULL,      ""},
  {12, "jack.smooth",  OFS(cv[0].smooth_ms),    PT_U8,      0,          0,      100,   NULL,      "ms"},
  {13, "jack.range",   OFS(cv[0].range_mv),     PT_U16,     0,          100,    10500, NULL,      "mV"},
  {14, "jack.cal0",    OFS(cv[0].cal_zero),     PT_I16,     PARAM_CAL,  -2000,  2000,  NULL,      "codes"},
  {15, "jack.gain",    OFS(cv[0].cal_gain),     PT_U16,     PARAM_CAL,  20000,  65000, NULL,      "codes/V x100"},

  {20, "filter.src",   OFS(cv[1].source),       PT_U8,      0,          0,      SRC_COUNT - 1, k_sources, ""},
  {21, "filter.cc",    OFS(cv[1].cc),           PT_U8,      0,          0,      127,   NULL,      ""},
  {22, "filter.smooth",OFS(cv[1].smooth_ms),    PT_U8,      0,          0,      100,   NULL,      "ms"},
  {23, "filter.range", OFS(cv[1].range_mv),     PT_U16,     0,          100,    10500, NULL,      "mV"},
  {24, "filter.cal0",  OFS(cv[1].cal_zero),     PT_I16,     PARAM_CAL,  -2000,  2000,  NULL,      "codes"},
  {25, "filter.gain",  OFS(cv[1].cal_gain),     PT_U16,     PARAM_CAL,  20000,  65000, NULL,      "codes/V x100"},

  {30, "attack",       OFS(fmod.attack_ms),     PT_U16,     0,          0,      10000, NULL,      "ms"},
  {31, "decay",        OFS(fmod.decay_ms),      PT_U16,     0,          0,      10000, NULL,      "ms"},
  {32, "sustain",      OFS(fmod.sustain),       PT_U8,      0,          0,      127,   NULL,      "/127"},
  {33, "release",      OFS(fmod.release_ms),    PT_U16,     0,          0,      10000, NULL,      "ms"},
  {34, "env",          OFS(fmod.env_mv),        PT_I16,     0,          -10000, 10000, NULL,      "mV"},
  {35, "envvel",       OFS(fmod.env_vel),       PT_U8,      0,          0,      100,   NULL,      "%"},
  {36, "envretrig",    OFS(fmod.env_retrig),    PT_U8,      0,          0,      1,     k_onoff,   ""},
  {40, "lfoshape",     OFS(fmod.lfo_shape),     PT_U8,      0,          0,      LFO_SHAPE_COUNT - 1, k_shapes, ""},
  {41, "lforate",      OFS(fmod.lfo_rate),      PT_U16,     0,          1,      3000,  NULL,      "Hz x100"},
  {42, "lfo",          OFS(fmod.lfo_mv),        PT_U16,     0,          0,      10000, NULL,      "mV"},
  {43, "lfosync",      OFS(fmod.lfo_sync),      PT_U8,      0,          0,      1,     k_onoff,   ""},
  {44, "lfowheel",     OFS(fmod.lfo_wheel),     PT_U8,      0,          0,      1,     k_onoff,   ""},
  {45, "keytrack",     OFS(fmod.keytrack),      PT_U8,      0,          0,      100,   NULL,      "%"},

  {50, "glide",        OFS(glide_ms),           PT_U16,     0,          0,      10000, NULL,      "ms"},
  {51, "glidemode",    OFS(glide_mode),         PT_U8,      0,          0,      1,     k_glide,   ""},
  {52, "glidelegato",  OFS(glide_legato),       PT_U8,      0,          0,      1,     k_onoff,   ""},
};

unsigned param_count(void) { return sizeof(k_params) / sizeof(k_params[0]); }

const param_t* param_at(unsigned index) { return index < param_count() ? &k_params[index] : NULL; }

const param_t* param_by_id(uint8_t id) {
  for (unsigned i = 0; i < param_count(); i++) {
    if (k_params[i].id == id) return &k_params[i];
  }
  return NULL;
}

const param_t* param_by_name(const char* name) {
  for (unsigned i = 0; i < param_count(); i++) {
    if (!strcmp(k_params[i].name, name)) return &k_params[i];
  }
  return NULL;
}

int32_t param_get(const settings_t* s, const param_t* p) {
  const uint8_t* base = (const uint8_t*) s + p->offset;
  switch (p->type) {
    case PT_U8: return *base;
    case PT_U16: {
      uint16_t v;
      memcpy(&v, base, 2);
      return v;
    }
    case PT_I16: {
      int16_t v;
      memcpy(&v, base, 2);
      return v;
    }
    case PT_CHANNEL: return *base == CHANNEL_OMNI ? 0 : *base + 1;
    default: return 0;
  }
}

bool param_set(settings_t* s, const param_t* p, int32_t value) {
  if (p->flags & PARAM_READONLY) return false;
  if (value < p->min || value > p->max) return false;
  uint8_t* base = (uint8_t*) s + p->offset;
  switch (p->type) {
    case PT_U8: *base = (uint8_t) value; break;
    case PT_U16: {
      uint16_t v = (uint16_t) value;
      memcpy(base, &v, 2);
      break;
    }
    case PT_I16: {
      int16_t v = (int16_t) value;
      memcpy(base, &v, 2);
      break;
    }
    case PT_CHANNEL: *base = value == 0 ? CHANNEL_OMNI : (uint8_t) (value - 1); break;
    default: return false;
  }
  return true;
}

const char* param_value_name(const param_t* p, int32_t value) {
  if (p->type == PT_CHANNEL && value == 0) return "omni";
  if (!p->names || value < 0) return NULL;
  for (int32_t i = 0; p->names[i]; i++) {
    if (i == value) return p->names[i];
  }
  return NULL;
}

bool param_parse(const param_t* p, const char* text, int32_t* value) {
  if (!text || !*text) return false;
  if (p->type == PT_CHANNEL && !strcmp(text, "omni")) {
    *value = 0;
    return true;
  }
  if (p->names) {
    for (int32_t i = 0; p->names[i]; i++) {
      if (!strcmp(p->names[i], text)) {
        *value = i;
        return true;
      }
    }
  }
  const char* s = text;
  bool neg = *s == '-';
  if (neg) s++;
  if (!*s) return false;
  int32_t v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || v > 100000000) return false;
    v = v * 10 + (*s - '0');
  }
  *value = neg ? -v : v;
  return true;
}
