/*
 * open-lifu-transmitter-bl-updater: main.
 *
 * One job: replace the secure bootloader (pages 0-31) with the bootloader embedded in
 * this image, clean the pages the new bootloader expects blank (old slot, preset pages,
 * anti-rollback floor) and reset, so that the NEW bootloader starts in DFU and the host
 * installs the application: USB DFU on the master module, I2C DFU (through the master)
 * on a slave module.
 *
 * Runs as an ordinary application launched by open-lifu-transmitter-bl 1.0.x from the slot
 * (0x08010400). The embedded bootloader is in .rodata, so the old bootloader verified
 * it as part of this image's signature (FwTag) before a single byte of it is used.
 *
 * Outcomes (all end in a reset into a bootloader's DFU, except FAIL):
 *   DONE     the bootloader pages hold the embedded bootloader (rewritten now or
 *            already) -> clean pages -> DFU request -> reset -> NEW bootloader's DFU.
 *   REFUSE   option bytes are not open (RDP 0, no WRP) or the embedded image is not
 *            sane; nothing written -> DFU request -> reset -> OLD bootloader's DFU.
 *   FAIL     the bootloader pages were erased but could not be programmed and verified
 *            after retries: no bootloader. LEDs blink; recover over SWD.
 *
 * LED (LD_HB): on = running / bootloader being rewritten, do not power off; off =
 * resetting; blinking = FAIL.
 */
#include <stdbool.h>
#include <stdint.h>

#include "stm32l4xx.h"
#include "board.h"
#include "flash_ram.h"
#include "updater_config.h"
#include "updater_layout.h"

extern const uint8_t updater_bl_image_start[];
extern const uint8_t updater_bl_image_end[];

#define RETRIES 3U

/* Identification string, found in the image by the host tools (and by eye in a dump). */
__attribute__((used, section(".updater_ident"))) static const char k_ident[] =
    "OW BL-UPDATER " UPDATER_PRODUCT_STR " " UPDATER_VERSION_STR " bl " UPDATER_BL_TAG_STR;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Size, initial stack pointer, reset vector and info block of the embedded bootloader. */
static bool image_sane(const uint8_t *img, uint32_t size)
{
    uint32_t sp = rd32(img), reset = rd32(img + 4);
    bool size_ok  = size == UPDATER_BL_SIZE && size > (BL_INFO_OFFSET + 4U) && size <= BL_REGION_SIZE;
    bool sp_ok    = sp > SRAM_BASE_ADDR && sp <= SRAM_END_ADDR;
    bool reset_ok = (reset & 1UL) != 0U && reset > FLASH_BASE_ADDR && reset < (FLASH_BASE_ADDR + BL_REGION_SIZE);
    bool info_ok  = size_ok && rd32(img + BL_INFO_OFFSET) == BL_INFO_MAGIC;
    return size_ok && sp_ok && reset_ok && info_ok;
}

static bool bootloader_already_installed(const uint8_t *img, uint32_t size)
{
    const volatile uint8_t *f = (const volatile uint8_t *)FLASH_BASE_ADDR;
    for (uint32_t i = 0U; i < BL_REGION_SIZE; i++) {
        uint8_t want = (i < size) ? img[i] : 0xFFU;
        if (f[i] != want) {
            return false;
        }
    }
    return true;
}

static void finish(void) __attribute__((noreturn));
static void finish(void)
{
    board_leds_set(0U);
    board_request_bootloader_dfu();
    __disable_irq();
    flash_ram_erase_pages_and_reset(CLEAN_FIRST_PAGE, CLEAN_LAST_PAGE, CLEAN_KEEP_PAGE);
}

static void refuse(void) __attribute__((noreturn));
static void refuse(void)
{
    /* Nothing was written: leave the slot alone too, so the old bootloader keeps its
     * floor and image state; its DFU takes any newer signed image. */
    board_leds_set(0U);
    board_request_bootloader_dfu();
    NVIC_SystemReset();
    for (;;) { }
}

static void fail_forever(void) __attribute__((noreturn));
static void fail_forever(void)
{
    for (;;) {
        board_leds_set(LED_RUN | LED_BUSY);
        board_delay(200000U);
        board_leds_set(0U);
        board_delay(200000U);
    }
}

int main(void)
{
    __disable_irq();
    board_iwdg_refresh();
    board_leds_init();
    board_leds_set(LED_RUN);
    board_hsi_on();

    const uint8_t *img = updater_bl_image_start;
    /* Both symbols bound the one .incbin blob (src/bl_blob.s.in). */
    // cppcheck-suppress comparePointers
    uint32_t size = (uint32_t)(updater_bl_image_end - updater_bl_image_start);

    if (!board_option_bytes_open() || !image_sane(img, size)) {
        refuse();
    }
    if (bootloader_already_installed(img, size)) {
        finish();
    }

    board_leds_set(LED_RUN | LED_BUSY);
    flash_ram_result_t rc = FLASH_RAM_OK;
    flash_ram_stats_t st;
    st.status = 0U;
    st.error_offset = 0U;
    for (uint32_t attempt = 0U; attempt < RETRIES; attempt++) {
        rc = flash_ram_rewrite_bootloader(img, size, &st);
        if (rc == FLASH_RAM_OK) {
            break;
        }
    }
    if (rc != FLASH_RAM_OK) {
        fail_forever();
    }
    finish();
}
