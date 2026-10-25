// Host-side tests for the MIDI parser and the CV/Gate engine.
// Build and run: make test   (uses the PC's gcc, no board needed)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine.h"
#include "../src/hal.h"
#include "../src/midi_parser.h"
#include "../src/params.h"
#include "../src/sysex.h"
#include "../src/settings.h"

// ---------------------------------------------------------------------------
// HAL stubs

static uint32_t now_ms;
static uint16_t dac[2];
static bool gate, clk, led_note, led_aux;
static uint8_t flash[512];
static bool flash_valid;

uint32_t hal_millis(void) { return now_ms; }
void hal_dac_write(uint8_t ch, uint16_t code) { dac[ch] = code; }
void hal_gate_write(bool on) { gate = on; }
void hal_clock_write(bool on) { clk = on; }
void hal_led_note(bool on) { led_note = on; }
void hal_led_aux(bool on) { led_aux = on; }
bool hal_storage_read(void* dst, uint32_t len) {
  if (!flash_valid) return false;
  memcpy(dst, flash, len);
  return true;
}
void sysex_get_diag(sysex_diag_t* d) {
  d->chip_id = 0x411;
  d->hse_mhz = 8;
  d->din_errors = 3;
  d->reset_flags = 0x24000000u;
}
bool hal_storage_write(const void* src, uint32_t len) {
  memcpy(flash, src, len);
  flash_valid = true;
  return true;
}

// ---------------------------------------------------------------------------

static int failures, checks;

#define CHECK(cond)                                                        \
  do {                                                                     \
    checks++;                                                              \
    if (!(cond)) {                                                         \
      failures++;                                                          \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                      \
  } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(abs((int) (a) - (int) (b)) <= (tol))

static void tick(int ms) {
  for (int i = 0; i < ms; i++) {
    now_ms++;
    engine_tick_1ms();
  }
}

static void send(const uint8_t* bytes, int n) {
  static midi_parser_t p;
  static bool init;
  if (!init) {
    midi_parser_init(&p);
    init = true;
  }
  for (int i = 0; i < n; i++) {
    midi_msg_t m;
    bool sx;
    if (midi_parser_feed(&p, bytes[i], &m, &sx)) engine_midi(&m);
  }
}

#define SEND(...)                                   \
  do {                                              \
    const uint8_t b_[] = {__VA_ARGS__};             \
    send(b_, (int) sizeof(b_));                     \
  } while (0)

static void reset_engine(void) {
  settings_defaults(&g_settings);
  now_ms = 1000;
  engine_init();
  SEND(0xB0, 123, 0); // all notes off, also resets parser running status use
}

// Expected DAC code for a voltage with nominal calibration (405.45 codes/V)
static int code_for_mv(int mv) { return (int) (((long long) mv * 40545 + 50000) / 100000); }

// ---------------------------------------------------------------------------

static void test_parser(void) {
  midi_parser_t p;
  midi_parser_init(&p);
  midi_msg_t m;
  bool sx;
  int n = 0;

  // Running status: 90 3C 64 3E 64 -> two Note On
  const uint8_t rs[] = {0x90, 0x3C, 0x64, 0x3E, 0x64};
  for (unsigned i = 0; i < sizeof(rs); i++) {
    if (midi_parser_feed(&p, rs[i], &m, &sx)) {
      n++;
      CHECK(m.status == 0x90 && m.len == 3);
    }
  }
  CHECK(n == 2);
  CHECK(m.data[0] == 0x3E);

  // Realtime in the middle of a message
  n = 0;
  const uint8_t rt[] = {0xB2, 0x07, 0xF8, 0x50};
  uint8_t seen[4];
  for (unsigned i = 0; i < sizeof(rt); i++) {
    if (midi_parser_feed(&p, rt[i], &m, &sx)) seen[n++] = m.status;
  }
  CHECK(n == 2 && seen[0] == 0xF8 && seen[1] == 0xB2);
  CHECK(m.data[0] == 0x07 && m.data[1] == 0x50);

  // Program change (1 data byte) with running status
  n = 0;
  const uint8_t pc[] = {0xC0, 0x05, 0x06};
  for (unsigned i = 0; i < sizeof(pc); i++) n += midi_parser_feed(&p, pc[i], &m, &sx);
  CHECK(n == 2 && m.len == 2 && m.data[0] == 6);

  // SysEx is reported as raw bytes and cancels running status
  int sx_bytes = 0;
  n = 0;
  const uint8_t se[] = {0xF0, 0x7D, 0x01, 0xF7, 0x3C};
  for (unsigned i = 0; i < sizeof(se); i++) {
    n += midi_parser_feed(&p, se[i], &m, &sx);
    sx_bytes += sx;
  }
  CHECK(sx_bytes == 4);
  CHECK(n == 0); // stray data byte after SysEx is ignored
}

static void test_pitch_and_gate(void) {
  reset_engine();
  CHECK(gate == false);

  SEND(0x90, 36, 100); // C2 -> 0 V
  CHECK(gate == true);
  CHECK(dac[CV_OUT_JACK] == 0);

  SEND(0x90, 48, 100); // C3 -> 1 V (legato, last note priority)
  CHECK(gate == true);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000), 1);

  SEND(0x90, 84, 100); // C6 -> 4 V
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(4000), 1);

  SEND(0x80, 84, 0); // back to C3
  CHECK(gate == true);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000), 1);

  SEND(0x90, 48, 0); // Note On vel 0 = off -> back to C2
  CHECK_NEAR(dac[CV_OUT_JACK], 0, 1);
  CHECK(gate == true);

  SEND(0x80, 36, 0);
  CHECK(gate == false);
  CHECK(led_note == false);

  // Notes below zero_note clamp to 0 V, high notes clamp to the DAC top
  SEND(0x90, 20, 100);
  CHECK(dac[CV_OUT_JACK] == 0);
  SEND(0x90, 127, 100); // 7.583 V
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(7583), 1);
  SEND(0xB0, 123, 0);
  CHECK(gate == false);
}

