#include "engine.h"

#include <string.h>

#include "hal.h"

#define STACK_MAX 16

#define BUTTON_DEBOUNCE_MS  20
#define BUTTON_SHORT_MAX_MS 1000
#define BUTTON_CHANNEL_MS   3000  // hold: MIDI channel learn
#define BUTTON_OMNI_MS      8000  // hold: back to Omni
#define BUTTON_FACTORY_MS   15000 // hold: factory settings
#define LEARN_TIMEOUT_MS    10000
#define RETRIGGER_MS        2
#define CONFIRM_BLINK_MS    600

static engine_status_t st;

static uint8_t stack[STACK_MAX];  // held notes, last = most recent
static bool key_down[128];        // physically held keys (for sustain)

static uint8_t cc_values[128];
static int32_t smooth_q16[2];     // smoothed DAC code, Q16
static uint16_t target_code[2];

static uint32_t clock_ticks;      // MIDI clocks since Start
static uint32_t clock_off_at;     // ms timestamp when the pulse ends, 0 = idle
static uint32_t retrig_until;     // gate held low until this time
static uint32_t learn_started;
static uint32_t confirm_until;

static bool btn_raw, btn_state;
static uint16_t btn_stable_ms;
static uint32_t btn_held_ms;
static bool btn_long_fired, btn_omni_fired, btn_factory_fired;

static bool save_requested;

// ---------------------------------------------------------------------------
// Output helpers

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static uint16_t uv_to_code(uint8_t ch, int64_t uv) {
  const cv_cfg_t* c = &g_settings.cv[ch];
  // code = zero + gain[codes/V x100] * uV / (100 * 1e6), rounded
  int64_t num = (int64_t) c->cal_gain * uv;
  int64_t q = (num >= 0 ? num + 50000000 : num - 50000000) / 100000000;
  return (uint16_t) clamp_i32((int32_t) (c->cal_zero + q), 0, DAC_MAX);
}

uint16_t engine_mv_to_code(uint8_t ch, int32_t mv) {
  return uv_to_code(ch, (int64_t) mv * 1000);
}

static void dac_out(uint8_t ch, uint16_t code) {
  if (st.dac[ch] != code) {
    st.dac[ch] = code;
    hal_dac_write(ch, code);
  }
}

static void gate_out(bool on) {
  hal_gate_write(on != (g_settings.gate_invert != 0));
}

// Pitch in Q16 semitones relative to zero_note, including pitch bend
static int32_t pitch_q16(void) {
  int32_t semis = (int32_t) st.note - g_settings.zero_note;
  // bend: 8192 = centre, full deflection = bend_range semitones
  int32_t bend = ((int32_t) st.bend - 8192) * g_settings.bend_range * 8; // x65536/8192
  return semis * 65536 + bend;
}

static uint16_t compute_target(uint8_t ch) {
  const cv_cfg_t* c = &g_settings.cv[ch];
  if (st.override_mv[ch] >= 0) return engine_mv_to_code(ch, st.override_mv[ch]);

  int64_t full_uv = (int64_t) c->range_mv * 1000;
  switch (c->source) {
    case SRC_PITCH:
      if (st.note < 0) return st.dac[ch]; // keep last pitch after release
      // 1 V per 12 semitones: uV = q16 * 1e6 / (12 * 65536)
      return uv_to_code(ch, (int64_t) pitch_q16() * 1000000 / (12 * 65536));
    case SRC_VELOCITY:
      return uv_to_code(ch, full_uv * st.velocity / 127);
    case SRC_CC:
      return uv_to_code(ch, full_uv * cc_values[c->cc] / 127);
    case SRC_AFTERTOUCH:
      return uv_to_code(ch, full_uv * st.aftertouch / 127);
    case SRC_BEND:
      return uv_to_code(ch, full_uv * st.bend / 16383);
    default:
      return uv_to_code(ch, 0);
  }
}

