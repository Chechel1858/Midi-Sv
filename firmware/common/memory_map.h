// Flash/RAM layout shared by the bootloader and the application.
//
//   0x08000000  sectors 0-1  32 KB  USB DFU bootloader
//   0x08008000  sector 2     16 KB  settings
//   0x0800C000  sectors 3-4  80 KB  application (up to 0x08020000, fits F205RB too;
//                                    on F446RE it could grow to 0x08080000)
//
//   0x2000FFF0  16 bytes at the top of RAM, not touched by either startup code:
//               the application leaves a magic word there before resetting
//               to ask the bootloader to stay in DFU mode.
#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#define BOOTLOADER_BASE        0x08000000u
#define SETTINGS_FLASH_ADDR    0x08008000u
#define SETTINGS_FLASH_SECTOR  2u
#define APP_BASE               0x0800C000u
#define APP_END                0x08020000u

#define BOOT_REQUEST_ADDR      0x2000FFF0u
#define BOOT_REQUEST_DFU       0xB007DF11u

#define BOOT_REQUEST (*(volatile unsigned int*) BOOT_REQUEST_ADDR)

#endif