static void test_bend(void) {
  reset_engine();
  SEND(0x90, 48, 100);              // 1 V
  SEND(0xE0, 0x7F, 0x7F);           // full up, +2 semitones
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000 + 2000 / 12), 1);
  SEND(0xE0, 0x00, 0x00);           // full down
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000 - 2000 / 12), 1);
  SEND(0xE0, 0x00, 0x40);           // centre
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000), 1);
}

static void test_sustain(void) {
  reset_engine();
  SEND(0xB0, 64, 127);
  SEND(0x90, 60, 100);
  SEND(0x80, 60, 0);
  CHECK(gate == true); // held by the pedal
  SEND(0x90, 62, 100);
  SEND(0xB0, 64, 0);   // pedal up, 62 still held
  CHECK(gate == true);
  CHECK(engine_status()->note == 62);
  SEND(0x80, 62, 0);
  CHECK(gate == false);
}

static void test_retrigger(void) {
  reset_engine();
  g_settings.gate_retrigger = 1;
  SEND(0x90, 60, 100);
  CHECK(gate == true);
  SEND(0x90, 64, 100);
  CHECK(gate == false); // short drop
  tick(3);
  CHECK(gate == true);
  SEND(0x90, 64, 100); // same note again: no drop
  CHECK(gate == true);
}