static bool smoothing_on(uint8_t ch) {
  const cv_cfg_t* c = &g_settings.cv[ch];
  return c->smooth_ms > 0 && c->source != SRC_PITCH && st.override_mv[ch] < 0;
}

// Recomputes an output. Unsmoothed outputs are written right away so that
// pitch is already settled when the gate goes high.
static void update_output(uint8_t ch) {
  target_code[ch] = compute_target(ch);
  if (!smoothing_on(ch)) {
    smooth_q16[ch] = (int32_t) target_code[ch] << 16;
    dac_out(ch, target_code[ch]);
  }
}

static void update_outputs_for(cv_source_t src) {
  for (uint8_t ch = 0; ch < CV_OUT_COUNT; ch++) {
    if (g_settings.cv[ch].source == src) update_output(ch);
  }
}

// ---------------------------------------------------------------------------
// Note stack (last note priority)

static void stack_remove(uint8_t note) {
  for (uint8_t i = 0; i < st.stack_len; i++) {
    if (stack[i] == note) {
      memmove(&stack[i], &stack[i + 1], st.stack_len - i - 1);
      st.stack_len--;
      return;
    }
  }
}

static void stack_push(uint8_t note) {
  stack_remove(note);
  if (st.stack_len == STACK_MAX) {
    memmove(&stack[0], &stack[1], STACK_MAX - 1);
    st.stack_len--;
  }
  stack[st.stack_len++] = note;
}

static void apply_stack(bool new_note_on) {
  bool was_gate = st.gate;
  int8_t prev = st.note;

  if (st.stack_len == 0) {
    st.gate = false;
    st.note = -1;
    gate_out(false);
    return;
  }

  st.note = (int8_t) stack[st.stack_len - 1];
  // Pitch first, then gate
  update_outputs_for(SRC_PITCH);
  update_outputs_for(SRC_VELOCITY);

  st.gate = true;
  if (was_gate && new_note_on && st.note != prev && g_settings.gate_retrigger) {
    gate_out(false);
    retrig_until = hal_millis() + RETRIGGER_MS;
  } else if (retrig_until == 0) {
    gate_out(true);
  }
}

static void note_on(uint8_t note, uint8_t vel) {
  key_down[note] = true;
  st.velocity = vel;
  stack_push(note);
  apply_stack(true);
}

static void note_off(uint8_t note) {
  key_down[note] = false;
  if (st.sustain) return; // released later, when the pedal goes up
  stack_remove(note);
  apply_stack(false);
}

static void sustain(bool on) {
  if (st.sustain == on) return;
  st.sustain = on;
  if (on) return;
  // Pedal released: drop notes whose keys are no longer held
  for (int i = (int) st.stack_len - 1; i >= 0; i--) {
    if (!key_down[stack[i]]) stack_remove(stack[i]);
  }
  apply_stack(false);
}

void engine_panic(void) {
  st.stack_len = 0;
  st.sustain = false;
  memset(key_down, 0, sizeof(key_down));
  retrig_until = 0;
  apply_stack(false);
}

// ---------------------------------------------------------------------------
// Learn

static void learn_done(void) {
  st.learn = LEARN_NONE;
  confirm_until = hal_millis() + CONFIRM_BLINK_MS;
  save_requested = true;
}

void engine_learn(learn_mode_t mode) {
  st.learn = mode;
  learn_started = hal_millis();
}

// ---------------------------------------------------------------------------
// MIDI input

