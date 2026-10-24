#include "midi_parser.h"

void midi_parser_init(midi_parser_t* p) {
  p->running = 0;
  p->count = 0;
  p->needed = 0;
  p->in_sysex = false;
}

uint8_t midi_data_len(uint8_t status) {
  if (status < 0xF0) {
    switch (status & 0xF0) {
      case 0xC0: // program change
      case 0xD0: // channel pressure
        return 1;
      default:
        return 2;
    }
  }
  switch (status) {
    case 0xF1: // MTC quarter frame
    case 0xF3: // song select
      return 1;
    case 0xF2: // song position
      return 2;
    default:
      return 0;
  }
}

bool midi_parser_feed(midi_parser_t* p, uint8_t byte, midi_msg_t* out, bool* sysex) {
  *sysex = false;

  // Realtime: single byte, may appear anywhere, does not touch running status
  if (byte >= 0xF8) {
    if (byte == 0xF9 || byte == 0xFD) return false; // undefined
    out->status = byte;
    out->len = 1;
    return true;
  }

  if (byte & 0x80) {
    if (byte == 0xF0) {
      p->in_sysex = true;
      p->running = 0;
      *sysex = true;
      return false;
    }
    if (byte == 0xF7) {
      if (p->in_sysex) *sysex = true;
      p->in_sysex = false;
      return false;
    }

    // Any other status byte terminates SysEx
    p->in_sysex = false;
    p->count = 0;

    if (byte >= 0xF0) {
      // System common clears running status
      p->running = 0;
      uint8_t n = midi_data_len(byte);
      if (n == 0) {
        if (byte == 0xF4 || byte == 0xF5) return false; // undefined
        out->status = byte;
        out->len = 1;
        return true;
      }
      p->running = byte; // collect its data bytes, then drop it
      p->needed = n;
      return false;
    }

    p->running = byte;
    p->needed = midi_data_len(byte);
    return false;
  }

  // Data byte
  if (p->in_sysex) {
    *sysex = true;
    return false;
  }
  if (p->running == 0) return false; // stray data byte

  p->data[p->count++] = byte;
  if (p->count < p->needed) return false;

  out->status = p->running;
  out->data[0] = p->data[0];
  out->data[1] = p->needed > 1 ? p->data[1] : 0;
  out->len = (uint8_t) (1 + p->needed);
  p->count = 0;
  if (p->running >= 0xF0) p->running = 0; // system common has no running status
  return true;
}
