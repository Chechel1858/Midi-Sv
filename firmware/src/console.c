#include "console.h"

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "fmt.h"
#include "hal.h"
#include "settings.h"
#include "tusb.h"

#define FW_VERSION "0.1.0"

static char line[96];
static uint8_t line_len;
static bool cdc_was_connected;

static void out(const char* s, uint32_t n) {
  debug_uart_write((const uint8_t*) s, n);
  if (tud_cdc_connected()) {
    uint32_t stalls = 0;
    while (n) {
      uint32_t w = tud_cdc_write(s, n);
      if (w == 0) {
        // Buffer full: let the USB stack drain it, give up if the host
        // does not read (terminal open but stuck)
        tud_cdc_write_flush();
        tud_task();
        if (!tud_cdc_connected() || ++stalls > 2000) break;
        continue;
      }
      s += w;
      n -= w;
    }
    tud_cdc_write_flush();
  }
}

void console_printf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = fmt_vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int) sizeof(buf)) n = sizeof(buf) - 1;
  out(buf, (uint32_t) n);
}

// ---------------------------------------------------------------------------

static const char* const src_names[SRC_COUNT] = {"off", "pitch", "vel", "cc", "at", "bend"};
static const char* const out_names[CV_OUT_COUNT] = {"jack", "filter"};

static int parse_out(const char* s) {
  if (!s) return -1;
  if (!strcmp(s, "jack") || !strcmp(s, "1")) return CV_OUT_JACK;
  if (!strcmp(s, "filter") || !strcmp(s, "2")) return CV_OUT_FILTER;
  return -1;
}

static bool parse_int(const char* s, long lo, long hi, long* v) {
  if (!s) return false;
  bool neg = *s == '-';
  if (neg) s++;
  if (!*s) return false;
  long x = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || x > 100000000L) return false;
    x = x * 10 + (*s - '0');
  }
  if (neg) x = -x;
  if (x < lo || x > hi) return false;
  *v = x;
  return true;
}

static void print_out_cfg(int ch) {
  const cv_cfg_t* c = &g_settings.cv[ch];
  console_printf("  %-6s: source=%s", out_names[ch], src_names[c->source]);
  if (c->source == SRC_CC) console_printf(" %u", c->cc);
  console_printf("  range=%umV smooth=%ums  cal: zero=%d gain=%u.%02u codes/V\r\n", c->range_mv, c->smooth_ms,
                 c->cal_zero, c->cal_gain / 100, c->cal_gain % 100);
}

static void cmd_status(void) {
  const engine_status_t* s = engine_status();
  console_printf("MIDI-CV Alpha fw " FW_VERSION ", %s (id 0x%03X rev 0x%04X)\r\n", board_chip_name(), g_board.dev_id,
                 g_board.rev_id);
  if (g_board.hse_hz) console_printf("clock: crystal %lu MHz -> 120 MHz\r\n", (unsigned long) (g_board.hse_hz / 1000000u));
  else console_printf("clock: NO CRYSTAL, internal RC -> 120 MHz (USB may be unreliable)\r\n");
  if (g_settings.channel == CHANNEL_OMNI) console_printf("channel: omni\r\n");
  else console_printf("channel: %u\r\n", g_settings.channel + 1);
  console_printf("zero note: %u, bend range: %u, retrigger: %s, gate invert: %s\r\n", g_settings.zero_note,
                 g_settings.bend_range, g_settings.gate_retrigger ? "on" : "off", g_settings.gate_invert ? "on" : "off");
  console_printf("clock out: every %u MIDI clocks, %u ms pulse\r\n", g_settings.clock_div, g_settings.clock_pulse_ms);
  for (int ch = 0; ch < CV_OUT_COUNT; ch++) print_out_cfg(ch);
  console_printf("now: gate=%d note=%d vel=%u bend=%u dac=[%u %u] held=%u\r\n", s->gate, s->note, s->velocity, s->bend,
                 s->dac[0], s->dac[1], s->stack_len);
  console_printf("midi msgs=%lu din errors=%lu usb=%s\r\n", (unsigned long) s->msg_count,
                 (unsigned long) midi_uart_errors(), tud_mounted() ? "connected" : "not connected");
}

