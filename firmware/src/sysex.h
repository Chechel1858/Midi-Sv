// SysEx configuration protocol (spec section 20), see docs/SYSEX.md.
//
//   F0 7D 4D <dev> <cmd> <param> <len> <data...> <crc7> F7
//
// 7D = development manufacturer id, 4D = product ("M"), dev = 00 (or 7F
// broadcast). Values are sent as 3 data bytes: a 21-bit two's complement
// number, most significant 7 bits first. crc7 = CRC-7 (x^7 + x^3 + 1,
// as used by SD cards) over everything from the product byte to the last
// data byte. Replies come back on the interface the request came from.
#ifndef SYSEX_H
#define SYSEX_H

#include <stdbool.h>
#include <stdint.h>

#define SYSEX_MANUFACTURER 0x7D
#define SYSEX_PRODUCT      0x4D
#define SYSEX_DEVICE       0x00
#define SYSEX_BROADCAST    0x7F
#define SYSEX_PROTOCOL     1

enum {
  SX_GET = 0x01,
  SX_SET = 0x02,
  SX_SAVE = 0x03,
  SX_DUMP = 0x04,
  SX_FACTORY_RESET = 0x05,
  SX_CAL_UNLOCK = 0x06,
  SX_CAL_SET = 0x07,
  SX_DIAG = 0x08,
  SX_VALUE = 0x41, // reply: param + value
  SX_ACK = 0x7E,   // reply: param = command
  SX_NACK = 0x7F,  // reply: param = command, data = error code
};

enum {
  SX_ERR_CRC = 1,
  SX_ERR_COMMAND = 2,
  SX_ERR_PARAM = 3,
  SX_ERR_RANGE = 4,
  SX_ERR_LOCKED = 5,
  SX_ERR_LENGTH = 6,
  SX_ERR_FLASH = 7,
};

// Read-only values reported by DIAG (and the protocol version by DUMP)
enum {
  SX_ID_PROTOCOL = 0x00,
  SX_ID_FW_VERSION = 0x70,
  SX_ID_CHIP = 0x71,
  SX_ID_HSE_MHZ = 0x72,
  SX_ID_MIDI_MSGS = 0x73,
  SX_ID_DIN_ERRORS = 0x74,
  SX_ID_RESET_FLAGS = 0x75,
};

typedef void (*sysex_reply_fn)(const uint8_t* data, uint32_t len, void* ctx);

typedef struct {
  uint8_t buf[48];
  uint8_t len;
  bool active;
  bool overflow;
} sysex_rx_t;

// Feed bytes the MIDI parser flagged as SysEx; a complete F0..F7 message
// is handled right away.
void sysex_rx_byte(sysex_rx_t* rx, uint8_t byte, sysex_reply_fn reply, void* ctx);
// A non-realtime status byte interrupted the message: drop it
void sysex_rx_abort(sysex_rx_t* rx);

// Handles one complete message (F0 ... F7). Messages for other devices or
// products are ignored silently.
void sysex_handle(const uint8_t* msg, uint32_t len, sysex_reply_fn reply, void* ctx);

uint8_t sysex_crc7(const uint8_t* p, uint32_t n);

// Builds a message into out (at least 12 bytes), returns its length
uint32_t sysex_build(uint8_t* out, uint8_t cmd, uint8_t param, const int32_t* value);

// Provided by the firmware (board info), stubbed by the tests
typedef struct {
  uint16_t chip_id;
  uint16_t hse_mhz;
  uint32_t din_errors;
  uint32_t reset_flags;
} sysex_diag_t;
void sysex_get_diag(sysex_diag_t* d);

#endif
