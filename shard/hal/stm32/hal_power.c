/*
 * filum/shard/hal/stm32/hal_power.c
 *
 * Power management: STOP mode entry/exit for STM32F4.
 */

#include "hal.h"

void hal_power_init(void)
{
    /*
     * Enable PWR peripheral clock: __HAL_RCC_PWR_CLK_ENABLE()
     * Configure voltage regulator for low-power in stop mode:
     *   HAL_PWREx_EnableLowRegulatorLowVoltage()
     */
}

void hal_power_enter_stop(void)
{
    /*
     * HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
     * After wakeup: SystemClock_Config() to restore PLL.
     */
}