static void cmd_help(void) {
  console_printf(
    "commands:\r\n"
    "  status                     show everything\r\n"
    "  ch <1-16|omni>             MIDI channel\r\n"
    "  src <jack|filter> <pitch|vel|cc N|at|bend|off>   what drives an output\r\n"
    "  range <jack|filter> <mV>   voltage at value 127 (default 10000)\r\n"
    "  smooth <jack|filter> <ms>  smoothing 0..100 ms\r\n"
    "  zero <note>                MIDI note that gives 0 V (default 36 = C2)\r\n"
    "  bend <0-24>                pitch bend range, semitones\r\n"
    "  retrig <on|off>            gate re-trigger on legato notes\r\n"
    "  gateinv <on|off>           invert GATE output\r\n"
    "  clockdiv <1-96>            MIDI clocks per clock pulse (6 = 16ths)\r\n"
    "  set <jack|filter> <mV|off> force an output voltage (testing)\r\n"
    "  gate <on|off>              force the gate (testing)\r\n"
    "  cal <jack|filter> <mV@1V> <mV@8V>  calibrate from two measurements\r\n"
    "  learn <filter|jack|ch>     same as the LEARN button\r\n"
    "  save                       store settings in flash\r\n"
    "  defaults                   factory settings (then 'save')\r\n"
    "  panic                      all notes off\r\n"
    "  reboot\r\n"
    "  dfu                        restart into the USB firmware updater\r\n");
}

static bool set_source(int ch, char* name, char* arg) {
  cv_cfg_t* c = &g_settings.cv[ch];
  for (int i = 0; i < SRC_COUNT; i++) {
    if (name && !strcmp(name, src_names[i])) {
      if (i == SRC_CC) {
        long cc;
        if (!parse_int(arg, 0, 127, &cc)) return false;
        c->cc = (uint8_t) cc;
      }
      c->source = (uint8_t) i;
      return true;
    }
  }
  return false;
}

// Two-point calibration. "set <out> 1000" and "set <out> 8000" were measured
// with a multimeter as m1 and m2 millivolts; the codes for those requests
// under the current calibration are known, so solve for zero and gain.
static bool calibrate(int ch, long m1, long m2) {
  if (m2 <= m1 + 1000) return false;
  int32_t c1 = engine_mv_to_code((uint8_t) ch, 1000);
  int32_t c2 = engine_mv_to_code((uint8_t) ch, 8000);
  if (c2 <= c1) return false;
  // gain in codes per volt x100
  int64_t gain = ((int64_t) (c2 - c1) * 100000 + (m2 - m1) / 2) / (m2 - m1);
  if (gain < 20000 || gain > 65000) return false;
  int64_t zero = c1 - (gain * m1 + 50000) / 100000;
  if (zero < -2000 || zero > 2000) return false;
  g_settings.cv[ch].cal_gain = (uint16_t) gain;
  g_settings.cv[ch].cal_zero = (int16_t) zero;
  return true;
}

