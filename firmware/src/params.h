// Table of user parameters: one place that knows names, SysEx ids, ranges
// and where each value lives in settings_t. Used by the console ("p"),
// the SysEx configuration protocol and the settings validation.
#ifndef PARAMS_H
#define PARAMS_H

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

#define PARAM_READONLY 0x01
#define PARAM_CAL      0x02 // calibration: SysEx needs CAL_UNLOCK first

typedef enum {
  PT_U8 = 0,
  PT_U16,
  PT_I16,
  PT_CHANNEL, // stored 0..15 / CHANNEL_OMNI, shown as 1..16 / 0 = omni
} param_type_t;

typedef struct {
  uint8_t id;               // SysEx parameter id
  const char* name;         // console name
  uint16_t offset;          // offsetof(settings_t, ...)
  uint8_t type;             // param_type_t
  uint8_t flags;
  int32_t min, max;
  const char* const* names; // value names for enumerations (index = value), or NULL
  const char* unit;         // shown by the console, may be ""
} param_t;

unsigned param_count(void);
const param_t* param_at(unsigned index);
const param_t* param_by_id(uint8_t id);
const param_t* param_by_name(const char* name);

int32_t param_get(const settings_t* s, const param_t* p);
// false if the value is out of range (nothing is changed then)
bool param_set(settings_t* s, const param_t* p, int32_t value);

// Accepts a number, an enumeration name, on/off, or "omni" for the channel
bool param_parse(const param_t* p, const char* text, int32_t* value);
// Name of an enumeration value, NULL for plain numbers
const char* param_value_name(const param_t* p, int32_t value);

#endif
