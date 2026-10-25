#include "sysex.h"

#include <string.h>

#include "engine.h"
#include "params.h"
#include "settings.h"
#include "version.h"

static bool cal_unlocked;

uint8_t sysex_crc7(const uint8_t* p, uint32_t n) {
  uint8_t crc = 0;
  while (n--) {
    uint8_t b = *p++;
    for (int i = 7; i >= 0; i--) {
      uint8_t in = (uint8_t) (((b >> i) & 1u) ^ ((crc >> 6) & 1u));
      crc = (uint8_t) ((crc << 1) & 0x7Fu);
      if (in) crc ^= 0x09u;
    }
  }
  return crc;
}

static void put_value(uint8_t* p, int32_t v) {
  uint32_t u = (uint32_t) v & 0x1FFFFFu;
  p[0] = (uint8_t) ((u >> 14) & 0x7Fu);
  p[1] = (uint8_t) ((u >> 7) & 0x7Fu);
  p[2] = (uint8_t) (u & 0x7Fu);
}

static int32_t get_value(const uint8_t* p) {
  uint32_t u = ((uint32_t) p[0] << 14) | ((uint32_t) p[1] << 7) | p[2];
  return (u & 0x100000u) ? (int32_t) u - 0x200000 : (int32_t) u;
}

uint32_t sysex_build(uint8_t* out, uint8_t cmd, uint8_t param, const int32_t* value) {
  uint32_t n = 0;
  out[n++] = 0xF0;
  out[n++] = SYSEX_MANUFACTURER;
  out[n++] = SYSEX_PRODUCT;
  out[n++] = SYSEX_DEVICE;
  out[n++] = cmd & 0x7F;
  out[n++] = param & 0x7F;
  out[n++] = value ? 3 : 0;
  if (value) {
    put_value(&out[n], *value);
    n += 3;
  }
  out[n] = sysex_crc7(&out[2], n - 2);
  n++;
  out[n++] = 0xF7;
  return n;
}

static void send(sysex_reply_fn reply, void* ctx, uint8_t cmd, uint8_t param, const int32_t* value) {
  uint8_t out[16];
  uint32_t n = sysex_build(out, cmd, param, value);
  reply(out, n, ctx);
}

static void ack(sysex_reply_fn reply, void* ctx, uint8_t cmd) { send(reply, ctx, SX_ACK, cmd, NULL); }

static void nack(sysex_reply_fn reply, void* ctx, uint8_t cmd, int32_t err) { send(reply, ctx, SX_NACK, cmd, &err); }

static void value(sysex_reply_fn reply, void* ctx, uint8_t id, int32_t v) { send(reply, ctx, SX_VALUE, id, &v); }

