// Flash sector geometry of STM32F2/F4 (no hardware access, also used by host tests)
#include "flash.h"

#define FLASH_BASE_ADDR 0x08000000u
#define FLASH_SECTORS   12 // up to 1 MB (F405RG); smaller parts simply have fewer

uint32_t flash_sector_size(int sector) {
  if (sector < 4) return 16u * 1024u;
  if (sector == 4) return 64u * 1024u;
  return 128u * 1024u;
}

uint32_t flash_sector_start(int sector) {
  uint32_t a = FLASH_BASE_ADDR;
  for (int s = 0; s < sector; s++) a += flash_sector_size(s);
  return a;
}

int flash_sector_of(uint32_t addr) {
  if (addr < FLASH_BASE_ADDR) return -1;
  for (int s = 0; s < FLASH_SECTORS; s++) {
    uint32_t start = flash_sector_start(s);
    if (addr < start + flash_sector_size(s)) return s;
  }
  return -1;
}
