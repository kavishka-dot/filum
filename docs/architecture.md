# Filum Architecture

## Overview

Filum is a federated learning library for resource-constrained edge devices communicating over LoRa. The system has two roles:

- **Shard** — runs on an MCU (STM32F411). Trains on private local data. Sends sparse gradient updates over LoRa.
- **Herald** — runs on a Linux host adjacent to a LoRa gateway. Coordinates rounds, aggregates updates, distributes the global model delta.

## Round lifecycle

```
Herald                              Shard
  |                                   |
  |──── FL_FRAME_BEACON ────────────►|  Round announced
  |                                   |  Local SGD training
  |                                   |  Top-k sparse encode
  |◄─── FL_FRAME_UPDATE (N pkts) ────|  Gradient fragments
  |     [reassemble + aggregate]      |
  |──── FL_FRAME_ACK ───────────────►|
  |──── FL_FRAME_DELTA (M pkts) ────►|  Global delta fragments
  |──── FL_FRAME_ROUND_CLOSE ───────►|  Shard may deep-sleep
```

## Memory model

All allocation is static. Buffer sizes are compile-time constants in `fl_config.h`.

| Component    | Default RAM | Preset TINY | Preset SMALL |
|---|---|---|---|
| FLModel      | 32 KB       | 512 B       | 2 KB         |
| FLShard      | 22 KB       | 1.5 KB      | 6 KB         |
| FLAggregator | 1 MB        | 192 KB      | 512 KB       |

Select a preset: `cmake -B build -DFL_CONFIG_PRESET=SMALL`

## Wire protocol

Every LoRa packet is a `FLFrame` (10-byte header + up to 214-byte payload).

Gradient entries: 3 bytes each (`uint16_t param_index` + `int8_t delta_q8`).
Q8 encoding: `real_value = delta_q8 / 128.0`

At SF7 (125 kHz BW): 71 gradient entries per packet.

## HAL interface

The shard runtime is hardware-independent via `FLHal` — a struct of function pointers:

```c
typedef struct {
    int      (*lora_send)(const uint8_t *buf, uint8_t len);
    int      (*lora_recv)(uint8_t *buf, uint8_t *len, uint32_t timeout_ms);
    void     (*sleep_ms)(uint32_t ms);
    void     (*deep_sleep_rtc)(uint32_t seconds);
    uint32_t (*get_tick_ms)(void);
    int      (*nvs_write)(uint32_t offset, const void *data, size_t len);
    int      (*nvs_read)(uint32_t offset, void *data, size_t len);
    void     (*log)(const char *fmt, ...);   /* optional, NULL in production */
} FLHal;
```

Implementations provided: `shard/hal/stm32/` (STM32F4 + SX1276) and `shard/hal/host/` (Linux loopback for testing).

## Security

Optional, enabled per-shard at init time:

**Differential Privacy** (`fl_dp.h`): Gaussian mechanism adds noise to parameter deltas before sparse encoding. Per-round (ε, δ)-DP guarantee. Budget tracked across rounds.

**Encryption** (`fl_crypto.h`): Curve25519 ECDH key exchange + ChaCha20-Poly1305 authenticated encryption of UPDATE and DELTA frames. Handshake via `FL_FRAME_HANDSHAKE` / `FL_FRAME_HANDSHAKE_ACK`.

## Thread safety

No Filum function is thread-safe. Callers must externally synchronize all access to a given `FLShard` or `FLHerald` instance.
