/*
 * RAM-resident bootloader rewrite for the STM32L443 (2 KB pages, 64-bit double-word
 * programming). See flash_ram.h.
 *
 * Why RAM: the core stalls on any flash fetch while the flash controller is busy, and
 * the bootloader armed the independent watchdog (about 8 s) before it launched us.
 * Running the wait loops from SRAM keeps the watchdog fed through the 95-page clean-up,
 * and lets the last routine erase the slot this image runs from.
 *
 * Compiled with -fno-builtin -fno-tree-loop-distribute-patterns -fno-jump-tables so
 * the loops cannot become memcpy/memset calls or table jumps into flash; checked at
 * build time by scripts/check_ramfunc.sh.
 */
#include "flash_ram.h"
#include "stm32l4xx.h"
#include "updater_layout.h"

#define RAMFUNC __attribute__((section(".RamFunc"), noinline, used))

#define UNLOCK_KEY1     0x45670123UL
#define UNLOCK_KEY2     0xCDEF89ABUL
#define SR_ERRORS       (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | \
                         FLASH_SR_RDERR | FLASH_SR_OPTVERR)
#define SR_CLEAR        (SR_ERRORS | FLASH_SR_EOP)
#define IWDG_RELOAD_KEY 0x0000AAAAUL
/* Finite bound on every busy wait: far beyond the datasheet page-erase time (about
 * 25 ms) at any core clock the bootloader may have left, but not forever. */
#define WAIT_LIMIT      0x02000000UL

RAMFUNC static int ram_wait_idle(void)
{
    uint32_t n = 0U;
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
        IWDG->KR = IWDG_RELOAD_KEY;
        if (++n > WAIT_LIMIT) {
            return 0;
        }
    }
    return 1;
}

RAMFUNC static void ram_unlock(void)
{
    (void)ram_wait_idle();
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = UNLOCK_KEY1;
        FLASH->KEYR = UNLOCK_KEY2;
    }
    FLASH->SR = SR_CLEAR;
}

RAMFUNC static int ram_erase_page(uint32_t page)
{
    int ok;
    (void)ram_wait_idle();
    FLASH->SR = SR_CLEAR;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PNB | FLASH_CR_PG)) |
                FLASH_CR_PER | ((page << FLASH_CR_PNB_Pos) & FLASH_CR_PNB);
    FLASH->CR |= FLASH_CR_STRT;
    ok = ram_wait_idle();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
    if ((FLASH->SR & SR_ERRORS) != 0U) {
        ok = 0;
    }
    FLASH->SR = SR_CLEAR;
    return ok;
}

/* Flush the instruction and data caches so the read-back sees the new contents. */
RAMFUNC static void ram_flush_caches(void)
{
    uint32_t acr = FLASH->ACR;
    FLASH->ACR = acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR = (acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN)) | FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR = acr & ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
}

RAMFUNC static uint32_t ram_word(const uint8_t *src, uint32_t size, uint32_t off)
{
    uint32_t w = 0U;
    for (uint32_t k = 0U; k < 4U; k++) {
        uint32_t byte = (off + k < size) ? src[off + k] : 0xFFU;
        w |= byte << (8U * k);
    }
    return w;
}

RAMFUNC flash_ram_result_t flash_ram_rewrite_bootloader(const uint8_t *src, uint32_t size,
                                                        flash_ram_stats_t *stats)
{
    const volatile uint8_t *dst8 = (const volatile uint8_t *)FLASH_BASE_ADDR;
    uint32_t padded = (size + 7U) & ~7U;

    stats->status = 0U;
    stats->error_offset = 0U;
    ram_unlock();

    /* ---- erase the bootloader pages ---- */
    for (uint32_t page = BL_FIRST_PAGE; page <= BL_LAST_PAGE; page++) {
        if (!ram_erase_page(page)) {
            stats->status = FLASH->SR;
            stats->error_offset = page * FLASH_PAGE_SIZE;
            FLASH->CR |= FLASH_CR_LOCK;
            return FLASH_RAM_ERASE_FAILED;
        }
    }

    /* ---- program, one 64-bit double word at a time (tail padded with 0xFF) ---- */
    FLASH->SR = SR_CLEAR;
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t off = 0U; off < padded; off += 8U) {
        volatile uint32_t *dst = (volatile uint32_t *)(FLASH_BASE_ADDR + off);
        uint32_t lo = ram_word(src, size, off);
        uint32_t hi = ram_word(src, size, off + 4U);
        IWDG->KR = IWDG_RELOAD_KEY;
        dst[0] = lo;
        __ISB();
        dst[1] = hi;
        if (!ram_wait_idle() || (FLASH->SR & SR_ERRORS) != 0U) {
            stats->status = FLASH->SR;
            stats->error_offset = off;
            FLASH->SR = SR_CLEAR;
            FLASH->CR &= ~FLASH_CR_PG;
            FLASH->CR |= FLASH_CR_LOCK;
            return FLASH_RAM_PROGRAM_FAILED;
        }
        FLASH->SR = SR_CLEAR;
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    ram_flush_caches();

    /* ---- verify by read-back; the rest of the region must read erased ---- */
    for (uint32_t i = 0U; i < BL_REGION_SIZE; i++) {
        uint8_t want = (i < size) ? src[i] : 0xFFU;
        if ((i & 0x3FFU) == 0U) {
            IWDG->KR = IWDG_RELOAD_KEY;
        }
        if (dst8[i] != want) {
            stats->error_offset = i;
            return FLASH_RAM_VERIFY_MISMATCH;
        }
    }
    return FLASH_RAM_OK;
}

RAMFUNC void flash_ram_erase_pages_and_reset(uint32_t first, uint32_t last, uint32_t keep)
{
    ram_unlock();
    for (uint32_t page = first; page <= last; page++) {
        if (page != keep) {
            (void)ram_erase_page(page);   /* bounded; on failure we reset anyway */
        }
    }
    FLASH->CR |= FLASH_CR_LOCK;
    __DSB();
    SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for (;;) {
        IWDG->KR = IWDG_RELOAD_KEY;
    }
}
