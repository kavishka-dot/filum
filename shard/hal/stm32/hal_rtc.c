/*
 * filum/shard/hal/stm32/hal_rtc.c
 *
 * RTC wakeup timer for deep sleep cycles.
 * Uses STM32 RTC Wakeup Timer (LSI or LSE clock source).
 */

#include "hal.h"
#include <stdint.h>

void hal_rtc_init(void)
{
    /*
     * Enable RTC clock, configure LSI as source.
     * RCC->BDCR |= RCC_BDCR_LSION; wait for LSIRDY;
     * RCC->BDCR |= RCC_BDCR_RTCEN | RCC_BDCR_RTCSEL_LSI;
     */
}

void hal_deep_sleep_rtc(uint32_t seconds)
{
    /*
     * 1. Configure RTC wakeup timer for 'seconds'.
     * 2. Enter STOP mode (all clocks off, RAM retained):
     *    PWR->CR  |= PWR_CR_LPDS;
     *    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
     *    __WFI();
     * 3. On wakeup: restore clocks, re-init peripherals.
     */
    (void)seconds;
}

uint32_t hal_get_tick_ms(void)
{
    /* return HAL_GetTick(); */
    return 0;
}

void hal_sleep_ms(uint32_t ms)
{
    /* HAL_Delay(ms); */
    (void)ms;
}
