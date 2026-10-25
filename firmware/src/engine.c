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

// Portamento: pitch in Q16 semitones relative to zero_note, without bend
static int32_t pitch_cur_q16, pitch_tgt_q16, glide_step_q16;
static bool pitch_valid; // a note has been played, so there is something to glide from

// Filter modulation
#define ENV_FULL (1 << 24)
static int32_t env_level;      // 0..ENV_FULL
static int32_t env_scale_q16;  // velocity scaling, latched when the envelope starts
static uint32_t lfo_phase;
static int32_t lfo_value;      // -32767..32767
static int32_t lfo_random;
static uint32_t rng_state = 0x12345678u;
static int32_t mod_mv;         // total modulation added to the filter output

static const int16_t k_sine[256] = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
  12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
  23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
  30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
  32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
  30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
  23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
  12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
  0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
  -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
  -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
  -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
  -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
  -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
  -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
  -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};

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

// Pitch in Q16 semitones relative to zero_note: current (gliding) note plus bend
static int32_t pitch_q16(void) {
  // bend: 8192 = centre, full deflection = bend_range semitones
  int32_t bend = ((int32_t) st.bend - 8192) * g_settings.bend_range * 8; // x65536/8192
  return pitch_cur_q16 + bend;
}

static bool modulated(uint8_t ch) {
  const filter_mod_t* m = &g_settings.fmod;
  return ch == CV_OUT_FILTER && st.override_mv[ch] < 0 && (m->env_mv != 0 || m->lfo_mv != 0 || m->keytrack != 0);
}

