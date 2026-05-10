#ifndef HAL_STM32_H
#define HAL_STM32_H

#include "../hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the STM32 HAL implementation.
 * Call once during init and pass to fl_shard_init().
 */
const FLHal *fl_hal_stm32_get(void);

/**
 * Set the minimum inter-packet gap for duty cycle enforcement.
 * Default: 10000ms (conservative for EU868 1% duty cycle with 1s airtime).
 * Adjust based on your actual SF and payload size.
 */
void fl_hal_stm32_set_duty_gap_ms(uint32_t gap_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_STM32_H */