static void test_cc_to_filter(void) {
  reset_engine();
  // Filter follows CC74 with smoothing
  SEND(0xB0, 74, 127);
  tick(100);
  CHECK(dac[CV_OUT_FILTER] == code_for_mv(10000));
  SEND(0xB0, 74, 0);
  tick(100);
  CHECK(dac[CV_OUT_FILTER] == 0);
  SEND(0xB0, 74, 64);
  tick(1);
  CHECK(dac[CV_OUT_FILTER] > 0 && dac[CV_OUT_FILTER] < code_for_mv(5039)); // still moving
  tick(100);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(10000 * 64 / 127), 1);

  // Other CCs do not touch it
  SEND(0xB0, 1, 127);
  tick(100);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(10000 * 64 / 127), 1);

  // The sketch example: CC15 = 121 on the jack, 0..5 V range
  g_settings.cv[CV_OUT_JACK].source = SRC_CC;
  g_settings.cv[CV_OUT_JACK].cc = 15;
  g_settings.cv[CV_OUT_JACK].range_mv = 5000;
  engine_settings_changed();
  SEND(0xB0, 15, 121);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(5000 * 121 / 127), 1);
}

static void test_channel_filter(void) {
  reset_engine();
  g_settings.channel = 2; // MIDI channel 3
  SEND(0x90, 60, 100);
  CHECK(gate == false);
  SEND(0x92, 60, 100);
  CHECK(gate == true);
  SEND(0x82, 60, 0);
  CHECK(gate == false);
}

static void test_learn_button(void) {
  reset_engine();
  flash_valid = false;

  // Short press -> CC learn for the filter
  for (int i = 0; i < 100; i++) { engine_button(true); tick(1); }
  for (int i = 0; i < 50; i++) { engine_button(false); tick(1); }
  CHECK(engine_status()->learn == LEARN_CC_FILTER);
  SEND(0xB0, 64, 127); // sustain is never learned
  CHECK(engine_status()->learn == LEARN_CC_FILTER);
  SEND(0xB0, 64, 0);
  SEND(0xB0, 21, 127);
  CHECK(engine_status()->learn == LEARN_NONE);
  CHECK(g_settings.cv[CV_OUT_FILTER].cc == 21);
  CHECK(engine_take_save_request());
  CHECK(!engine_take_save_request());

  // Hold 3 s -> channel learn, next Note On selects channel 10
  for (int i = 0; i < 3100; i++) { engine_button(true); tick(1); }
  CHECK(engine_status()->learn == LEARN_CHANNEL);
  for (int i = 0; i < 50; i++) { engine_button(false); tick(1); }
  CHECK(engine_status()->learn == LEARN_CHANNEL); // release after long hold is not a short press
  SEND(0x99, 40, 100);
  CHECK(g_settings.channel == 9);
  CHECK(engine_status()->learn == LEARN_NONE);
  CHECK(gate == true);
  SEND(0x89, 40, 0);

  // Learn times out after 10 s
  for (int i = 0; i < 100; i++) { engine_button(true); tick(1); }
  for (int i = 0; i < 50; i++) { engine_button(false); tick(1); }
  CHECK(engine_status()->learn == LEARN_CC_FILTER);
  tick(10100);
  CHECK(engine_status()->learn == LEARN_NONE);

  // Hold 8 s -> Omni
  for (int i = 0; i < 8100; i++) { engine_button(true); tick(1); }
  for (int i = 0; i < 50; i++) { engine_button(false); tick(1); }
  CHECK(g_settings.channel == CHANNEL_OMNI);

  // Hold 15 s -> factory settings (the learned filter CC goes back to 74)
  CHECK(g_settings.cv[CV_OUT_FILTER].cc == 21);
  engine_take_save_request();
  for (int i = 0; i < 15100; i++) { engine_button(true); tick(1); }
  for (int i = 0; i < 50; i++) { engine_button(false); tick(1); }
  CHECK(g_settings.cv[CV_OUT_FILTER].cc == 74);
  CHECK(engine_take_save_request());
}

static void test_clock(void) {
  reset_engine();
  SEND(0xFA);
  int pulses = 0;
  bool last = false;
  for (int i = 0; i < 24; i++) { // one quarter note at 120 BPM: clock every ~21 ms
    SEND(0xF8);
    for (int t = 0; t < 21; t++) {
      if (clk && !last) pulses++;
      last = clk;
      tick(1);
    }
  }
  CHECK(pulses == 4); // divider 6 -> 4 pulses per quarter
  tick(10);
  CHECK(clk == false);
}

