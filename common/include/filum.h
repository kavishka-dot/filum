#ifndef FILUM_H
#define FILUM_H

/**
 * @file filum.h
 * @brief Filum — Federated Learning for MCU-class Edge Devices.
 *
 * This is the single umbrella header. Applications may include this
 * file instead of individual module headers.
 *
 * @code
 * #include <filum.h>
 * @endcode
 *
 * Alternatively, include only the modules you need:
 * @code
 * #include <fl_shard.h>   // Shard runtime (MCU side)
 * #include <fl_herald.h>  // Herald coordinator (Linux side)
 * @endcode
 *
 * @mainpage Filum
 *
 * @section overview Overview
 *
 * Filum is a pure-C99 federated learning library for resource-constrained
 * edge devices communicating over LoRa. It targets the STM32F411 Blackpill
 * with RFM95W (SX1276) as the shard (edge device) and any Linux host as
 * the Herald (coordinator).
 *
 * Key design constraints:
 * - No dynamic memory allocation anywhere in the library
 * - No C++ runtime dependency
 * - No Python runtime dependency
 * - All wire-format structs are packed and CRC-protected
 * - Shard code is hardware-independent via a HAL struct of function pointers
 *
 * @section quickstart Quick Start
 *
 * @subsection shard Shard (MCU)
 * @code
 * #include <filum.h>
 *
 * static FLModel  model;
 * static FLShard  shard;
 * static const FLHal hal = { ... };  // fill in your HAL
 *
 * // Define your layer architecture
 * static const FLLayerDesc layers[] = {
 *     { .type=FL_LAYER_LINEAR, .activation=FL_ACT_RELU,
 *       .in_features=8, .out_features=16, .param_count=8*16+16 },
 *     { .type=FL_LAYER_LINEAR, .activation=FL_ACT_SIGMOID,
 *       .in_features=16, .out_features=2, .param_count=16*2+2 },
 * };
 *
 * // Provide training data one sample at a time
 * int fl_user_data_cb(void *ctx, FLSample *s) { ... }
 *
 * int main(void) {
 *     fl_model_init(&model, layers, 2);
 *     fl_model_init_random(&model, SHARD_ID);
 *     fl_shard_init(&shard, &hal, SHARD_ID, &model);
 *     // Optional: enable privacy and encryption
 *     fl_shard_enable_dp(&shard, 1.0f, 1e-5f, 1.0f);
 *     fl_shard_enable_crypto(&shard, seed);
 *     for (;;) {
 *         fl_shard_tick(&shard);
 *         fl_shard_sleep(&shard);
 *     }
 * }
 * @endcode
 *
 * @subsection herald Herald (Linux)
 * @code
 * #include <filum.h>
 *
 * static float global_model[192];
 * static FLHerald herald;
 *
 * int main(void) {
 *     FLHeraldConfig cfg = {
 *         .serial_port = "/dev/ttyUSB0",
 *         .baud_rate   = 115200,
 *         .model_param_count = 192,
 *         .global_model      = global_model,
 *         .aggregator        = FL_AGG_FEDAVG,
 *     };
 *     fl_herald_init(&herald, &cfg);
 *     fl_herald_run(&herald);
 * }
 * @endcode
 *
 * @section memory Memory Model
 *
 * All buffer sizes are compile-time constants in fl_config.h.
 * Override via CMake: -DFL_MODEL_MAX_PARAMS=256
 *
 * Default RAM budget per component:
 * - FLModel:      ~32 KB  (FL_MODEL_MAX_PARAMS=4096)
 * - FLShard:      ~22 KB  (excluding FLModel)
 * - FLAggregator: ~1 MB   (Herald-only, coordinate-wise median)
 *
 * For STM32F103 (20KB RAM): use -DFL_CONFIG_PRESET_TINY
 * For STM32F411 (128KB RAM): use -DFL_CONFIG_PRESET_SMALL or default
 *
 * @section threading Thread Safety
 *
 * No Filum function is thread-safe. All access to FLShard and FLHerald
 * instances must be externally synchronized. The recommended pattern is
 * one task/thread per Shard on FreeRTOS, or a single-threaded event loop
 * on the Herald.
 *
 * @section versioning Versioning
 *
 * Wire protocol version: FL_WIRE_VERSION (current: 1)
 * Library version: FL_VERSION_STRING (current: 0.2.0)
 *
 * Shards and Heralds must use the same FL_WIRE_VERSION to interoperate.
 * Library API stability follows Semantic Versioning 2.0.
 */

/* Version and configuration - always include first */
#include "fl_version.h"
#include "fl_config.h"
#include "fl_error.h"

/* Common: wire protocol, quantization, sparse encoding, DP, crypto */
#include "fl_frame.h"
#include "fl_quant.h"
#include "fl_sparse.h"
#include "fl_model.h"
#include "fl_dp.h"
#include "fl_crypto.h"

/* Shard runtime (MCU side) */
#include "fl_shard.h"
#include "fl_train.h"

/* Herald coordinator (Linux side) */
#include "fl_herald.h"
#include "fl_aggregator.h"
#include "fl_round.h"
#include "fl_fragment_pool.h"

#endif /* FILUM_H */
