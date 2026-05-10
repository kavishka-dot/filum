#ifndef HAL_HOST_H
#define HAL_HOST_H

#include "../hal.h"
#include "fl_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get the host loopback HAL. */
const FLHal *fl_hal_host_get(void);

/**
 * Enable/disable loopback mode.
 * In loopback mode, sent packets are immediately available for recv.
 * Useful for single-process unit tests.
 */
void fl_hal_host_set_loopback(int enable);

/**
 * Open named pipes for inter-process testing.
 * tx_path: shard writes here, herald reads here.
 * rx_path: herald writes here, shard reads here.
 */
int fl_hal_host_open_pipes(const char *tx_path, const char *rx_path);

#ifdef __cplusplus
}
#endif

#endif /* HAL_HOST_H */