static void test_settings_storage(void) {
  flash_valid = false;
  settings_t s;
  CHECK(!settings_load(&s));
  CHECK(s.channel == CHANNEL_OMNI && s.cv[1].cc == 74);
  s.channel = 5;
  s.cv[0].cal_zero = 3;
  CHECK(settings_save(&s));
  settings_t r;
  CHECK(settings_load(&r));
  CHECK(r.channel == 5 && r.cv[0].cal_zero == 3);
  flash[10] ^= 0xFF; // corrupt
  CHECK(!settings_load(&r));
  CHECK(r.channel == CHANNEL_OMNI);
}

static void test_override_and_calibration(void) {
  reset_engine();
  engine_override(CV_OUT_JACK, 5000);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(5000), 1);
  SEND(0x90, 60, 100); // ignored while overridden
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(5000), 1);
  engine_override(CV_OUT_JACK, -1);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(2000), 1);

  g_settings.cv[CV_OUT_JACK].cal_zero = 10;
  g_settings.cv[CV_OUT_JACK].cal_gain = 41000;
  CHECK(engine_mv_to_code(CV_OUT_JACK, 0) == 10);
  CHECK(engine_mv_to_code(CV_OUT_JACK, 1000) == 420);
  CHECK(engine_mv_to_code(CV_OUT_JACK, 20000) == DAC_MAX);
  CHECK(engine_mv_to_code(CV_OUT_JACK, -1000) == 0);
}


// ---------------------------------------------------------------------------
// Portamento

static void test_glide(void) {
  reset_engine();
  g_settings.glide_ms = 100; // constant rate: 100 ms per octave, legato only (defaults)
  engine_settings_changed();

  SEND(0x90, 36, 100);
  CHECK(dac[CV_OUT_JACK] == 0); // first note never glides
  SEND(0x90, 48, 100);          // legato: glide up one octave
  CHECK(dac[CV_OUT_JACK] < 10);
  tick(50);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(500), 3);
  tick(60);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000), 1);

  // Detached note with "legato only": jumps
  SEND(0x80, 48, 0);
  SEND(0x80, 36, 0);
  SEND(0x90, 60, 100);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(2000), 1);
  SEND(0x80, 60, 0);

  // "Always": detached notes glide too
  g_settings.glide_legato = 0;
  SEND(0x90, 36, 100);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(2000), 1);
  tick(100);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(1000), 3);
  tick(110);
  CHECK(dac[CV_OUT_JACK] == 0);
  SEND(0x80, 36, 0);

  // Constant time: 0 -> 4 V in 100 ms, halfway at 50 ms
  g_settings.glide_mode = GLIDE_TIME;
  SEND(0x90, 84, 100);
  tick(50);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(2000), 5);
  tick(60);
  CHECK_NEAR(dac[CV_OUT_JACK], code_for_mv(4000), 1);
  SEND(0xB0, 123, 0);
}

// ---------------------------------------------------------------------------
// Filter modulation

static void filter_mod_setup(void) {
  reset_engine();
  g_settings.cv[CV_OUT_FILTER].smooth_ms = 0;
  g_settings.fmod.env_mv = 5000;
  g_settings.fmod.attack_ms = 10;
  g_settings.fmod.decay_ms = 100;
  g_settings.fmod.sustain = 64;
  g_settings.fmod.release_ms = 100;
  g_settings.fmod.env_vel = 0;
  engine_settings_changed();
}