static uint16_t compute_target(uint8_t ch) {
  const cv_cfg_t* c = &g_settings.cv[ch];
  if (st.override_mv[ch] >= 0) return engine_mv_to_code(ch, st.override_mv[ch]);

  int64_t full_uv = (int64_t) c->range_mv * 1000;
  switch (c->source) {
    case SRC_PITCH:
      // Holds the last note after release. 1 V per 12 semitones: uV = q16 * 1e6 / (12 * 65536)
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

// Writes base code + modulation (filter output only)
static void write_output(uint8_t ch, int32_t base_code) {
  int32_t code = base_code;
  if (modulated(ch)) {
    // codes = gain[codes/V x100] * mV / 100000
    code += (int32_t) (((int64_t) g_settings.cv[ch].cal_gain * mod_mv) / 100000);
  }
  dac_out(ch, (uint16_t) clamp_i32(code, 0, DAC_MAX));
}

// Recomputes an output. Unsmoothed outputs are written right away so that
// pitch is already settled when the gate goes high.
static void update_output(uint8_t ch) {
  target_code[ch] = compute_target(ch);
  if (!smoothing_on(ch)) {
    smooth_q16[ch] = (int32_t) target_code[ch] << 16;
    write_output(ch, target_code[ch]);
  }
}

// ---------------------------------------------------------------------------
// Portamento

static void set_pitch_target(int32_t target, bool legato) {
  uint16_t g = g_settings.glide_ms;
  bool glide = g > 0 && pitch_valid && (legato || !g_settings.glide_legato);
  pitch_tgt_q16 = target;
  pitch_valid = true;
  if (!glide) {
    pitch_cur_q16 = target;
    glide_step_q16 = 0;
    return;
  }
  int32_t step;
  if (g_settings.glide_mode == GLIDE_TIME) {
    int32_t dist = target - pitch_cur_q16;
    if (dist < 0) dist = -dist;
    step = dist / g; // whole transition takes glide_ms
  } else {
    step = (12 * 65536) / g; // one octave takes glide_ms
  }
  glide_step_q16 = step > 0 ? step : 1;
}

static bool glide_tick(void) {
  if (pitch_cur_q16 == pitch_tgt_q16) return false;
  int32_t d = pitch_tgt_q16 - pitch_cur_q16;
  if (d > glide_step_q16) d = glide_step_q16;
  if (d < -glide_step_q16) d = -glide_step_q16;
  pitch_cur_q16 += d;
  return true;
}

// ---------------------------------------------------------------------------
// Filter envelope and LFO

typedef enum { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } env_stage_t;
static env_stage_t env_stage;

static void env_start(void) {
  const filter_mod_t* m = &g_settings.fmod;
  // 0 % sensitivity = full depth always, 100 % = depth proportional to velocity
  env_scale_q16 = 65536 - (int32_t) m->env_vel * 65536 / 100 + (int32_t) m->env_vel * st.velocity * 65536 / (100 * 127);
  env_stage = ENV_ATTACK; // from the current level, so retriggers do not click
}

static void env_release(void) {
  if (env_stage != ENV_IDLE) env_stage = ENV_RELEASE;
}

// One-pole coefficient (Q16) that gets ~99 % of the way in `ms`
static int32_t exp_alpha(uint16_t ms) {
  if (ms == 0) return 65536;
  return 65536 / (ms / 5 + 1);
}

static void env_tick(void) {
  const filter_mod_t* m = &g_settings.fmod;
  int32_t sus = (int32_t) ((int64_t) m->sustain * ENV_FULL / 127);
  switch (env_stage) {
    case ENV_ATTACK:
      env_level = m->attack_ms ? env_level + ENV_FULL / m->attack_ms : ENV_FULL;
      if (env_level >= ENV_FULL) {
        env_level = ENV_FULL;
        env_stage = ENV_DECAY;
      }
      break;
    case ENV_DECAY:
      env_level += (int32_t) (((int64_t) (sus - env_level) * exp_alpha(m->decay_ms)) >> 16);
      if (env_level - sus < ENV_FULL / 4096 && sus - env_level < ENV_FULL / 4096) {
        env_level = sus;
        env_stage = ENV_SUSTAIN;
      }
      break;
    case ENV_SUSTAIN:
      env_level = sus; // follows live changes of the sustain level
      break;
    case ENV_RELEASE:
      env_level -= (int32_t) (((int64_t) env_level * exp_alpha(m->release_ms)) >> 16);
      if (env_level < ENV_FULL / 4096) {
        env_level = 0;
        env_stage = ENV_IDLE;
      }
      break;
    default:
      break;
  }
}

static void lfo_tick(void) {
  const filter_mod_t* m = &g_settings.fmod;
  uint32_t old = lfo_phase;
  lfo_phase += (uint32_t) m->lfo_rate * 42950u; // 2^32 / (100 * 1000 ms) per 0.01 Hz
  if (lfo_phase < old) {
    rng_state = rng_state * 1664525u + 1013904223u;
    lfo_random = (int32_t) (rng_state >> 16) - 32768;
  }
  int32_t p = (int32_t) (lfo_phase >> 16); // 0..65535
  switch (m->lfo_shape) {
    case LFO_SINE: {
      int32_t i = p >> 8, frac = p & 0xFF;
      int32_t a = k_sine[i], b = k_sine[(i + 1) & 0xFF];
      lfo_value = a + (((b - a) * frac) >> 8);
      break;
    }
    case LFO_TRIANGLE: lfo_value = p < 32768 ? p * 2 - 32767 : 32767 - (p - 32768) * 2; break;
    case LFO_SQUARE: lfo_value = p < 32768 ? 32767 : -32767; break;
    case LFO_SAW_UP: lfo_value = p - 32768; break;
    case LFO_SAW_DOWN: lfo_value = 32767 - p; break;
    case LFO_RANDOM: lfo_value = lfo_random; break;
    default: lfo_value = 0; break;
  }
}

static void update_mod(void) {
  const filter_mod_t* m = &g_settings.fmod;
  int64_t mv = 0;
  mv += (int64_t) m->env_mv * env_level / ENV_FULL * env_scale_q16 / 65536;
  int32_t depth = m->lfo_mv;
  if (m->lfo_wheel) depth = depth * cc_values[1] / 127;
  mv += (int64_t) depth * lfo_value / 32768;
  // key tracking: keytrack % of 1 V/oct, from the (gliding) note relative to zero_note
  mv += (int64_t) m->keytrack * pitch_cur_q16 * 10 / (12 * 65536);
  mod_mv = (int32_t) (mv < -20000 ? -20000 : (mv > 20000 ? 20000 : mv));
  st.mod_mv = mod_mv;
  st.env_stage = (uint8_t) env_stage;
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
    env_release();
    return;
  }

  st.note = (int8_t) stack[st.stack_len - 1];
  if (st.note != prev || !was_gate) {
    set_pitch_target(((int32_t) st.note - g_settings.zero_note) * 65536, was_gate);
  }
  // Pitch first, then gate
  update_outputs_for(SRC_PITCH);
  update_outputs_for(SRC_VELOCITY);

  if (!was_gate || (new_note_on && g_settings.fmod.env_retrig)) env_start();

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
  if (g_settings.fmod.lfo_sync) lfo_phase = 0;
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

  if (glide_tick()) update_outputs_for(SRC_PITCH);

  env_tick();
  lfo_tick();
  update_mod();

  for (uint8_t ch = 0; ch < CV_OUT_COUNT; ch++) {
    bool smooth = smoothing_on(ch);
    if (smooth) {
      int32_t target = (int32_t) target_code[ch] << 16;
      int32_t alpha_q16 = 65536 / (g_settings.cv[ch].smooth_ms + 1);
      smooth_q16[ch] += (int32_t) (((int64_t) (target - smooth_q16[ch]) * alpha_q16) >> 16);
      // close the last fraction so the output really reaches the target
      if (smooth_q16[ch] - target < 65536 && target - smooth_q16[ch] < 65536) smooth_q16[ch] = target;
    }
    if (smooth || modulated(ch)) write_output(ch, (smooth_q16[ch] + 32768) >> 16);
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
  pitch_cur_q16 = pitch_tgt_q16 = 0; // pitch outputs start at 0 V until the first note
  glide_step_q16 = 0;
  pitch_valid = false;
  env_stage = ENV_IDLE;
  env_level = 0;
  env_scale_q16 = 65536;
  lfo_phase = 0;
  lfo_value = 0;
  mod_mv = 0;

  gate_out(false);
  hal_clock_write(false);
  hal_led_note(false);
  hal_led_aux(true);

  engine_settings_changed();
}

const engine_status_t* engine_status(void) { return &st; }

bool engine_take_save_request(void) {
  bool r = save_requested;
  save_requested = false;
  return r;
}
