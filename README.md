# Filum

**Federated learning for MCU-class edge devices over LoRa.**

Pure C, end-to-end. No Python runtime. No dynamic allocation. Designed for STM32 + RFM95W (SX1276).

---

## Architecture

```
Herald (Linux, C)                    Shard (STM32F411, C)
┌─────────────────────────┐         ┌──────────────────────────┐
│ Event loop              │ LoRa    │ State machine            │
│ Fragment pool           │ ◄─────► │ Local SGD                │
│ FedAvg aggregation      │         │ Top-k sparse encoding    │
│ Round scheduler         │         │ HAL (SX1276 SPI driver)  │
└─────────────────────────┘         └──────────────────────────┘
            │                                   │
     Serial/UART to                        Deep sleep
     LoRa gateway                       between rounds
```

**Wire protocol:** Q8 sparse gradient entries (3 bytes each), fragmented across LoRa packets. 71 entries per SF7 packet.

---

## Repo structure

```
filum/
├── common/            Wire protocol, CRC, quantization, sparse encoding
│   ├── include/
│   └── src/
├── shard/             Shard runtime (state machine, training, HAL interface)
│   ├── include/
│   ├── src/
│   └── hal/
│       ├── stm32/     STM32F4 SPI/RTC/NVS/power stubs
│       └── host/      Linux loopback HAL for testing
├── herald/            Herald coordinator (event loop, aggregation)
│   ├── include/
│   ├── src/
│   └── transport/
├── examples/
│   ├── shard_stm32f4/ Reference STM32 application
│   └── herald_linux/  Linux daemon + software shard simulator
├── tests/             Host-side unit tests (no MCU required)
└── CMakeLists.txt
```

---

## Build

### Host (tests + Herald daemon)

```bash
cmake -B build
cmake --build build
cd build && ctest --output-on-failure
```

### STM32F411

```bash
cmake -B build_stm32 -DFILUM_TARGET=STM32F411
cmake --build build_stm32
# Flash:
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "program build_stm32/filum_shard.bin 0x08000000 verify reset exit"
```

STM32F103 also supported: `-DFILUM_TARGET=STM32F103`

---

## Quick start

### 1. Run host tests first

```bash
cmake -B build && cmake --build build
cd build && ctest -V
```

All tests should pass before touching hardware.

### 2. Run software simulation (no hardware needed)

```bash
./build/filum_shard_sim
```

Runs a Shard and Herald in the same process via pipes.
Completes one full FL round and prints stats.

### 3. Flash STM32

1. Wire RFM95W to STM32F411 Blackpill (SPI1: PA5/PA6/PA7, NSS=PA4, DIO0=PB0, RESET=PC13)
2. Fill in `shard/hal/stm32/hal_lora.c` SPI primitives (replace stubs with STM32 HAL calls)
3. Implement `fl_user_data_cb()` in `examples/shard_stm32f4/main.c`
4. Build and flash

### 4. Run Herald on Linux

```bash
./build/filum_herald \
    --port /dev/ttyUSB0 \
    --baud 115200 \
    --params 192 \
    --window 7200 \
    --min 2 --max 20
```

---

## Wire protocol

Every LoRa packet is a `FLFrame`:

| Offset | Size | Field       | Notes                          |
|--------|------|-------------|--------------------------------|
| 0      | 2    | magic       | `0x464C` ('FL')                |
| 2      | 1    | frame_type  | BEACON/DELTA/UPDATE/ACK/CLOSE  |
| 3      | 2    | shard_id    | 0xFFFF = Herald                |
| 5      | 1    | round_id    | wraps at 255                   |
| 6      | 1    | frag_index  | 0-based                        |
| 7      | 1    | frag_total  | 1 = single packet              |
| 8      | 2    | crc16       | CRC-16/CCITT-FALSE             |
| 10     | N    | payload     | up to 214 bytes                |

Gradient entry: `uint16_t param_index` + `int8_t delta_q8` = **3 bytes**. 71 per packet at SF7.

---

## HAL interface

The `FLHal` struct is the sole hardware dependency:

```c
typedef struct {
    int      (*lora_send)(const uint8_t *buf, uint8_t len);
    int      (*lora_recv)(uint8_t *buf, uint8_t *len, uint32_t timeout_ms);
    int      (*lora_set_sf)(uint8_t sf);           /* optional */
    void     (*sleep_ms)(uint32_t ms);
    void     (*deep_sleep_rtc)(uint32_t seconds);
    uint32_t (*get_tick_ms)(void);
    int      (*nvs_write)(uint32_t offset, const void *data, size_t len);
    int      (*nvs_read)(uint32_t offset, void *data, size_t len);
    void     (*log)(const char *fmt, ...);          /* optional */
} FLHal;
```

Shard code never calls hardware directly — always through the HAL.

---

## Customize for your task

1. **Model architecture**: edit `g_layers[]` in `examples/shard_stm32f4/main.c`
2. **Sensor data**: implement `fl_user_data_cb()` with real ADC/I2C reads
3. **LoRa frequency**: change `LORA_FREQ_HZ` (868MHz EU / 915MHz US)
4. **Round timing**: adjust `window_seconds` and `inter_round_delay_s` in Herald config
5. **Shard ID**: derive from `UID_BASE` (MCU 96-bit unique ID) for production

---

## Roadmap

- [ ] ECDH key exchange for gradient encryption
- [ ] Differential privacy (gradient noise injection)
- [ ] FreeRTOS task wrapper for Shard
- [ ] ChirpStack/LoRaWAN gateway integration
- [ ] Per-round model checkpointing on Herald
- [ ] ARGUS extension: multi-node cross-validation

---

## License

MIT
