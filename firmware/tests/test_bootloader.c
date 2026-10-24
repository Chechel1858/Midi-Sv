// Host-side tests for the bootloader's image writer (bootloader/dfu_image.c).
// Flash is emulated in RAM with real STM32 semantics: erase sets 0xFF,
// programming can only clear bits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bootloader/dfu_image.h"
#include "../common/flash.h"
#include "../common/memory_map.h"

#define FLASH_BASE 0x08000000u
#define FLASH_SIZE (512u * 1024u) // STM32F446RE

static uint8_t flash[FLASH_SIZE];
static bool unlocked;
static int erase_count[12];
static int fail_program_after = -1; // simulate a write error after N programmed words

const void* flash_ptr(uint32_t addr) { return &flash[addr - FLASH_BASE]; }
void flash_unlock(void) { unlocked = true; }
void flash_lock(void) { unlocked = false; }
void img_feed_watchdog(void) {}

bool flash_erase_sector(int sector) {
  if (!unlocked || sector < 0 || sector > 7) return false;
  memset(&flash[flash_sector_start(sector) - FLASH_BASE], 0xFF, flash_sector_size(sector));
  erase_count[sector]++;
  return true;
}

bool flash_program(uint32_t addr, const void* data, uint32_t len) {
  if (!unlocked || (addr & 3u)) return false;
  const uint8_t* p = data;
  for (uint32_t i = 0; i < len; i++) {
    if (fail_program_after == 0) return false;
    if ((i & 3u) == 0 && fail_program_after > 0) fail_program_after--;
    flash[addr - FLASH_BASE + i] &= p[i]; // flash can only clear bits
  }
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

// A fake application image: valid vectors, recognisable payload
static uint8_t* make_image(uint32_t size, uint8_t seed) {
  uint8_t* img = malloc(size);
  for (uint32_t i = 0; i < size; i++) img[i] = (uint8_t) (i * 7 + seed);
  uint32_t vec[2] = {0x2000FFF0u, APP_BASE + 0x1001u};
  memcpy(img, vec, 8);
  return img;
}

// Sends an image like dfu-util does: 1024-byte blocks from 0, then manifest
static img_status_t download(const uint8_t* img, uint32_t size, int stop_after_blocks) {
  uint16_t block = 0;
  for (uint32_t off = 0; off < size; off += DFU_BLOCK_SIZE, block++) {
    if (stop_after_blocks >= 0 && block == stop_after_blocks) return IMG_OK; // cable pulled
    uint32_t n = size - off < DFU_BLOCK_SIZE ? size - off : DFU_BLOCK_SIZE;
    img_status_t s = img_write_block(block, img + off, (uint16_t) n);
    if (s != IMG_OK) return s;
  }
  return img_finish();
}

static bool app_matches(const uint8_t* img, uint32_t size) {
  return memcmp(flash_ptr(APP_BASE), img, size) == 0;
}

static void blank_chip(void) {
  memset(flash, 0xFF, sizeof(flash));
  memset(erase_count, 0, sizeof(erase_count));
  fail_program_after = -1;
  img_abort();
}

// ---------------------------------------------------------------------------

static void test_layout(void) {
  CHECK(flash_sector_of(BOOTLOADER_BASE) == 0);
  CHECK(flash_sector_of(0x08007FFFu) == 1);
  CHECK(flash_sector_of(SETTINGS_FLASH_ADDR) == (int) SETTINGS_FLASH_SECTOR);
  CHECK(flash_sector_of(APP_BASE) == 3);
  CHECK(flash_sector_of(0x08010000u) == 4);
  CHECK(flash_sector_of(APP_END - 1) == 4);
  CHECK(flash_sector_of(0x08020000u) == 5);
  CHECK(flash_sector_of(0x07FFFFFFu) == -1);
}

static void test_blank_chip_and_full_update(void) {
  blank_chip();
  CHECK(!img_app_valid());

  uint32_t size = 26028; // about the size of the real application
  uint8_t* img = make_image(size, 1);
  CHECK(download(img, size, -1) == IMG_OK);
  CHECK(app_matches(img, size));
  CHECK(img_app_valid());
  CHECK(erase_count[3] == 1 && erase_count[4] == 1);
  CHECK(erase_count[0] == 0 && erase_count[1] == 0 && erase_count[2] == 0); // bootloader and settings untouched

  // Read back (dfu-util -U)
  uint8_t buf[DFU_BLOCK_SIZE];
  CHECK(img_read_block(0, buf, sizeof(buf)) == DFU_BLOCK_SIZE && memcmp(buf, img, DFU_BLOCK_SIZE) == 0);
  CHECK(img_read_block((APP_END - APP_BASE) / DFU_BLOCK_SIZE, buf, sizeof(buf)) == 0);
  free(img);
}

static void test_interrupted_update_is_not_bootable(void) {
  blank_chip();
  uint32_t size = 20000;
  uint8_t* old_img = make_image(size, 2);
  CHECK(download(old_img, size, -1) == IMG_OK);
  CHECK(img_app_valid());

  // New download, cable pulled after 5 blocks: the old image is gone and the
  // new one must not look valid, so the bootloader stays in DFU mode
  uint8_t* new_img = make_image(size, 3);
  CHECK(download(new_img, size, 5) == IMG_OK);
  CHECK(!img_app_valid());

  // Retry succeeds
  img_abort();
  CHECK(download(new_img, size, -1) == IMG_OK);
  CHECK(img_app_valid());
  CHECK(app_matches(new_img, size));
  free(old_img);
  free(new_img);
}

static void test_rejects_foreign_images(void) {
  blank_chip();
  uint32_t size = 4096;
  uint8_t* img = make_image(size, 4);

  // Image linked at 0x08000000 (e.g. the bootloader or the full hex as bin)
  uint32_t vec[2] = {0x2000FFF0u, 0x080006D1u};
  memcpy(img, vec, 8);
  CHECK(download(img, size, -1) == IMG_ERR_TARGET);
  CHECK(!img_app_valid());

  // Garbage
  memset(img, 0x55, size);
  CHECK(download(img, size, -1) == IMG_ERR_TARGET);

  // Too large for the application area
  uint32_t big = APP_END - APP_BASE + DFU_BLOCK_SIZE;
  uint8_t* huge = make_image(big, 5);
  CHECK(download(huge, big, -1) == IMG_ERR_ADDRESS);
  CHECK(!img_app_valid());

  // Manifest without any download
  img_abort();
  CHECK(img_finish() == IMG_ERR_FIRMWARE);

  // Blocks not starting at 0
  CHECK(img_write_block(3, img, DFU_BLOCK_SIZE) == IMG_ERR_ADDRESS);
  free(img);
  free(huge);
}

static void test_write_error(void) {
  blank_chip();
  uint32_t size = 8192;
  uint8_t* img = make_image(size, 6);
  fail_program_after = 300;
  CHECK(download(img, size, -1) == IMG_ERR_WRITE);
  CHECK(!img_app_valid());
  fail_program_after = -1;
  img_abort();
  CHECK(download(img, size, -1) == IMG_OK);
  CHECK(img_app_valid());
  free(img);
}

static void test_odd_sizes(void) {
  blank_chip();
  uint32_t size = 3 * DFU_BLOCK_SIZE + 3; // last block not a multiple of 4
  uint8_t* img = make_image(size, 7);
  CHECK(download(img, size, -1) == IMG_OK);
  CHECK(app_matches(img, size));
  free(img);

  // Second update in the same session re-erases
  uint8_t* img2 = make_image(size, 8);
  CHECK(download(img2, size, -1) == IMG_OK);
  CHECK(app_matches(img2, size));
  CHECK(erase_count[3] == 2);
  free(img2);
}

int main(void) {
  test_layout();
  test_blank_chip_and_full_update();
  test_interrupted_update_is_not_bootable();
  test_rejects_foreign_images();
  test_write_error();
  test_odd_sizes();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
