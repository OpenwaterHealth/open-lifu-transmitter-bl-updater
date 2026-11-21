/*
 * Linker script: Open-LIFU transmitter bootloader updater, application-slot image.
 *
 * STM32L443RC, 256 KB flash. Installed into the slot by the secure bootloader's DFU
 * (signed header at 0x08010000, padded to 0x400) and executed at 0x08010400, like the
 * transmitter application. The image stays below page 96 (0x08030000), so it fits
 * the slot of bootloader 1.0.x (pages 32-125) and of 1.1.x (pages 32-95).
 *
 * RAM: SRAM1 (48 KB). .data holds .RamFunc, the flash code that must run while the
 * bootloader pages and the slot are being erased.
 */

ENTRY(Reset_Handler)

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 48K
  FLASH (rx)  : ORIGIN = 0x08010400, LENGTH = 127K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x400;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    KEEP(*(.updater_ident))
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.exidx (READONLY) :
  {
    *(.ARM.exidx*)
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  ._stack (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + _Min_Stack_Size;
  } >RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
