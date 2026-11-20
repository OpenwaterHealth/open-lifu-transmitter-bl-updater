/*
 * Flash layout the updater works with: Open-LIFU transmitter, STM32L443RC
 * (256 KB flash, 128 pages of 2 KB, double-word programming).
 *
 *   page   0-31   0x08000000   64K  secure bootloader (rewritten by this updater)
 *   page  32-105  0x08010000  148K  application slot (this updater runs here, at +0x400)
 *   page 106-125  0x08035000   40K  presets, 10 x 4 KB (bootloader >= 1.1.0); slot before
 *   page 126      0x0803F000    2K  anti-rollback floor (both bootloaders, same format)
 *   page 127      0x0803F800    2K  LIFU runtime config: KEPT (outside the cleaned range)
 *
 * Bootloader 1.0.1 (fielded with app 2.0.8) has a 188K slot, pages 32-125; 1.1.0 moves
 * pages 106-125 to the presets. The updater erases pages 32-126 before it resets, so
 * the new bootloader starts with an empty slot, blank preset slots and no floor.
 */
#ifndef UPDATER_LAYOUT_H
#define UPDATER_LAYOUT_H

#define FLASH_BASE_ADDR      0x08000000UL
#define FLASH_PAGE_SIZE      0x800UL
#define FLASH_PAGE_COUNT     128UL

#define BL_FIRST_PAGE        0UL
#define BL_LAST_PAGE         31UL
#define BL_REGION_SIZE       ((BL_LAST_PAGE - BL_FIRST_PAGE + 1UL) * FLASH_PAGE_SIZE)  /* 64 KB */

/* Pages erased after the bootloader is in place: the old slot, the new preset slots
 * and the anti-rollback floor. Page 127 (runtime config) is outside the range. */
#define CLEAN_FIRST_PAGE     32UL
#define CLEAN_LAST_PAGE      126UL
#define CLEAN_KEEP_PAGE      0xFFFFFFFFUL     /* none */

/* The embedded bootloader's info block (bootloader >= 1.1.0): 'OWBI' at this offset. */
#define BL_INFO_OFFSET       0x1E0UL
#define BL_INFO_MAGIC        0x4942574FUL     /* "OWBI" little-endian */

/* SRAM1: 48 KB at 0x20000000. */
#define SRAM_BASE_ADDR       0x20000000UL
#define SRAM_END_ADDR        0x2000C000UL

#endif /* UPDATER_LAYOUT_H */