static void control_change(uint8_t cc, uint8_t val) {
  if (st.learn == LEARN_CC_FILTER || st.learn == LEARN_CC_JACK) {
    // Sustain and channel mode messages are never learned
    if (cc != 64 && cc < 120) {
      uint8_t ch = st.learn == LEARN_CC_FILTER ? CV_OUT_FILTER : CV_OUT_JACK;
      g_settings.cv[ch].source = SRC_CC;
      g_settings.cv[ch].cc = cc;
      learn_done();
    }
  }

  cc_values[cc] = val;

  switch (cc) {
    case 64:
      sustain(val >= 64);
      break;
    case 120: // all sound off
    case 123: // all notes off
      engine_panic();
      break;
    case 121: // reset all controllers
      st.bend = 8192;
      st.aftertouch = 0;
      sustain(false);
      update_outputs_for(SRC_PITCH);
      update_outputs_for(SRC_BEND);
      update_outputs_for(SRC_AFTERTOUCH);
      break;
    default:
      break;
  }

  for (uint8_t ch = 0; ch < CV_OUT_COUNT; ch++) {
    if (g_settings.cv[ch].source == SRC_CC && g_settings.cv[ch].cc == cc) update_output(ch);
  }
}

static void realtime(uint8_t status) {
  switch (status) {
    case 0xF8: // clock
      if (clock_ticks % g_settings.clock_div == 0) {
        hal_clock_write(true);
        clock_off_at = hal_millis() + g_settings.clock_pulse_ms;
        if (clock_off_at == 0) clock_off_at = 1;
      }
      clock_ticks++;
      st.clock_count++;
      break;
    case 0xFA: // start
      clock_ticks = 0;
      break;
    case 0xFB: // continue
    case 0xFC: // stop
    default:
      break;
  }
}

