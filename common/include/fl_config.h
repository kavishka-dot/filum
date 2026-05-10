#ifndef FILUM_FL_CONFIG_H
#define FILUM_FL_CONFIG_H

/**
 * @file fl_config.h
 * @brief Compile-time memory and capacity configuration.
 *
 * All static buffer sizes in Filum are controlled here.
 * Override any define via CMake: -DFL_MODEL_MAX_PARAMS=256
 *
 * RAM budget calculator (approximate):
 *
 *   FLModel:
 *     params[]   = FL_MODEL_MAX_PARAMS * 4 bytes
 *     grad[]     = FL_MODEL_MAX_PARAMS * 4 bytes
 *     layers[]   = FL_MODEL_MAX_LAYERS * 16 bytes
 *     total      = FL_MODEL_MAX_PARAMS * 8 + FL_MODEL_MAX_LAYERS * 16
 *
 *   FLShard (excl. model):
 *     update_buf = FL_SPARSE_MAX_ENTRIES * 3 + 4 bytes
 *     delta_buf  = FL_SPARSE_MAX_ENTRIES * 3 + 4 bytes
 *     residual[] = FL_MODEL_MAX_PARAMS * 4 bytes
 *     tx/rx bufs = FL_FRAME_MAX_SIZE * 2
 *     total      ≈ FL_MODEL_MAX_PARAMS * 4 + FL_SPARSE_MAX_ENTRIES * 6
 *
 *   FLAggregator (Herald only, Linux):
 *     median_buf = FL_AGG_MEDIAN_MAX_SHARDS * FL_MODEL_MAX_PARAMS * 4 bytes
 *
 * Preset configurations:
 *
 *   Preset          PARAMS  LAYERS  SPARSE  Notes
 *   TINY (MCU-XS)     64      4      32     STM32F103, 20KB RAM budget
 *   SMALL (MCU-S)    256      8     128     STM32F411, 50KB RAM budget
 *   MEDIUM (default) 4096    16    1024     STM32F411 full, Linux
 *   LARGE (Herald)  16384    32    2048     Linux Herald only
 *
 * Select a preset by defining FL_CONFIG_PRESET before including this file,
 * or define individual values directly.
 */

/* -------------------------------------------------------------------------
 * Preset selection
 * ------------------------------------------------------------------------- */

#if defined(FL_CONFIG_PRESET_TINY)
#  define FL_MODEL_MAX_PARAMS    64
#  define FL_MODEL_MAX_LAYERS     4
#  define FL_SPARSE_MAX_ENTRIES  32
#  define FL_AGG_MEDIAN_MAX_SHARDS 16

#elif defined(FL_CONFIG_PRESET_SMALL)
#  define FL_MODEL_MAX_PARAMS   256
#  define FL_MODEL_MAX_LAYERS     8
#  define FL_SPARSE_MAX_ENTRIES 128
#  define FL_AGG_MEDIAN_MAX_SHARDS 32

#elif defined(FL_CONFIG_PRESET_LARGE)
#  define FL_MODEL_MAX_PARAMS  16384
#  define FL_MODEL_MAX_LAYERS     32
#  define FL_SPARSE_MAX_ENTRIES 2048
#  define FL_AGG_MEDIAN_MAX_SHARDS 64
#endif

/* -------------------------------------------------------------------------
 * Individual overrides (or defaults when no preset selected)
 * Each can be overridden via CMake: -DFL_MODEL_MAX_PARAMS=256
 * ------------------------------------------------------------------------- */

/**
 * @brief Maximum number of floating-point parameters in a model.
 *
 * RAM cost: FL_MODEL_MAX_PARAMS * 8 bytes (params + grad).
 * Default 4096 = 32KB. For STM32F103 use 64 or 128.
 */
#ifndef FL_MODEL_MAX_PARAMS
#  define FL_MODEL_MAX_PARAMS  4096
#endif

/**
 * @brief Maximum number of layers in a model descriptor.
 * RAM cost: FL_MODEL_MAX_LAYERS * 16 bytes.
 */
#ifndef FL_MODEL_MAX_LAYERS
#  define FL_MODEL_MAX_LAYERS  16
#endif

/**
 * @brief Maximum number of sparse gradient entries in one FLSparseBuffer.
 *
 * RAM cost: FL_SPARSE_MAX_ENTRIES * 3 bytes per buffer.
 * Shard holds 2 buffers (update + delta): FL_SPARSE_MAX_ENTRIES * 6 bytes.
 * Must be >= (FL_MODEL_MAX_PARAMS / topk_ratio) to avoid truncation.
 */
#ifndef FL_SPARSE_MAX_ENTRIES
#  define FL_SPARSE_MAX_ENTRIES  1024
#endif

/**
 * @brief Maximum concurrent shards buffered for coordinate-wise median.
 * Herald (Linux) only. RAM cost: FL_AGG_MEDIAN_MAX_SHARDS * FL_MODEL_MAX_PARAMS * 4 bytes.
 */
#ifndef FL_AGG_MEDIAN_MAX_SHARDS
#  define FL_AGG_MEDIAN_MAX_SHARDS  64
#endif

/**
 * @brief Maximum concurrent shards tracked by FLFragmentPool.
 * Herald only. RAM cost: FL_POOL_MAX_SHARDS * sizeof(FLFragSlot).
 */
#ifndef FL_POOL_MAX_SHARDS
#  define FL_POOL_MAX_SHARDS  64
#endif

/**
 * @brief Maximum fragments per shard in FLFragmentPool.
 */
#ifndef FL_POOL_MAX_FRAGS
#  define FL_POOL_MAX_FRAGS  32
#endif

/* -------------------------------------------------------------------------
 * Derived RAM budget estimates (informational, in bytes)
 * ------------------------------------------------------------------------- */

/** Approximate RAM used by one FLModel instance. */
#define FL_RAM_MODEL \
    (FL_MODEL_MAX_PARAMS * 8 + FL_MODEL_MAX_LAYERS * 16 + 16)

/** Approximate RAM used by one FLShard instance (excluding FLModel). */
#define FL_RAM_SHARD \
    (FL_SPARSE_MAX_ENTRIES * 6 + FL_MODEL_MAX_PARAMS * 4 + 224 + 64)

/** Approximate RAM used by FLAggregator with median (Herald only). */
#define FL_RAM_AGGREGATOR \
    (FL_AGG_MEDIAN_MAX_SHARDS * FL_MODEL_MAX_PARAMS * 4 + FL_MODEL_MAX_PARAMS * 8 + 16)

#endif /* FILUM_FL_CONFIG_H */