static void test_envelope(void) {
  filter_mod_setup();
  CHECK(dac[CV_OUT_FILTER] == 0);
  SEND(0x90, 60, 127);
  tick(5);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(2500), 10); // half way up the attack
  tick(5);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(5000), 3);  // peak
  tick(200);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(5000 * 64 / 127), 3); // sustain
  CHECK(engine_status()->env_stage == 3);
  SEND(0x80, 60, 0);
  tick(30);
  CHECK(dac[CV_OUT_FILTER] < code_for_mv(5000 * 64 / 127)); // releasing
  tick(200);
  CHECK(dac[CV_OUT_FILTER] == 0);
  CHECK(engine_status()->env_stage == 0);

  // Velocity sensitivity 100 %: half velocity, half depth
  g_settings.fmod.env_vel = 100;
  SEND(0x90, 60, 64);
  tick(10);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(5000 * 64 / 127), 5);
  SEND(0x80, 60, 0);
  tick(300);

  // Negative depth closes the filter from the CC74 position
  g_settings.fmod.env_mv = -5000;
  g_settings.fmod.env_vel = 0;
  SEND(0xB0, 74, 127);
  CHECK(dac[CV_OUT_FILTER] == code_for_mv(10000));
  SEND(0x90, 60, 100);
  tick(10);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(5000), 3);
  SEND(0x80, 60, 0);
  tick(300);
  CHECK(dac[CV_OUT_FILTER] == code_for_mv(10000));

  // Legato notes do not restart the envelope unless envretrig is on
  g_settings.fmod.env_mv = 5000;
  SEND(0xB0, 74, 0);
  SEND(0x90, 60, 100);
  tick(300); // at sustain
  SEND(0x90, 62, 100);
  tick(5);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(5000 * 64 / 127), 3);
  g_settings.fmod.env_retrig = 1;
  SEND(0x90, 64, 100);
  tick(5); // attack restarts from the current (sustain) level, so the peak comes sooner
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(5000), 3);
  SEND(0xB0, 123, 0);
}

static void test_lfo(void) {
  filter_mod_setup();
  g_settings.fmod.env_mv = 0;
  g_settings.fmod.lfo_mv = 1000;
  g_settings.fmod.lfo_rate = 100; // 1 Hz
  g_settings.fmod.lfo_shape = LFO_SQUARE;
  SEND(0xB0, 74, 64); // base 5.04 V
  int lo = 9999, hi = 0;
  for (int i = 0; i < 1000; i++) {
    tick(1);
    if (dac[CV_OUT_FILTER] < lo) lo = dac[CV_OUT_FILTER];
    if (dac[CV_OUT_FILTER] > hi) hi = dac[CV_OUT_FILTER];
  }
  int base = code_for_mv(10000 * 64 / 127);
  CHECK_NEAR(hi, base + code_for_mv(1000), 2);
  CHECK_NEAR(lo, base - code_for_mv(1000), 2);

  // Sine: full swing over one period, back near the base at the half period
  g_settings.fmod.lfo_shape = LFO_SINE;
  SEND(0x90, 60, 100); // lfosync off: phase keeps running
  lo = 9999, hi = 0;
  for (int i = 0; i < 1000; i++) {
    tick(1);
    if (dac[CV_OUT_FILTER] < lo) lo = dac[CV_OUT_FILTER];
    if (dac[CV_OUT_FILTER] > hi) hi = dac[CV_OUT_FILTER];
  }
  CHECK_NEAR(hi, base + code_for_mv(1000), 3);
  CHECK_NEAR(lo, base - code_for_mv(1000), 3);
  SEND(0x80, 60, 0);

  // Mod wheel scales the depth: wheel at 0 -> no LFO
  g_settings.fmod.lfo_wheel = 1;
  SEND(0xB0, 1, 0);
  lo = 9999, hi = 0;
  for (int i = 0; i < 1000; i++) {
    tick(1);
    if (dac[CV_OUT_FILTER] < lo) lo = dac[CV_OUT_FILTER];
    if (dac[CV_OUT_FILTER] > hi) hi = dac[CV_OUT_FILTER];
  }
  CHECK(lo == base && hi == base);

  // Sync: a new note restarts the phase (square starts high)
  g_settings.fmod.lfo_wheel = 0;
  g_settings.fmod.lfo_sync = 1;
  g_settings.fmod.lfo_shape = LFO_SQUARE;
  tick(600); // somewhere in the low half
  SEND(0x90, 60, 100);
  tick(1);
  CHECK_NEAR(dac[CV_OUT_FILTER], base + code_for_mv(1000), 2);
  SEND(0xB0, 123, 0);
}

