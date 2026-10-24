#include "dfu_image.h"

#include <string.h>

#include "flash.h"
#include "memory_map.h"

// The first 8 bytes (stack pointer + reset vector) are held back until the
// whole image arrived, so an interrupted update leaves no bootable half-image.
static uint8_t first_words[8];
static bool have_first;
static uint32_t erased_mask; // sectors erased during this download

bool img_vectors_valid(const uint32_t* v) {
  uint32_t sp = v[0], reset = v[1];
  return sp >= 0x20000000u && sp <= 0x20020000u && (sp & 3u) == 0 &&
         (reset & 1u) && reset >= APP_BASE + 8u && reset < APP_END;
}

bool img_app_valid(void) {
  uint32_t v[2];
  memcpy(v, flash_ptr(APP_BASE), sizeof(v));
  return img_vectors_valid(v);
}

static bool ensure_erased(uint32_t addr, uint32_t len) {
  uint32_t a = addr;
  while (a < addr + len) {
    int s = flash_sector_of(a);
    if (s < 0) return false;
    if (!(erased_mask & (1u << s))) {
      img_feed_watchdog();
      if (!flash_erase_sector(s)) return false;
      erased_mask |= 1u << s;
      img_feed_watchdog();
    }
    a = flash_sector_start(s) + flash_sector_size(s);
  }
  return true;
}

img_status_t img_write_block(uint16_t block_num, const uint8_t* data, uint16_t length) {
  uint32_t addr = APP_BASE + (uint32_t) block_num * DFU_BLOCK_SIZE;

  if (block_num == 0) {
    // New download: forget what was erased, reject images that are not ours
    // (e.g. midi_cv_full.hex converted to bin, which starts with the bootloader)
    erased_mask = 0;
    have_first = false;
    uint32_t v[2];
    if (length < 8) return IMG_ERR_TARGET;
    memcpy(v, data, sizeof(v));
    if (!img_vectors_valid(v)) return IMG_ERR_TARGET;
  } else if (!have_first) {
    return IMG_ERR_ADDRESS; // blocks must start from 0
  }
  if (addr + length > APP_END) return IMG_ERR_ADDRESS;
  if (length == 0) return IMG_OK;

  const uint8_t* p = data;
  uint32_t a = addr, n = length;

  flash_unlock();
  bool ok = ensure_erased(addr, length);
  if (ok && block_num == 0) {
    memcpy(first_words, data, 8);
    have_first = true;
    p += 8;
    a += 8;
    n -= 8;
  }
  if (ok) ok = flash_program(a, p, n);
  flash_lock();

  if (!ok) return IMG_ERR_WRITE;
  if (memcmp(flash_ptr(a), p, n) != 0) return IMG_ERR_VERIFY;
  return IMG_OK;
}

img_status_t img_finish(void) {
  if (!have_first) return IMG_ERR_FIRMWARE;
  have_first = false;

  flash_unlock();
  bool ok = flash_program(APP_BASE, first_words, sizeof(first_words));
  flash_lock();

  if (!ok) return IMG_ERR_WRITE;
  if (memcmp(flash_ptr(APP_BASE), first_words, sizeof(first_words)) != 0) return IMG_ERR_VERIFY;
  return IMG_OK;
}

void img_abort(void) { have_first = false; }

uint16_t img_read_block(uint16_t block_num, uint8_t* data, uint16_t length) {
  uint32_t addr = APP_BASE + (uint32_t) block_num * DFU_BLOCK_SIZE;
  if (addr >= APP_END) return 0;
  uint32_t n = APP_END - addr;
  if (n > length) n = length;
  memcpy(data, flash_ptr(addr), n);
  return (uint16_t) n;
}
