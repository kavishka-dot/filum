# Filum

[![CI](https://github.com/kavishka-dot/filum/actions/workflows/ci.yml/badge.svg)](https://github.com/kavishka-dot/filum/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/kavishka-dot/filum/branch/main/graph/badge.svg)](https://codecov.io/gh/kavishka-dot/filum)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.2.0-blue.svg)](CHANGELOG.md)
![Language: C99](https://img.shields.io/badge/language-C99-brightgreen.svg)
![Platform](https://img.shields.io/badge/platform-STM32%20%7C%20Linux-lightgrey.svg)
![No heap](https://img.shields.io/badge/heap-none-orange.svg)
![No Python](https://img.shields.io/badge/python-none-red.svg)

**Federated learning for MCU-class edge devices over LoRa.**

Filum is a pure C99 library for on-device federated learning on microcontrollers that communicate over LoRa radio. A **Shard** running on an STM32F411 trains on private local sensor data and transmits sparse gradient updates over LoRa. A **Herald** running on Linux or a Raspberry Pi coordinates training rounds, aggregates updates, and sends the improved global model back to the shards.

No Python runtime. No dynamic allocation. No heap usage. Designed to run within a 128 KB RAM budget.

---

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Getting started](#getting-started)
- [Build options](#build-options)
- [Architecture](#architecture)
- [API reference](#api-reference)
- [Security](#security)
- [Memory model](#memory-model)
- [Wire protocol](#wire-protocol)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- **Pure C99:** no C++, no RTOS dependency, no stdlib heap
- **Zero dynamic allocation:** all buffers are compile-time static
- **LoRa-native:** wire format designed around LoRa payload constraints, from SF7 to SF12
- **Q8 sparse gradients:** top-k selection with Q8 quantization, giving about 5.5× compression compared with dense float updates
- **Federated aggregation:** FedAvg, FedProx, and coordinate-wise median aggregation for Byzantine robustness
- **Differential privacy:** per-shard Gaussian mechanism with an (ε, δ)-DP guarantee and budget tracking
- **ECDH encryption:** Curve25519 key exchange with ChaCha20-Poly1305 authenticated encryption
- **HAL abstraction:** shard logic stays hardware-independent through a struct of function pointers
- **Configurable memory:** four presets ranging from 2 KB for STM32F103 to 1 MB for Herald Linux
- **Tested:** 9 unit and integration tests, CI on every push, and Codecov coverage reporting

---

## Requirements

### Host (Herald + tests)

| Dependency | Version | Notes |
|---|---|---|
| GCC or Clang | ≥ 9 | C99 mode |
| CMake | ≥ 3.20 | |
| make or Ninja | any | |
| Doxygen | optional | for `--target filum_docs` |

### Shard (STM32 firmware)

| Dependency | Version | Notes |
|---|---|---|
| arm-none-eabi-gcc | ≥ 10 | `sudo apt install gcc-arm-none-eabi` |
| CMake | ≥ 3.20 | |
| OpenOCD | any | for flashing |

### Hardware (optional, all tests run without it)

- STM32F411 Blackpill (or STM32F103 Blue Pill)
- RFM95W (SX1276, 868 MHz EU / 915 MHz US)
- ST-Link V2 for flashing

---

## Getting started

### 1. Clone and build

```bash
git clone https://github.com/kavishka-dot/filum.git
cd filum
cmake -B build -DFILUM_TARGET=host
cmake --build build
```

### 2. Run the test suite

```bash
cd build && ctest --output-on-failure
```

Expected output:

```
1/9 Test #1: test_quant ...........   Passed    0.00 sec
2/9 Test #2: test_sparse ..........   Passed    0.00 sec
3/9 Test #3: test_frame ...........   Passed    0.00 sec
4/9 Test #4: test_round ...........   Passed    2.00 sec
5/9 Test #5: test_aggregator ......   Passed    0.00 sec
6/9 Test #6: test_dp ..............   Passed    0.00 sec
7/9 Test #7: test_crypto ..........   Passed    0.00 sec
8/9 Test #8: test_median ..........   Passed    0.00 sec
9/9 Test #9: test_integration .....   Passed    0.10 sec

100% tests passed, 0 tests failed out of 9
```

### 3. Run the end-to-end demo

```bash
./build/filum_demo
```

Runs a complete FL loop with Herald, Shard, and a synthetic dataset entirely in memory. No hardware is needed.

```
=======================================================
  Filum  -  End-to-End Federated Learning Demo
  1 Herald  |  1 Shard  |  Synthetic Data  |  No HW
=======================================================

Round  Shard acc   Global acc   Entries TX   Bytes saved   Time ms
  1      75.0%       85.0%        14 entries    190 bytes     1 ms
  2      97.5%       95.0%        14 entries    190 bytes     0 ms
  3     100.0%      100.0%        14 entries    190 bytes     0 ms

Wire efficiency:
  Dense upload   : 232 bytes  (58 params × 4B float)
  Sparse upload  :  42 bytes  (14 entries × 3B Q8, top-25%)
  Compression    : 5.5× smaller
  LoRa packets   : 1 packet per round at SF7
```

### 4. Flash STM32

```bash
# Build STM32F411 firmware
cmake -B build_stm32 -DFILUM_TARGET=STM32F411 -DFL_CONFIG_PRESET=SMALL
cmake --build build_stm32

# Flash via OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "program build_stm32/filum_shard.bin 0x08000000 verify reset exit"
```

Before flashing, fill in the HAL stubs in `shard/hal/stm32/hal_lora.c` and implement `fl_user_data_cb()` in `examples/shard_stm32f4/main.c`. See [`docs/porting.md`](docs/porting.md).

### 5. Run the Herald

```bash
./build/filum_herald \
    --port /dev/ttyUSB0 \
    --baud 115200 \
    --params 192 \
    --window 7200 \
    --min 2 --max 20
```

---

## Build options

### Target

```bash
cmake -B build -DFILUM_TARGET=host          # Linux/macOS/WSL
cmake -B build -DFILUM_TARGET=STM32F411     # STM32F411 Blackpill
cmake -B build -DFILUM_TARGET=STM32F103     # STM32F103 Blue Pill
```

### Memory presets

| Preset | Max params | Approx RAM | Intended target |
|---|---|---|---|
| `TINY` | 64 | ~2 KB | STM32F103, 20 KB RAM |
| `SMALL` | 256 | ~8 KB | STM32F411, constrained |
| _(default)_ | 4096 | ~54 KB | STM32F411 full / Linux |
| `LARGE` | 16384 | ~1 MB | Herald Linux only |

```bash
cmake -B build -DFL_CONFIG_PRESET=SMALL

# Or override individual limits
cmake -B build -DFL_MODEL_MAX_PARAMS=256 -DFL_SPARSE_MAX_ENTRIES=64
```

### Other options

| Option | Default | Description |
|---|---|---|
| `FILUM_ENABLE_TESTS` | `ON` | Build unit tests (host only) |
| `FILUM_ENABLE_ASAN` | `OFF` | AddressSanitizer (GCC/Clang) |
| `FILUM_LORA_SF` | `7` | LoRa spreading factor, from 7 to 12 |

### Install

```bash
cmake --install build --prefix /usr/local
```

Installs headers to `include/filum/`, libraries to `lib/`, and generates:
- `lib/pkgconfig/filum.pc`: use via `pkg-config --libs filum`
- `lib/cmake/Filum/FilumConfig.cmake`: use via `find_package(Filum)`

### Documentation

```bash
sudo apt install doxygen
cmake --build build --target filum_docs
# Output: build/docs/html/index.html
```

---

## Architecture

```
Herald (Linux, C)                      Shard (STM32F411, C)
┌───────────────────────────┐         ┌────────────────────────────┐
│  Event loop               │ LoRa    │  State machine             │
│  Fragment pool            │ ◄─────► │  Local SGD                 │
│  FedAvg / Median / FedProx│         │  Top-k sparse Q8 encoding  │
│  Round scheduler          │         │  HAL (SX1276 SPI driver)   │
│  DP sigma coordination    │         │  Gaussian DP noise         │
│  ECDH key management      │         │  ChaCha20-Poly1305         │
└───────────────────────────┘         └────────────────────────────┘
             │                                      │
      Serial/UART to                          Deep sleep
      LoRa gateway                         between rounds
```

### Round lifecycle

```
Herald                                  Shard
  │                                       │
  │──── FL_FRAME_BEACON ────────────────► │  Round announced; hyperparams + DP sigma
  │                                       │  Local SGD training (local_epochs passes)
  │                                       │  Gaussian DP noise added to delta
  │                                       │  Top-k sparse encode → Q8 → fragment
  │ ◄─── FL_FRAME_UPDATE (N packets) ─── │  Gradient fragments transmitted
  │      Reassemble → aggregate           │
  │──── FL_FRAME_ACK ──────────────────► │
  │──── FL_FRAME_DELTA (M packets) ────► │  Aggregated global delta distributed
  │──── FL_FRAME_ROUND_CLOSE ──────────► │  Shard deep-sleeps until next beacon
```

### Repository structure

```
filum/
├── common/                   Wire protocol, CRC, quantization, sparse encoding,
│   ├── include/              DP, crypto, error codes, version, config
│   └── src/
├── shard/                    Shard runtime (MCU side)
│   ├── include/              fl_shard.h, fl_train.h
│   ├── src/                  State machine, model, training
│   └── hal/
│       ├── hal.h             HAL interface (struct of function pointers)
│       ├── stm32/            STM32F4 + SX1276 implementation
│       └── host/             Linux loopback HAL for testing
├── herald/                   Herald coordinator (Linux side)
│   ├── include/              fl_herald.h, fl_aggregator.h, fl_round.h, fl_fragment_pool.h
│   ├── src/
│   └── transport/            Serial/UART LoRa gateway bridge
├── examples/
│   ├── shard_stm32f4/        Reference STM32 application
│   ├── herald_linux/         Linux daemon + pipe-based shard simulator
│   └── demo/                 Self-contained end-to-end demo (no hardware)
├── tests/                    9 unit and integration tests
├── docs/                     architecture.md, porting.md
├── cmake/                    FilumConfig.cmake.in, filum.pc.in
├── .github/                  CI workflow, issue templates, PR template
├── CHANGELOG.md
├── CONTRIBUTING.md
└── SECURITY.md
```

---

## API reference

Full Doxygen-generated reference: `cmake --build build --target filum_docs`.

### Umbrella header

```c
#include <filum.h>   /* all modules */

/* or include only what you need */
#include <fl_shard.h>    /* Shard runtime (MCU) */
#include <fl_herald.h>   /* Herald coordinator (Linux) */
```

### Error handling

Every public function returns `FLError`. Zero is success; negative values are errors.

```c
FLError err = fl_shard_init(&shard, &hal, SHARD_ID, &model);
if (err != FL_OK) {
    hal.log("init failed: %s\n", fl_strerror(err));
}
```

| Code | Meaning |
|---|---|
| `FL_OK` | Success |
| `FL_AGAIN` | No data available; try again |
| `FL_TIMEOUT` | Operation timed out |
| `FL_PENDING` | More fragments expected |
| `FL_ERR_INVALID_ARG` | NULL or invalid argument |
| `FL_ERR_BUFFER_TOO_SMALL` | Output buffer too small |
| `FL_ERR_CRC` | Frame CRC mismatch; corrupt |
| `FL_ERR_AUTH` | Poly1305 MAC failed; frame tampered |
| `FL_ERR_TRANSPORT` | Serial write failed |

Full list in [`common/include/fl_error.h`](common/include/fl_error.h).

### Shard (MCU side)

```c
static FLModel model;
static FLShard shard;

static const FLLayerDesc layers[] = {
    { .type=FL_LAYER_LINEAR, .activation=FL_ACT_RELU,
      .in_features=8, .out_features=16, .param_count=8*16+16 },
    { .type=FL_LAYER_LINEAR, .activation=FL_ACT_SIGMOID,
      .in_features=16, .out_features=2, .param_count=16*2+2 },
};

/* Provide training data one sample at a time */
int fl_user_data_cb(void *ctx, FLSample *s) {
    /* fill s->input[], s->label[], s->input_len, s->label_len */
    return 1;  /* return 0 at end of epoch */
}

int main(void) {
    fl_model_init(&model, layers, 2);
    fl_model_init_random(&model, SHARD_ID);

    fl_shard_init(&shard, &hal, SHARD_ID, &model);

    /* Optional: privacy and encryption */
    fl_shard_enable_dp(&shard, 1.0f, 1e-5f, 1.0f);
    fl_shard_enable_crypto(&shard, seed_32_bytes);

    for (;;) {
        fl_shard_tick(&shard);
        fl_shard_sleep(&shard);
    }
}
```

### Herald (Linux side)

```c
static float    global_model[192];
static FLHerald herald;

FLHeraldConfig cfg = {
    .serial_port       = "/dev/ttyUSB0",
    .baud_rate         = 115200,
    .model_param_count = 192,
    .global_model      = global_model,
    .aggregator        = FL_AGG_FEDAVG,   /* or FL_AGG_MEDIAN, FL_AGG_FEDPROX */
    .round_policy = {
        .window_seconds      = 7200,
        .inter_round_delay_s = 300,
        .min_shards          = 2,
        .max_shards          = 20,
        .local_epochs        = 3,
        .learning_rate       = 0.01f,
    },
};

fl_herald_init(&herald, &cfg);
fl_herald_run(&herald);   /* blocking; call fl_herald_stop() from signal handler */
```

---

## Security

Security features are opt-in per shard and independent of each other.

### Differential Privacy

```c
fl_shard_enable_dp(&shard,
    1.0f,   /* epsilon: privacy budget per round */
    1e-5f,  /* delta:   failure probability */
    1.0f    /* sensitivity: L2 clip norm */
);

/* Query cumulative budget spent (basic composition) */
fl_shard_privacy_report(&shard);
```

Herald coordinates noise levels by embedding `dp_sigma` in `FL_FRAME_BEACON`. Budget is tracked per shard using basic composition (`ε_total = T × ε_round`).

### Encryption

```c
/* seed: 32 bytes from MCU hardware RNG or XOR of UID registers */
fl_shard_enable_crypto(&shard, seed);
```

Shard sends `FL_FRAME_HANDSHAKE` on the next idle tick. Herald completes the X25519 exchange. All subsequent `FL_FRAME_UPDATE` payloads are encrypted (ChaCha20) and authenticated (Poly1305). Tampered frames return `FL_ERR_AUTH` and are discarded.

> **Cryptography notice:** `fl_crypto.c` is pure C99 for MCU portability and has not been independently audited. For deployments where libsodium or mbedTLS is available, see [SECURITY.md](SECURITY.md).

---

## Memory model

```
Component        Default RAM   TINY preset   SMALL preset
─────────────────────────────────────────────────────────
FLModel          32 KB         512 B         2 KB
FLShard          22 KB         1.5 KB        6 KB
─────────────────────────────────────────────────────────
Per shard total  54 KB         2 KB          8 KB
STM32F411 RAM    128 KB        ✅            ✅
STM32F103 RAM    20 KB         ✅            ⚠️

FLAggregator     1 MB          192 KB        512 KB
(Herald only)
```

RAM budget macros:

```c
#include <fl_config.h>
/* FL_RAM_MODEL, FL_RAM_SHARD, FL_RAM_AGGREGATOR available at compile time */
```

---

## Wire protocol

Every LoRa packet is a packed `FLFrame`:

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | `magic` | `0x464C` ('FL') |
| 2 | 1 | `frame_type` | See table below |
| 3 | 2 | `shard_id` | `0xFFFF` = Herald |
| 5 | 1 | `round_id` | Wraps at 255 |
| 6 | 1 | `frag_index` | 0-based |
| 7 | 1 | `frag_total` | 1 = single-packet message |
| 8 | 2 | `crc16` | CRC-16/CCITT-FALSE over header + payload |
| 10 | N | `payload` | Up to 214 bytes (SF7) |

| Frame type | Direction | Description |
|---|---|---|
| `FL_FRAME_BEACON` | Herald → Shard | Opens a round; carries hyperparams + DP sigma |
| `FL_FRAME_DELTA` | Herald → Shard | Aggregated global model delta |
| `FL_FRAME_UPDATE` | Shard → Herald | Local gradient update |
| `FL_FRAME_ACK` | Herald → Shard | Update fully received |
| `FL_FRAME_ROUND_CLOSE` | Herald → Shard | Round closed; shard may sleep |
| `FL_FRAME_HANDSHAKE` | Shard → Herald | ECDH public key |
| `FL_FRAME_HANDSHAKE_ACK` | Herald → Shard | ECDH public key reply |

Current wire protocol version: `FL_WIRE_VERSION 1`.

---

## HAL interface

Implementing `FLHal` is the only requirement to port Filum to a new MCU.

```c
typedef struct {
    int      (*lora_send)(const uint8_t *buf, uint8_t len);
    int      (*lora_recv)(uint8_t *buf, uint8_t *len, uint32_t timeout_ms);
    int      (*lora_set_sf)(uint8_t sf);         /* optional, may be NULL */
    void     (*sleep_ms)(uint32_t ms);
    void     (*deep_sleep_rtc)(uint32_t seconds);
    uint32_t (*get_tick_ms)(void);
    int      (*nvs_write)(uint32_t offset, const void *data, size_t len);
    int      (*nvs_read)(uint32_t offset, void *data, size_t len);
    void     (*log)(const char *fmt, ...);       /* optional, may be NULL */
} FLHal;
```

Provided implementations:
- [`shard/hal/stm32/`](shard/hal/stm32/): STM32F4 with SX1276 via SPI1
- [`shard/hal/host/`](shard/hal/host/): Linux loopback for host testing

For a complete porting walkthrough, see [`docs/porting.md`](docs/porting.md).

---

## Versioning

Filum follows [Semantic Versioning 2.0.0](https://semver.org).

| Component | Current |
|---|---|
| Library | `0.2.0` |
| Wire protocol | `FL_WIRE_VERSION 1` |

```c
/* Runtime check */
if (!fl_version_compatible()) {
    /* headers and library MAJOR version differ */
}
printf("Filum %s\n", fl_version_string());
```

Shards and Heralds must use the same `FL_WIRE_VERSION` to interoperate. See [CHANGELOG.md](CHANGELOG.md) for migration notes between versions.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build setup, code style, and the PR checklist.

Quick summary:
- All 9 tests must pass: `cd build && ctest --output-on-failure`
- Every new public function needs Doxygen `@param` / `@retval` comments
- No dynamic allocation: use static buffers or return `FL_ERR_CAPACITY`
- Update [CHANGELOG.md](CHANGELOG.md) under `[Unreleased]`

To report a security vulnerability, see [SECURITY.md](SECURITY.md). Do not open a public issue.

---

## License

MIT. See [LICENSE](LICENSE).