void sysex_handle(const uint8_t* msg, uint32_t len, sysex_reply_fn reply, void* ctx) {
  // F0 7D 4D dev cmd param len [data] crc F7
  if (len < 9 || msg[0] != 0xF0 || msg[len - 1] != 0xF7) return;
  if (msg[1] != SYSEX_MANUFACTURER || msg[2] != SYSEX_PRODUCT) return;
  if (msg[3] != SYSEX_DEVICE && msg[3] != SYSEX_BROADCAST) return;

  uint8_t cmd = msg[4], param = msg[5], dlen = msg[6];
  if (cmd == SX_VALUE || cmd == SX_ACK || cmd == SX_NACK) return; // our own replies looped back
  if ((uint32_t) dlen + 9 != len) {
    nack(reply, ctx, cmd, SX_ERR_LENGTH);
    return;
  }
  if (sysex_crc7(&msg[2], 5u + dlen) != msg[7 + dlen]) {
    nack(reply, ctx, cmd, SX_ERR_CRC);
    return;
  }
  const uint8_t* data = &msg[7];

  switch (cmd) {
    case SX_GET: {
      if (param == SX_ID_PROTOCOL) {
        value(reply, ctx, param, SYSEX_PROTOCOL);
        return;
      }
      const param_t* p = param_by_id(param);
      if (!p) {
        nack(reply, ctx, cmd, SX_ERR_PARAM);
        return;
      }
      value(reply, ctx, param, param_get(&g_settings, p));
      return;
    }

    case SX_SET:
    case SX_CAL_SET: {
      const param_t* p = param_by_id(param);
      if (!p || (p->flags & PARAM_READONLY)) {
        nack(reply, ctx, cmd, SX_ERR_PARAM);
        return;
      }
      if (dlen != 3) {
        nack(reply, ctx, cmd, SX_ERR_LENGTH);
        return;
      }
      if ((p->flags & PARAM_CAL) && !cal_unlocked) {
        nack(reply, ctx, cmd, SX_ERR_LOCKED);
        return;
      }
      if (!param_set(&g_settings, p, get_value(data))) {
        nack(reply, ctx, cmd, SX_ERR_RANGE);
        return;
      }
      engine_settings_changed(); // applied in RAM right away, SAVE stores it
      ack(reply, ctx, cmd);
      return;
    }

    case SX_SAVE:
      if (settings_save(&g_settings)) ack(reply, ctx, cmd);
      else nack(reply, ctx, cmd, SX_ERR_FLASH);
      return;

    case SX_DUMP:
      value(reply, ctx, SX_ID_PROTOCOL, SYSEX_PROTOCOL);
      value(reply, ctx, SX_ID_FW_VERSION, FW_VERSION_NUM);
      for (unsigned i = 0; i < param_count(); i++) {
        const param_t* p = param_at(i);
        value(reply, ctx, p->id, param_get(&g_settings, p));
      }
      ack(reply, ctx, cmd);
      return;

    case SX_FACTORY_RESET:
      settings_defaults(&g_settings);
      engine_settings_changed();
      ack(reply, ctx, cmd);
      return;

    case SX_CAL_UNLOCK:
      // There is no SERVICE jumper on the alpha board: the key "CAL" unlocks
      // calibration parameters until the next reset.
      if (dlen == 3 && data[0] == 'C' && data[1] == 'A' && data[2] == 'L') {
        cal_unlocked = true;
        ack(reply, ctx, cmd);
      } else {
        nack(reply, ctx, cmd, SX_ERR_LOCKED);
      }
      return;

    case SX_DIAG: {
      sysex_diag_t d;
      sysex_get_diag(&d);
      value(reply, ctx, SX_ID_FW_VERSION, FW_VERSION_NUM);
      value(reply, ctx, SX_ID_CHIP, d.chip_id);
      value(reply, ctx, SX_ID_HSE_MHZ, d.hse_mhz);
      value(reply, ctx, SX_ID_MIDI_MSGS, (int32_t) (engine_status()->msg_count & 0xFFFFFu));
      value(reply, ctx, SX_ID_DIN_ERRORS, (int32_t) (d.din_errors & 0xFFFFFu));
      value(reply, ctx, SX_ID_RESET_FLAGS, (int32_t) (d.reset_flags >> 24));
      ack(reply, ctx, cmd);
      return;
    }

    default:
      nack(reply, ctx, cmd, SX_ERR_COMMAND);
      return;
  }
}

void sysex_rx_abort(sysex_rx_t* rx) {
  rx->active = false;
  rx->len = 0;
}

void sysex_rx_byte(sysex_rx_t* rx, uint8_t byte, sysex_reply_fn reply, void* ctx) {
  if (byte == 0xF0) {
    rx->active = true;
    rx->overflow = false;
    rx->len = 0;
  }
  if (!rx->active) return;
  if (rx->len < sizeof(rx->buf)) rx->buf[rx->len++] = byte;
  else rx->overflow = true;
  if (byte == 0xF7) {
    if (!rx->overflow) sysex_handle(rx->buf, rx->len, reply, ctx);
    sysex_rx_abort(rx);
  }
}