static void test_keytrack(void) {
  filter_mod_setup();
  g_settings.fmod.env_mv = 0;
  g_settings.fmod.keytrack = 100;
  SEND(0x90, 48, 100);
  tick(1);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(1000), 1);
  SEND(0x90, 60, 100);
  tick(1);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(2000), 1);
  g_settings.fmod.keytrack = 50;
  tick(1);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(1000), 1);
  SEND(0xB0, 123, 0);
  // Override still wins over modulation
  engine_override(CV_OUT_FILTER, 3000);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(3000), 1);
  tick(5);
  CHECK_NEAR(dac[CV_OUT_FILTER], code_for_mv(3000), 1);
  engine_override(CV_OUT_FILTER, -1);
}

// ---------------------------------------------------------------------------
// Parameter table

static void test_params(void) {
  settings_t s;
  settings_defaults(&s);
  const param_t* ch = param_by_name("channel");
  CHECK(ch && param_get(&s, ch) == 0); // omni
  CHECK(param_set(&s, ch, 10) && s.channel == 9);
  CHECK(param_set(&s, ch, 0) && s.channel == CHANNEL_OMNI);
  CHECK(!param_set(&s, ch, 17));

  const param_t* env = param_by_name("env");
  CHECK(env && env->id == 34);
  CHECK(param_set(&s, env, -3000) && s.fmod.env_mv == -3000 && param_get(&s, env) == -3000);
  CHECK(!param_set(&s, env, 10001));

  int32_t v;
  const param_t* shape = param_by_name("lfoshape");
  CHECK(param_parse(shape, "tri", &v) && v == LFO_TRIANGLE);
  CHECK(param_value_name(shape, LFO_RANDOM) && !strcmp(param_value_name(shape, LFO_RANDOM), "random"));
  CHECK(param_parse(ch, "omni", &v) && v == 0);
  CHECK(param_parse(param_by_name("retrig"), "on", &v) && v == 1);
  CHECK(!param_parse(env, "abc", &v));
  CHECK(param_by_id(52) == param_by_name("glidelegato"));

  // Ids and names are unique; every default is inside its range
  for (unsigned i = 0; i < param_count(); i++) {
    const param_t* a = param_at(i);
    int32_t d = param_get(&s, a);
    settings_t def;
    settings_defaults(&def);
    d = param_get(&def, a);
    CHECK(d >= a->min && d <= a->max);
    for (unsigned j = i + 1; j < param_count(); j++) {
      CHECK(param_at(j)->id != a->id);
      CHECK(strcmp(param_at(j)->name, a->name) != 0);
    }
  }

  // Out-of-range value in flash -> rejected, defaults used
  flash_valid = false;
  settings_defaults(&s);
  s.fmod.lfo_rate = 0; // below min
  CHECK(settings_save(&s));
  settings_t r;
  CHECK(!settings_load(&r));
  CHECK(r.fmod.lfo_rate == 200);
}

// ---------------------------------------------------------------------------
// SysEx

static uint8_t sx_out[2048];
static uint32_t sx_len;
static int sx_msgs;

static void sx_reply(const uint8_t* d, uint32_t n, void* ctx) {
  (void) ctx;
  memcpy(sx_out + sx_len, d, n);
  sx_len += n;
  sx_msgs++;
}

static void sx_send(uint8_t cmd, uint8_t param, const int32_t* v) {
  uint8_t m[16];
  uint32_t n = sysex_build(m, cmd, param, v);
  sx_len = 0;
  sx_msgs = 0;
  sysex_handle(m, n, sx_reply, NULL);
}