void engine_midi(const midi_msg_t* m) {
  st.msg_count++;

  if (m->status >= 0xF8) {
    realtime(m->status);
    return;
  }
  if (m->status >= 0xF0) return;

  uint8_t type = m->status & 0xF0;
  uint8_t chan = m->status & 0x0F;

  if (st.learn == LEARN_CHANNEL && type == 0x90 && m->data[1] > 0) {
    g_settings.channel = chan;
    learn_done();
    // fall through: the learning note also plays
  }

  if (g_settings.channel != CHANNEL_OMNI && chan != g_settings.channel) return;

  switch (type) {
    case 0x90:
      if (m->data[1] > 0) {
        note_on(m->data[0], m->data[1]);
        break;
      }
      // velocity 0 = note off
      // fall through
    case 0x80:
      note_off(m->data[0]);
      break;
    case 0xB0:
      control_change(m->data[0], m->data[1]);
      break;
    case 0xD0:
      st.aftertouch = m->data[0];
      update_outputs_for(SRC_AFTERTOUCH);
      break;
    case 0xE0:
      st.bend = (uint16_t) (m->data[0] | (m->data[1] << 7));
      update_outputs_for(SRC_PITCH);
      update_outputs_for(SRC_BEND);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Button

static void button_short_press(void) {
  switch (st.learn) {
    case LEARN_NONE:
      engine_learn(LEARN_CC_FILTER);
      break;
    case LEARN_CC_FILTER:
      engine_learn(LEARN_CC_JACK); // second press: learn for the CV jack instead
      break;
    default:
      st.learn = LEARN_NONE; // third press (or press during channel learn): cancel
      break;
  }
}

void engine_button(bool pressed) {
  if (pressed != btn_raw) {
    btn_raw = pressed;
    btn_stable_ms = 0;
    return;
  }
  if (btn_stable_ms < BUTTON_DEBOUNCE_MS) {
    btn_stable_ms++;
    return;
  }

  if (btn_raw != btn_state) {
    btn_state = btn_raw;
    if (btn_state) {
      btn_held_ms = 0;
      btn_long_fired = false;
      btn_omni_fired = false;
      btn_factory_fired = false;
    } else if (btn_held_ms < BUTTON_SHORT_MAX_MS) {
      button_short_press();
    }
    return;
  }

  if (btn_state) {
    btn_held_ms++;
    if (!btn_long_fired && btn_held_ms >= BUTTON_CHANNEL_MS) {
      btn_long_fired = true;
      engine_learn(LEARN_CHANNEL);
    }
    if (!btn_omni_fired && btn_held_ms >= BUTTON_OMNI_MS) {
      btn_omni_fired = true;
      g_settings.channel = CHANNEL_OMNI;
      learn_done();
    }
    if (!btn_factory_fired && btn_held_ms >= BUTTON_FACTORY_MS) {
      btn_factory_fired = true;
      settings_defaults(&g_settings);
      engine_settings_changed();
      learn_done();
    }
  }
}

// ---------------------------------------------------------------------------
// Periodic work

static void update_leds(uint32_t now) {
  bool note_led = st.gate;

  if (now < confirm_until) {
    note_led = ((confirm_until - now) / 100) & 1; // fast triple blink
  } else {
    uint32_t t = now - learn_started;
    switch (st.learn) {
      case LEARN_CC_FILTER: note_led = (t / 100) & 1; break;                // fast blink
      case LEARN_CC_JACK: note_led = (t % 600) < 300 && ((t / 75) & 1); break; // double blink
      case LEARN_CHANNEL: note_led = (t / 500) & 1; break;                   // slow blink
      default: break;
    }
  }
  hal_led_note(note_led);
}

void engine_tick_1ms(void) {
  uint32_t now = hal_millis();

  if (retrig_until && (int32_t) (now - retrig_until) >= 0) {
    retrig_until = 0;
    if (st.gate) gate_out(true);
  }

  if (clock_off_at && (int32_t) (now - clock_off_at) >= 0) {
    clock_off_at = 0;
    hal_clock_write(false);
  }

  if (st.learn != LEARN_NONE && now - learn_started >= LEARN_TIMEOUT_MS) st.learn = LEARN_NONE;

  for (uint8_t ch = 0; ch < CV_OUT_COUNT; ch++) {
    if (!smoothing_on(ch)) continue;
    int32_t target = (int32_t) target_code[ch] << 16;
    int32_t alpha_q16 = 65536 / (g_settings.cv[ch].smooth_ms + 1);
    smooth_q16[ch] += (int32_t) (((int64_t) (target - smooth_q16[ch]) * alpha_q16) >> 16);
    // close the last fraction so the output really reaches the target
    if (smooth_q16[ch] - target < 65536 && target - smooth_q16[ch] < 65536) smooth_q16[ch] = target;
    dac_out(ch, (uint16_t) ((smooth_q16[ch] + 32768) >> 16));
  }

  update_leds(now);
}

// ---------------------------------------------------------------------------

void engine_override(uint8_t ch, int32_t mv) {
  if (ch >= CV_OUT_COUNT) return;
  st.override_mv[ch] = mv;
  update_output(ch);
}

void engine_settings_changed(void) {
  for (uint8_t ch = 0; ch < CV_OUT_COUNT; ch++) update_output(ch);
  if (!st.gate) gate_out(false);
}

void engine_init(void) {
  memset(&st, 0, sizeof(st));
  memset(cc_values, 0, sizeof(cc_values));
  memset(key_down, 0, sizeof(key_down));
  st.note = -1;
  st.bend = 8192;
  st.override_mv[0] = st.override_mv[1] = -1;
  st.dac[0] = st.dac[1] = 0xFFFF; // force the first write
  clock_ticks = 0;
  clock_off_at = 0;
  retrig_until = 0;
  confirm_until = 0;
  save_requested = false;
  btn_raw = btn_state = false;
  btn_stable_ms = 0;

  gate_out(false);
  hal_clock_write(false);
  hal_led_note(false);
  hal_led_aux(true);

  // Pitch outputs start at 0 V until the first note arrives
  for (uint8_t ch = 0; ch < CV_OUT_COUNT; ch++) {
    if (g_settings.cv[ch].source == SRC_PITCH) {
      dac_out(ch, engine_mv_to_code(ch, 0));
    }
  }
  engine_settings_changed();
}

const engine_status_t* engine_status(void) { return &st; }

bool engine_take_save_request(void) {
  bool r = save_requested;
  save_requested = false;
  return r;
}