static void execute(char* cmdline) {
  char* argv[5] = {0};
  int argc = 0;
  for (char* p = cmdline; *p && argc < 5;) {
    while (*p == ' ' || *p == '\t') *p++ = 0;
    if (!*p) break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') p++;
  }
  if (argc == 0) return;

  const char* cmd = argv[0];
  long v, v2;
  int ch;
  bool ok = true, changed = false;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    cmd_help();
  } else if (!strcmp(cmd, "status") || !strcmp(cmd, "s")) {
    cmd_status();
  } else if (!strcmp(cmd, "ch")) {
    if (argv[1] && !strcmp(argv[1], "omni")) g_settings.channel = CHANNEL_OMNI;
    else if ((ok = parse_int(argv[1], 1, 16, &v))) g_settings.channel = (uint8_t) (v - 1);
    changed = ok;
  } else if (!strcmp(cmd, "src")) {
    ok = (ch = parse_out(argv[1])) >= 0 && set_source(ch, argv[2], argv[3]);
    changed = ok;
  } else if (!strcmp(cmd, "range")) {
    ok = (ch = parse_out(argv[1])) >= 0 && parse_int(argv[2], 100, 10500, &v);
    if (ok) g_settings.cv[ch].range_mv = (uint16_t) v;
    changed = ok;
  } else if (!strcmp(cmd, "smooth")) {
    ok = (ch = parse_out(argv[1])) >= 0 && parse_int(argv[2], 0, 100, &v);
    if (ok) g_settings.cv[ch].smooth_ms = (uint8_t) v;
    changed = ok;
  } else if (!strcmp(cmd, "zero")) {
    if ((ok = parse_int(argv[1], 0, 127, &v))) g_settings.zero_note = (uint8_t) v;
    changed = ok;
  } else if (!strcmp(cmd, "bend")) {
    if ((ok = parse_int(argv[1], 0, 24, &v))) g_settings.bend_range = (uint8_t) v;
    changed = ok;
  } else if (!strcmp(cmd, "retrig") || !strcmp(cmd, "gateinv")) {
    ok = argv[1] && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"));
    if (ok) {
      uint8_t on = !strcmp(argv[1], "on");
      if (!strcmp(cmd, "retrig")) g_settings.gate_retrigger = on;
      else g_settings.gate_invert = on;
    }
    changed = ok;
  } else if (!strcmp(cmd, "clockdiv")) {
    if ((ok = parse_int(argv[1], 1, 96, &v))) g_settings.clock_div = (uint8_t) v;
    changed = ok;
  } else if (!strcmp(cmd, "set")) {
    ok = (ch = parse_out(argv[1])) >= 0 && argv[2];
    if (ok && !strcmp(argv[2], "off")) engine_override((uint8_t) ch, -1);
    else if (ok && (ok = parse_int(argv[2], 0, 10500, &v))) {
      engine_override((uint8_t) ch, (int32_t) v);
      console_printf("%s = %ld mV (DAC code %u)\r\n", out_names[ch], v, engine_status()->dac[ch]);
    }
  } else if (!strcmp(cmd, "gate")) {
    ok = argv[1] && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"));
    if (ok) hal_gate_write(!strcmp(argv[1], "on") != (g_settings.gate_invert != 0));
  } else if (!strcmp(cmd, "cal")) {
    ok = (ch = parse_out(argv[1])) >= 0 && parse_int(argv[2], 0, 11000, &v) && parse_int(argv[3], 0, 11000, &v2) &&
         calibrate(ch, v, v2);
    if (ok) {
      console_printf("calibrated, check with 'set %s 1000' / 'set %s 8000', then 'save'\r\n", out_names[ch],
                     out_names[ch]);
      engine_override((uint8_t) ch, -1);
    }
    changed = ok;
  } else if (!strcmp(cmd, "learn")) {
    if (argv[1] && !strcmp(argv[1], "filter")) engine_learn(LEARN_CC_FILTER);
    else if (argv[1] && !strcmp(argv[1], "jack")) engine_learn(LEARN_CC_JACK);
    else if (argv[1] && !strcmp(argv[1], "ch")) engine_learn(LEARN_CHANNEL);
    else ok = false;
    if (ok) console_printf("waiting for MIDI (10 s)...\r\n");
  } else if (!strcmp(cmd, "save")) {
    ok = settings_save(&g_settings);
    console_printf(ok ? "saved\r\n" : "flash write FAILED\r\n");
    ok = true;
  } else if (!strcmp(cmd, "defaults")) {
    settings_defaults(&g_settings);
    changed = true;
    console_printf("factory settings loaded, type 'save' to keep them\r\n");
  } else if (!strcmp(cmd, "panic")) {
    engine_panic();
  } else if (!strcmp(cmd, "dfu")) {
    console_printf("restarting into the USB updater, run: dfu-util -a 0 -D midi_cv_alpha.bin\r\n");
    uint32_t t = g_ms;
    while (g_ms - t < 100) tud_task(); // let the message go out
    board_reboot_to_bootloader();
  } else if (!strcmp(cmd, "reboot")) {
    settings_save(&g_settings);
    board_reboot();
  } else {
    console_printf("unknown command, type 'help'\r\n");
    return;
  }

  if (!ok) {
    console_printf("bad arguments, type 'help'\r\n");
    return;
  }
  if (changed) {
    engine_settings_changed();
    console_printf("ok (not saved yet, type 'save')\r\n");
  }
}

static void input_byte(uint8_t b) {
  if (b == '\r' || b == '\n') {
    if (line_len == 0) return;
    out("\r\n", 2);
    line[line_len] = 0;
    line_len = 0;
    execute(line);
    out("> ", 2);
  } else if (b == 0x08 || b == 0x7F) {
    if (line_len) {
      line_len--;
      out("\b \b", 3);
    }
  } else if (b >= 0x20 && b < 0x7F && line_len < sizeof(line) - 1) {
    line[line_len++] = (char) b;
    out((const char*) &b, 1); // echo
  }
}

void console_print_banner(void) {
  console_printf("\r\n== MIDI-CV Alpha fw " FW_VERSION " == type 'help'\r\n> ");
}

void console_init(void) {
  line_len = 0;
  cdc_was_connected = false;
}

void console_poll(void) {
  uint8_t b;
  while (debug_uart_read(&b)) input_byte(b);

  bool conn = tud_cdc_connected();
  if (conn && !cdc_was_connected) console_print_banner();
  cdc_was_connected = conn;

  while (tud_cdc_available()) {
    uint8_t buf[32];
    uint32_t n = tud_cdc_read(buf, sizeof(buf));
    for (uint32_t i = 0; i < n; i++) input_byte(buf[i]);
  }
}
