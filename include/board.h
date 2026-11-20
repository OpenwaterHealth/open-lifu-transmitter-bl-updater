/*
 * Board support for the updater: LEDs, watchdog, flash clock, option bytes and the
 * bootloader's DFU request. Register-level, CMSIS only.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define LED_RUN    0x1U   /* transmitter LD_HB (PC12); the board has one LED */
#define LED_BUSY   0x2U   /* same LED */

void board_leds_init(void);
void board_leds_set(uint32_t mask);
void board_iwdg_refresh(void);
void board_delay(uint32_t loops);

/* Kept for symmetry with the console updater: the L4 flash needs no extra clock. */
void board_hsi_on(void);

/* True when the option bytes show the research state the updater was made for:
 * RDP level 0 and no write protection anywhere. */
bool board_option_bytes_open(void);

/* RTC backup registers: confirm this boot (clear the boot-failure counter) and ask the
 * bootloader that runs after the next reset to enter DFU (BKP7R = 0xB007C0DE). */
void board_request_bootloader_dfu(void);

#endif /* BOARD_H */
