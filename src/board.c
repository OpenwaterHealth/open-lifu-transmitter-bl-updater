/*
 * Board support, Open-LIFU transmitter (STM32L443RC). See board.h.
 */
#include "board.h"
#include "stm32l4xx.h"

#define LED_PIN  12U   /* PC12, LD_HB */

/* Contract with open-lifu-transmitter-bl (1.0.x and 1.1.x), RTC backup registers. */
#define BL_BKP_REQ_DFU_MAGIC  0xB007C0DEUL   /* BKP7R: one-shot DFU request */

void board_leds_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;
    /* Touch only this pin's mode bits. */
    GPIOC->MODER = (GPIOC->MODER & ~(3UL << (LED_PIN * 2U))) | (1UL << (LED_PIN * 2U));
    GPIOC->OTYPER &= ~(1UL << LED_PIN);
    board_leds_set(0U);
}

void board_leds_set(uint32_t mask)
{
    if ((mask & (LED_RUN | LED_BUSY)) != 0U) {
        GPIOC->BSRR = 1UL << LED_PIN;
    } else {
        GPIOC->BSRR = 1UL << (LED_PIN + 16U);
    }
}

void board_iwdg_refresh(void)
{
    IWDG->KR = 0x0000AAAAUL;
}

void board_delay(uint32_t loops)
{
    for (volatile uint32_t i = 0U; i < loops; i++) {
        if ((i & 0xFFFU) == 0U) {
            board_iwdg_refresh();
        }
    }
}

void board_hsi_on(void)
{
}

bool board_option_bytes_open(void)
{
    uint32_t wrpa = FLASH->WRP1AR, wrpb = FLASH->WRP1BR;
    uint32_t a_start = (wrpa & FLASH_WRP1AR_WRP1A_STRT) >> FLASH_WRP1AR_WRP1A_STRT_Pos;
    uint32_t a_end   = (wrpa & FLASH_WRP1AR_WRP1A_END) >> FLASH_WRP1AR_WRP1A_END_Pos;
    uint32_t b_start = (wrpb & FLASH_WRP1BR_WRP1B_STRT) >> FLASH_WRP1BR_WRP1B_STRT_Pos;
    uint32_t b_end   = (wrpb & FLASH_WRP1BR_WRP1B_END) >> FLASH_WRP1BR_WRP1B_END_Pos;
    uint32_t p_start = FLASH->PCROP1SR & FLASH_PCROP1SR_PCROP1_STRT;
    uint32_t p_end   = FLASH->PCROP1ER & FLASH_PCROP1ER_PCROP1_END;

    bool rdp0   = (FLASH->OPTR & FLASH_OPTR_RDP) == 0xAAU;
    bool no_wrp = a_start > a_end && b_start > b_end;   /* an area is off when start > end */
    bool no_pcrop = p_start > p_end;
    return rdp0 && no_wrp && no_pcrop;
}

void board_request_bootloader_dfu(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;
    PWR->CR1 |= PWR_CR1_DBP;
    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0U) {
        if ((RCC->BDCR & RCC_BDCR_RTCSEL) == 0U) {
            RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_1;   /* LSI */
        }
        RCC->BDCR |= RCC_BDCR_RTCEN;
    }
    RTC->BKP6R = 0U;                     /* boot-failure counter */
    RTC->BKP7R = BL_BKP_REQ_DFU_MAGIC;
    __DSB();
}