// Decodes reply number i (all replies are 9 or 12 bytes)
static bool sx_reply_at(int i, uint8_t* cmd, uint8_t* param, int32_t* value) {
  uint32_t off = 0;
  for (int k = 0; k <= i; k++) {
    if (off + 9 > sx_len) return false;
    uint32_t n = 9u + sx_out[off + 6];
    if (k == i) {
      const uint8_t* m = sx_out + off;
      if (m[0] != 0xF0 || m[n - 1] != 0xF7 || m[1] != 0x7D || m[2] != 0x4D) return false;
      if (sysex_crc7(m + 2, 5u + m[6]) != m[7 + m[6]]) return false;
      *cmd = m[4];
      *param = m[5];
      if (m[6] == 3) {
        uint32_t u = ((uint32_t) m[7] << 14) | ((uint32_t) m[8] << 7) | m[9];
        *value = (u & 0x100000u) ? (int32_t) u - 0x200000 : (int32_t) u;
      } else {
        *value = 0;
      }
      return true;
    }
    off += n;
  }
  return false;
}

static void test_sysex(void) {
  reset_engine();
  uint8_t cmd, param;
  int32_t v;

  // CRC-7 reference: "123456789" -> 0x75 (CRC-7/MMC check value)
  CHECK(sysex_crc7((const uint8_t*) "123456789", 9) == 0x75);

  // Same bytes as tools/mcv_sysex.py and the examples in docs/SYSEX.md
  {
    static const uint8_t set_env_5000[] = {0xF0, 0x7D, 0x4D, 0x00, 0x02, 0x22, 0x03, 0x00, 0x27, 0x08, 0x6C, 0xF7};
    static const uint8_t set_env_m2500[] = {0xF0, 0x7D, 0x4D, 0x00, 0x02, 0x22, 0x03, 0x7F, 0x6C, 0x3C, 0x6D, 0xF7};
    static const uint8_t get_channel[] = {0xF0, 0x7D, 0x4D, 0x00, 0x01, 0x01, 0x00, 0x22, 0xF7};
    uint8_t m[16];
    int32_t x = 5000;
    CHECK(sysex_build(m, SX_SET, 34, &x) == sizeof(set_env_5000) && !memcmp(m, set_env_5000, sizeof(set_env_5000)));
    x = -2500;
    CHECK(sysex_build(m, SX_SET, 34, &x) == sizeof(set_env_m2500) && !memcmp(m, set_env_m2500, sizeof(set_env_m2500)));
    CHECK(sysex_build(m, SX_GET, 1, NULL) == sizeof(get_channel) && !memcmp(m, get_channel, sizeof(get_channel)));
  }

  // GET channel (omni = 0)
  sx_send(SX_GET, 1, NULL);
  CHECK(sx_msgs == 1 && sx_reply_at(0, &cmd, &param, &v) && cmd == SX_VALUE && param == 1 && v == 0);

  // SET envelope depth to a negative value, applied right away
  v = -2500;
  sx_send(SX_SET, 34, &v);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_ACK && param == SX_SET);
  CHECK(g_settings.fmod.env_mv == -2500);
  sx_send(SX_GET, 34, NULL);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && v == -2500);

  // Out of range, unknown parameter, unknown command
  v = 20000;
  sx_send(SX_SET, 34, &v);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_NACK && param == SX_SET && v == SX_ERR_RANGE);
  v = 1;
  sx_send(SX_SET, 99, &v);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_NACK && v == SX_ERR_PARAM);
  sx_send(0x33, 0, NULL);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_NACK && param == 0x33 && v == SX_ERR_COMMAND);

  // Calibration is locked until CAL_UNLOCK "CAL"
  v = 41000;
  sx_send(SX_SET, 15, &v);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_NACK && v == SX_ERR_LOCKED);
  {
    uint8_t m[] = {0xF0, 0x7D, 0x4D, 0x00, SX_CAL_UNLOCK, 0x00, 3, 'C', 'A', 'L', 0, 0xF7};
    m[10] = sysex_crc7(m + 2, 8);
    sx_len = 0;
    sysex_handle(m, sizeof(m), sx_reply, NULL);
    CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_ACK && param == SX_CAL_UNLOCK);
  }
  v = 41000;
  sx_send(SX_CAL_SET, 15, &v);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_ACK);
  CHECK(g_settings.cv[0].cal_gain == 41000);

  // Bad CRC
  {
    uint8_t m[16];
    uint32_t n = sysex_build(m, SX_GET, 2, NULL);
    m[n - 2] ^= 0x01;
    sx_len = 0;
    sysex_handle(m, n, sx_reply, NULL);
    CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_NACK && v == SX_ERR_CRC);
  }

  // Other device or product: silently ignored
  {
    uint8_t m[16];
    uint32_t n = sysex_build(m, SX_GET, 2, NULL);
    m[3] = 0x05;
    m[n - 2] = sysex_crc7(m + 2, n - 4);
    sx_len = 0;
    sx_msgs = 0;
    sysex_handle(m, n, sx_reply, NULL);
    CHECK(sx_msgs == 0);
  }

  // DUMP: protocol + firmware version + every parameter, then ACK
  sx_send(SX_DUMP, 0, NULL);
  CHECK(sx_msgs == (int) param_count() + 3);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_VALUE && param == SX_ID_PROTOCOL && v == SYSEX_PROTOCOL);
  CHECK(sx_reply_at(sx_msgs - 1, &cmd, &param, &v) && cmd == SX_ACK && param == SX_DUMP);

  // DIAG
  sx_send(SX_DIAG, 0, NULL);
  CHECK(sx_reply_at(1, &cmd, &param, &v) && param == SX_ID_CHIP && v == 0x411);
  CHECK(sx_reply_at(2, &cmd, &param, &v) && param == SX_ID_HSE_MHZ && v == 8);

  // SAVE writes flash, FACTORY_RESET restores defaults in RAM
  flash_valid = false;
  sx_send(SX_SAVE, 0, NULL);
  CHECK(sx_reply_at(0, &cmd, &param, &v) && cmd == SX_ACK && flash_valid);
  sx_send(SX_FACTORY_RESET, 0, NULL);
  CHECK(g_settings.fmod.env_mv == 0 && g_settings.cv[0].cal_gain == 40545);
  settings_t r;
  CHECK(settings_load(&r) && r.fmod.env_mv == -2500); // the saved copy is untouched

  // Receiver: assembles F0..F7 from single bytes, abort drops a partial message
  {
    sysex_rx_t rx = {0};
    uint8_t m[16];
    uint32_t n = sysex_build(m, SX_GET, 3, NULL);
    sx_len = 0;
    sx_msgs = 0;
    for (uint32_t i = 0; i < 4; i++) sysex_rx_byte(&rx, m[i], sx_reply, NULL);
    sysex_rx_abort(&rx);
    for (uint32_t i = 4; i < n; i++) sysex_rx_byte(&rx, m[i], sx_reply, NULL);
    CHECK(sx_msgs == 0);
    for (uint32_t i = 0; i < n; i++) sysex_rx_byte(&rx, m[i], sx_reply, NULL);
    CHECK(sx_msgs == 1 && sx_reply_at(0, &cmd, &param, &v) && param == 3 && v == 2);
  }
  settings_defaults(&g_settings);
}

int main(void) {
  test_parser();
  test_pitch_and_gate();
  test_bend();
  test_sustain();
  test_retrigger();
  test_cc_to_filter();
  test_channel_filter();
  test_learn_button();
  test_clock();
  test_settings_storage();
  test_override_and_calibration();
  test_glide();
  test_envelope();
  test_lfo();
  test_keytrack();
  test_params();
  test_sysex();

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
