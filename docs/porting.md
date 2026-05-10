# Porting Guide

## Porting to a new MCU

Filum's shard logic is hardware-independent. Porting requires implementing the `FLHal` struct.

### 1. Implement FLHal

Create a file `shard/hal/<platform>/hal_<platform>.c`:

```c
#include "hal.h"

static int my_lora_send(const uint8_t *buf, uint8_t len) {
    // Transmit len bytes via your SX1276 SPI driver
    // Return 0 on success, nonzero on failure
}

static int my_lora_recv(uint8_t *buf, uint8_t *len, uint32_t timeout_ms) {
    // Receive a LoRa packet within timeout_ms milliseconds
    // Return 0 = packet received, 1 = timeout, -1 = error
}

static void my_sleep_ms(uint32_t ms) { /* low-power delay */ }
static void my_deep_sleep_rtc(uint32_t s) { /* RTC wakeup after s seconds */ }
static uint32_t my_get_tick_ms(void) { /* monotonic ms counter */ }
static int my_nvs_write(uint32_t off, const void *d, size_t n) { /* flash write */ }
static int my_nvs_read(uint32_t off, void *d, size_t n) { /* flash read */ }

const FLHal g_my_hal = {
    .lora_send      = my_lora_send,
    .lora_recv      = my_lora_recv,
    .sleep_ms       = my_sleep_ms,
    .deep_sleep_rtc = my_deep_sleep_rtc,
    .get_tick_ms    = my_get_tick_ms,
    .nvs_write      = my_nvs_write,
    .nvs_read       = my_nvs_read,
    .log            = NULL,  /* or your UART printf wrapper */
};
```

### 2. Select a memory preset

Choose the preset that fits your MCU's RAM:

| Preset   | Define                    | Params | RAM (model+shard) |
|---|---|---|---|
| TINY     | FL_CONFIG_PRESET_TINY     | 64     | ~2 KB             |
| SMALL    | FL_CONFIG_PRESET_SMALL    | 256    | ~8 KB             |
| MEDIUM   | (default)                 | 4096   | ~54 KB            |

In your build system, define the preset before including any Filum header:
```c
#define FL_CONFIG_PRESET_SMALL
#include <filum.h>
```

Or via CMake: `-DFL_CONFIG_PRESET=SMALL`

### 3. Implement the data callback

```c
int fl_user_data_cb(void *ctx, FLSample *s) {
    // Read one sample from your sensor / stored dataset
    // Fill s->input[], s->label[], s->input_len, s->label_len
    // Return 1 if sample is valid, 0 at end of epoch
}
```

### 4. Main loop

```c
FLModel model;
FLShard shard;

fl_model_init(&model, layers, layer_count);
fl_model_init_random(&model, SHARD_ID);
fl_shard_init(&shard, &g_my_hal, SHARD_ID, &model);

for (;;) {
    fl_shard_tick(&shard);
    fl_shard_sleep(&shard);
}
```

## Porting the Herald to a different transport

The Herald uses a file descriptor for all I/O. Any bidirectional file descriptor works:

```c
int fd = open_my_transport();   // SPI-to-UART bridge, CAN socket, etc.
fl_herald_init_fd(&herald, &cfg, fd);
fl_herald_run(&herald);
```
