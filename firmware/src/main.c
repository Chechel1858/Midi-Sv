// MIDI-CV/Gate firmware for the "MIDI Analog Filter" alpha board.
//
// DIN MIDI IN and USB MIDI are merged and drive:
//   J1 "CV" jack   - pitch 1 V/oct by default (or velocity / any CC ...)
//   filter cutoff  - CC74 by default (LEARN button re-assigns it)
//   GATE           - high while a note is held
//   CLOCK (J31)    - MIDI clock divided
// DIN MIDI IN is also forwarded to the computer over USB, USB MIDI from the
// computer goes out on USART2_TX (J19, future MIDI OUT).
#include "board.h"
#include "console.h"
#include "engine.h"
#include "midi_parser.h"
#include "settings.h"
#include "tusb.h"
#include "usb_hw.h"

static midi_parser_t din_parser, usb_parser;

static void usb_midi_send(const uint8_t* p, uint32_t n) {
  if (tud_midi_mounted()) tud_midi_stream_write(0, p, n);
}

static void process_din(void) {
  uint8_t b;
  while (midi_uart_read(&b)) {
    midi_msg_t m;
    bool sysex;
    if (midi_parser_feed(&din_parser, b, &m, &sysex)) {
      usb_midi_send(&m.status, 1);
      if (m.len > 1) usb_midi_send(m.data, m.len - 1u);
      engine_midi(&m);
    } else if (sysex) {
      usb_midi_send(&b, 1);
    }
  }
}

static void process_usb_midi(void) {
  uint8_t buf[64];
  while (tud_midi_available()) {
    uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
    if (n == 0) break;
    for (uint32_t i = 0; i < n; i++) {
      midi_uart_write(buf[i]);
      midi_msg_t m;
      bool sysex;
      if (midi_parser_feed(&usb_parser, buf[i], &m, &sysex)) engine_midi(&m);
    }
  }
}

int main(void) {
  board_init();

  bool loaded = settings_load(&g_settings);
  engine_init();

  midi_parser_init(&din_parser);
  midi_parser_init(&usb_parser);

  console_init();
  usb_hw_init();
  board_watchdog_init();

  console_print_banner();
  if (!loaded) console_printf("no saved settings, using defaults\r\n> ");

  uint32_t last_ms = g_ms;
  for (;;) {
    tud_task();
    process_din();
    process_usb_midi();
    console_poll();

    // 1 ms housekeeping, catching up if the loop was blocked (flash erase)
    uint32_t now = g_ms;
    while (last_ms != now) {
      last_ms++;
      engine_button(board_button_pressed());
      engine_tick_1ms();
    }

    if (engine_take_save_request()) {
      bool ok = settings_save(&g_settings);
      console_printf("\r\nlearned, %s. Type 'status' to see the settings\r\n> ", ok ? "saved" : "flash write FAILED");
    }

    board_watchdog_feed();
  }
}
