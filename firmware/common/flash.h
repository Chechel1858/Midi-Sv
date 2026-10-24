// Internal flash programming for STM32F2/F4 (x32 parallelism, 2.7..3.6 V).
#ifndef FLASH_H
#define FLASH_H

#include <stdbool.h>
#include <stdint.h>

// Sector number containing addr (F2/F4 layout: 4x16K, 64K, 128K...), -1 if outside flash
int flash_sector_of(uint32_t addr);
uint32_t flash_sector_start(int sector);
uint32_t flash_sector_size(int sector);

void flash_unlock(void);
void flash_lock(void); // also drops stale data cache lines
bool flash_erase_sector(int sector);
// addr must be 4-byte aligned; a tail shorter than 4 bytes is padded with 0xFF
bool flash_program(uint32_t addr, const void* data, uint32_t len);

// Pointer for reading flash contents at addr (identity on the MCU, a buffer in host tests)
const void* flash_ptr(uint32_t addr);

#endif
