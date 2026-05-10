# Changelog

All notable changes to Filum are documented here.
Format: [Semantic Versioning 2.0.0](https://semver.org).

---

## [0.2.0] - 2025-05-10

### Added
- **Differential Privacy** (`fl_dp.h`, `fl_dp.c`): Gaussian mechanism for
  (ε, δ)-DP on shard gradient updates. Per-round budget tracking with basic
  composition. Enable via `fl_shard_enable_dp()`.
- **ECDH encryption** (`fl_crypto.h`, `fl_crypto.c`): Curve25519 key exchange
  + ChaCha20-Poly1305 authenticated encryption for UPDATE/DELTA frames. Enable
  via `fl_shard_enable_crypto()`.
- **Byzantine-robust aggregation**: Coordinate-wise median fully implemented in
  `FL_AGG_MEDIAN`. Tolerates up to (n-1)/2 malicious shards per round.
- **`FL_FRAME_HANDSHAKE` / `FL_FRAME_HANDSHAKE_ACK`** frame types for ECDH
  public key exchange over LoRa.
- **`fl_herald_init_fd()`**: Initialize Herald with a pre-opened file descriptor.
  Enables testing via pipes and custom transports (SPI, CAN, pty).
- **`FLError` return type**: All public API functions now return `FLError`.
  Error codes are documented in `fl_error.h`.
- **`fl_config.h`**: Centralized compile-time memory configuration. Four presets:
  TINY (STM32F103), SMALL (STM32F411), MEDIUM (default), LARGE (Herald).
- **`fl_version.h`**: Runtime version query via `fl_version()` and
  `fl_version_string()`. Wire protocol version tracked via `FL_WIRE_VERSION`.
- **`filum.h`**: Umbrella header including all public modules.
- **Doxygen documentation** target: `cmake --build build --target filum_docs`.
- **CMake install target**: `cmake --install build` installs headers, libraries,
  pkg-config file, and CMake package config (`find_package(Filum)`).
- **Integration test** (`test_integration.c`): Full FL round via pipe loopback.
  Covers Herald, fragment pool, shard state machine, DP, and crypto paths.
- **9 unit tests** total (up from 5). 100% passing.
- `docs/architecture.md`: System design, round lifecycle, memory model.
- `docs/porting.md`: Step-by-step guide for new MCU targets.
- `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`.

### Changed
- `FLBeaconPayload` now includes `dp_sigma` field so the Herald can coordinate
  noise levels across shards (Herald-dictated DP).
- `fl_shard.c` TRAINING state now computes parameter delta
  (`params_after - params_before`) rather than encoding zeroed `grad[]`.
- `fl_shard.c` TX state handles `frag_total == 0` without hanging.
- `fl_aggregator.c`: `FL_AGG_MEDIAN` now fully implemented with coordinate-wise
  median via qsort. Was previously falling back to mean.
- `CMakeLists.txt`: Dual-target build (host + STM32) now uses `set()` for
  `FILUM_TARGET` instead of `option()`, fixing VS Code CMake Tools compatibility.
- All `CMakePresets.json` entries updated for VS 2022/2026 compatibility.

### Fixed
- Shard encoding zero gradients after per-sample SGD (param delta was always 0).
- TX state machine infinite loop when `frag_total == 0`.
- CRC computation used two separate `fl_crc16` calls XOR'd together (wrong);
  now correctly computed over header[0..7] + payload in one pass.
- FedAvg accumulator was weighted by `shard_data_size`; now uses unweighted mean.
- Curve25519 field arithmetic carry overflow (`h0 += c >> 25 * 19` precedence bug).
- CMake `FILUM_TARGET=OFF` crash when VS Code CMake Tools doesn't set a value.

---

## [0.1.0] - 2025-05-09

### Added
- Initial implementation of Herald + Shard federated learning over LoRa.
- Wire protocol: `FLFrame` with CRC-16/CCITT-FALSE, 5 frame types.
- Q8 sparse gradient encoding: top-k selection, fragmentation, residual accumulation.
- FedAvg and FedProx aggregation strategies.
- Fragment reassembly pool (Herald).
- Round scheduler with configurable window and min/max shard policy.
- STM32F411 HAL stubs (SPI/SX1276, RTC, NVS, power management).
- Host loopback HAL for testing without hardware.
- End-to-end demo (`examples/demo/demo.c`).
- 5 unit tests: quant, sparse, frame, round, aggregator.
- CI via GitHub Actions + Codecov coverage reporting.
