/* Application, starts after the bootloader and the settings sector
 * (see common/memory_map.h). Works on STM32F205RB / F405RG / F446RE.
 * RAM: 64 KB (smallest of the three), minus 16 bytes at the top that are
 * shared with the bootloader. */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x0800C000, LENGTH = 80K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K - 16
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x1000;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  ASSERT(_ebss + _min_stack <= _estack, "Not enough RAM for the stack")

  /DISCARD/ :
  {
    *(.ARM.attributes)
  }
}
