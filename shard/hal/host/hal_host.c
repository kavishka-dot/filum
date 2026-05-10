/*
 * filum/shard/hal/host/hal_host.c
 *
 * Host (Linux) HAL implementation for integration testing.
 * Shard and Herald run in the same process; LoRa is a loopback pipe.
 */

#include "hal.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>

/* Pipe pair: shard writes to tx_pipe, herald reads from it */
static int tx_pipe[2] = { -1, -1 };   /* shard -> herald */
static int rx_pipe[2] = { -1, -1 };   /* herald -> shard */

void hal_host_init_pipes(int *shard_tx_r, int *shard_rx_w,
                          int *herald_rx_r, int *herald_tx_w)
{
    pipe(tx_pipe);
    pipe(rx_pipe);
    *shard_tx_r  = tx_pipe[1];  /* shard writes here */
    *shard_rx_w  = rx_pipe[0];  /* shard reads here  */
    *herald_rx_r = tx_pipe[0];  /* herald reads here */
    *herald_tx_w = rx_pipe[1];  /* herald writes here */
}

static int g_shard_tx_fd = -1;
static int g_shard_rx_fd = -1;

void hal_host_set_fds(int tx_fd, int rx_fd)
{
    g_shard_tx_fd = tx_fd;
    g_shard_rx_fd = rx_fd;
}

static int host_lora_send(const uint8_t *buf, uint8_t len)
{
    uint8_t hdr = len;
    if (write(g_shard_tx_fd, &hdr, 1) != 1)  return -1;
    if (write(g_shard_tx_fd, buf, len) != len) return -1;
    return 0;
}

static int host_lora_recv(uint8_t *buf, uint8_t *len_out, uint32_t timeout_ms)
{
    /* Non-blocking read with poll */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    uint8_t hdr = 0;
    while (1) {
        ssize_t n = read(g_shard_rx_fd, &hdr, 1);
        if (n == 1) {
            ssize_t m = read(g_shard_rx_fd, buf, hdr);
            *len_out = (m > 0) ? (uint8_t)m : 0;
            return 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t elapsed_ms = (uint64_t)(now.tv_sec - start.tv_sec) * 1000
                            + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) return 1;  /* timeout */
        usleep(1000);
    }
}

static void host_sleep_ms(uint32_t ms)   { usleep(ms * 1000); }
static void host_deep_sleep_rtc(uint32_t s) { sleep(s); }

static uint32_t host_get_tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int host_nvs_write(uint32_t offset, const void *data, size_t len)
{
    FILE *f = fopen("/tmp/filum_shard.nvs", "r+b");
    if (!f) f = fopen("/tmp/filum_shard.nvs", "wb");
    if (!f) return -1;
    fseek(f, (long)offset, SEEK_SET);
    fwrite(data, 1, len, f);
    fclose(f);
    return 0;
}

static int host_nvs_read(uint32_t offset, void *data, size_t len)
{
    FILE *f = fopen("/tmp/filum_shard.nvs", "rb");
    if (!f) return -1;
    fseek(f, (long)offset, SEEK_SET);
    size_t n = fread(data, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

static void host_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* Exported HAL struct */
const FLHal g_host_hal = {
    .lora_send      = host_lora_send,
    .lora_recv      = host_lora_recv,
    .lora_set_sf    = NULL,
    .sleep_ms       = host_sleep_ms,
    .deep_sleep_rtc = host_deep_sleep_rtc,
    .get_tick_ms    = host_get_tick_ms,
    .nvs_write      = host_nvs_write,
    .nvs_read       = host_nvs_read,
    .log            = host_log,
};
