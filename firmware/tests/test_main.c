// Host-side tests for the MIDI parser and the CV/Gate engine.
// Build and run: make test   (uses the PC's gcc, no board needed)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine.h"
#include "../src/hal.h"
#include "../src/midi_parser.h"
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

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
