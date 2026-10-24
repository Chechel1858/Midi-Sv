#include "flash.h"

#include <string.h>

#include "stm32f4xx.h"

static bool flash_wait(void) {
  while (FLASH->SR & FLASH_SR_BSY) {}
  return (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) == 0;
}

void flash_unlock(void) {
  if (FLASH->CR & FLASH_CR_LOCK) {
    FLASH->KEYR = 0x45670123u;
    FLASH->KEYR = 0xCDEF89ABu;
  }
  flash_wait();
  FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
}

void flash_lock(void) {
  FLASH->CR = FLASH_CR_LOCK;
  FLASH->ACR &= ~FLASH_ACR_DCEN;
  FLASH->ACR |= FLASH_ACR_DCRST;
  FLASH->ACR &= ~FLASH_ACR_DCRST;
  FLASH->ACR |= FLASH_ACR_DCEN;
}

bool flash_erase_sector(int sector) {
  FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_SER | ((uint32_t) sector << FLASH_CR_SNB_Pos);
  FLASH->CR |= FLASH_CR_STRT;
  bool ok = flash_wait();
  FLASH->CR = 0;
  return ok;
}

const void* flash_ptr(uint32_t addr) { return (const void*) addr; }

bool flash_program(uint32_t addr, const void* data, uint32_t len) {
  const uint8_t* p = (const uint8_t*) data;
  bool ok = true;
  FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;
  for (uint32_t off = 0; off < len && ok; off += 4) {
    uint32_t w = 0xFFFFFFFFu;
    memcpy(&w, p + off, (len - off) >= 4 ? 4 : (len - off));
    if (w != 0xFFFFFFFFu) { // erased flash already reads 0xFF
      *(volatile uint32_t*) (addr + off) = w;
      ok = flash_wait();
    }
  }
  FLASH->CR = 0;
  return ok;
}
