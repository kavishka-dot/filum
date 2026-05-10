#ifndef FILUM_HAL_H
#define FILUM_HAL_H

/*
 * filum/shard/hal/hal.h
 *
 * Hardware Abstraction Layer for Filum Shard.
 *
 * Platform-specific code fills an FLHal struct and passes it to fl_shard_init.
 * All shard logic calls only through this interface - no direct hardware access.
 *
 * Implementations:
 *   shard/hal/stm32/   - STM32F4 + SX1276 (RFM95W)
 *   shard/hal/host/    - Host loopback for unit tests
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes */
#define FL_HAL_OK       0
#define FL_HAL_TIMEOUT  1
#define FL_HAL_ERROR   -1

/* -------------------------------------------------------------------------
 * HAL function pointer table
 * ------------------------------------------------------------------------- */

typedef struct {

    /* --- LoRa ------------------------------------------------------------- */

    /*
     * lora_send - transmit buf (len bytes) over LoRa.
     * Blocks until TX complete or returns FL_HAL_ERROR.
     * Caller must respect duty cycle; HAL does not enforce it.
     */
    int (*lora_send)(const uint8_t *buf, uint8_t len);

    /*
     * lora_recv - wait for incoming LoRa packet.
     * buf:        output buffer
     * len:        output, number of bytes received
     * timeout_ms: 0 = non-blocking poll, >0 = block up to timeout_ms
     * Returns FL_HAL_OK if packet received, FL_HAL_TIMEOUT if none, FL_HAL_ERROR on fault.
     */
    int (*lora_recv)(uint8_t *buf, uint8_t *len, uint32_t timeout_ms);

    /*
     * lora_set_sf - set spreading factor (7..12).
     * Optional. Pass NULL if SF is fixed at compile time.
     */
    int (*lora_set_sf)(uint8_t sf);

    /* --- Power management -------------------------------------------------- */

    /*
     * sleep_ms - low-power delay (RTOS delay or WFI loop).
     */
    void (*sleep_ms)(uint32_t ms);

    /*
     * deep_sleep_rtc - enter STOP/STANDBY mode, wake after seconds.
     * Used between training sessions and TX windows to save power.
     */
    void (*deep_sleep_rtc)(uint32_t seconds);

    /* --- Time -------------------------------------------------------------- */

    /*
     * get_tick_ms - monotonic millisecond counter.
     * Must not overflow during a single active session (~hours).
     */
    uint32_t (*get_tick_ms)(void);

    /* --- Non-volatile storage ---------------------------------------------- */

    /*
     * nvs_write / nvs_read - persist model weights across resets.
     * offset: byte offset within NVS region
     * data / len: payload
     * Returns 0 on success.
     */
    int (*nvs_write)(uint32_t offset, const void *data, size_t len);
    int (*nvs_read)(uint32_t offset, void *data, size_t len);

    /* --- Debug (optional, may be NULL in production) ----------------------- */

    /*
     * log - formatted log output (UART, SWO, etc.)
     * May be NULL; Shard code checks before calling.
     */
    void (*log)(const char *fmt, ...);

} FLHal;

#ifdef __cplusplus
}
#endif

#endif /* FILUM_HAL_H */
