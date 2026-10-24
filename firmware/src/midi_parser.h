// Byte-stream MIDI 1.0 parser: running status, realtime bytes interleaved
// anywhere, SysEx passed through as raw bytes.
#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t status;   // 0x80..0xFF
  uint8_t data[2];
  uint8_t len;      // total length including status (1..3)
} midi_msg_t;

typedef struct {
  uint8_t running;  // running status (channel messages only)
  uint8_t data[2];
  uint8_t count;    // data bytes collected
  uint8_t needed;   // data bytes required by the current status
  bool in_sysex;
} midi_parser_t;

void midi_parser_init(midi_parser_t* p);

// Feeds one byte. Returns true and fills *out when a complete short
// message (channel, system common or realtime) is available.
// SysEx bytes (F0..F7) are not reported as messages; *sysex is set
// to true for them so the caller can forward them raw.
bool midi_parser_feed(midi_parser_t* p, uint8_t byte, midi_msg_t* out, bool* sysex);

// Number of data bytes that follow a given status byte.
uint8_t midi_data_len(uint8_t status);

#endif
