/*
 * RAM-resident flash routines. Everything declared here lives in .RamFunc (copied to
 * SRAM by the startup code) and calls nothing that lives in flash: the bootloader pages
 * are erased while these run, and the last routine erases the slot this updater runs
 * from. Interrupts must be masked by the caller.
 */
#ifndef FLASH_RAM_H
#define FLASH_RAM_H

#include <stdint.h>

typedef enum {
    FLASH_RAM_OK = 0,
    FLASH_RAM_ERASE_FAILED,
    FLASH_RAM_PROGRAM_FAILED,
    FLASH_RAM_VERIFY_MISMATCH,
} flash_ram_result_t;

typedef struct {
    uint32_t status;        /* FLASH->SR at the first error, if any */
    uint32_t error_offset;  /* byte offset (from 0x08000000) of the first failure */
} flash_ram_stats_t;

/*
 * Erase the bootloader pages, program `size` bytes from `src` at 0x08000000 and verify
 * by read-back, including that the rest of the bootloader region reads erased. `src`
 * is the embedded image in the slot: it is not touched by this routine.
 */
flash_ram_result_t flash_ram_rewrite_bootloader(const uint8_t *src, uint32_t size,
                                                flash_ram_stats_t *stats);

/*
 * Erase pages first..last except `keep` (pass a value outside the range for none),
 * then request a system reset. Never returns. Erases the page this updater runs from.
 */
void flash_ram_erase_pages_and_reset(uint32_t first, uint32_t last, uint32_t keep)
    __attribute__((noreturn));

#endif /* FLASH_RAM_H */
