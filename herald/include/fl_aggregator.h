#ifndef FILUM_FL_AGGREGATOR_H
#define FILUM_FL_AGGREGATOR_H

/*
 * filum/herald/include/fl_aggregator.h
 *
 * Federated aggregation strategies.
 *
 * All strategies operate on dense float gradient vectors.
 * The aggregator accumulates updates incrementally to avoid
 * buffering all shard updates simultaneously.
 */

#include <stdint.h>
#include "fl_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FL_AGG_FEDAVG  = 0x01,   /* Federated Averaging (McMahan et al. 2017) */
    FL_AGG_FEDPROX = 0x02,   /* FedProx with proximal term                */
    FL_AGG_MEDIAN  = 0x03,   /* Coordinate-wise median (Byzantine robust)  */
} FLAggregatorType;

/*
 * FLAggregator - accumulates shard updates and computes global delta.
 * Statically allocated; supports up to 64 shards per round.
 */
#define FL_AGG_MAX_SHARDS  64

/*
 * FL_AGG_MEDIAN_MAX_SHARDS - max shards buffered for coordinate-wise median.
 * Each shard's full update is stored until finalize.
 * Memory: FL_AGG_MEDIAN_MAX_SHARDS * FL_MODEL_MAX_PARAMS * 4 bytes.
 * At 64 shards * 4096 params = 1MB - Herald (Linux) only.
 */
#define FL_AGG_MEDIAN_MAX_SHARDS  64

typedef struct {
    FLAggregatorType type;
    float    accumulator[FL_MODEL_MAX_PARAMS];  /* FedAvg/FedProx running sum */
    float    global_model[FL_MODEL_MAX_PARAMS]; /* reference for FedProx      */
    uint8_t  shard_count;
    uint16_t param_count;
    float    fedprox_mu;   /* proximal term coefficient (FedProx only) */

    /* Median: full update buffer [shard_idx][param_idx] */
    float    median_buf[FL_AGG_MEDIAN_MAX_SHARDS][FL_MODEL_MAX_PARAMS];
    uint16_t median_shard_samples[FL_AGG_MEDIAN_MAX_SHARDS]; /* data sizes */
} FLAggregator;

/*
 * fl_agg_init - initialize aggregator for a new round.
 * global_model: current global model params (copied as reference)
 */
void fl_agg_init(FLAggregator *agg, FLAggregatorType type,
                 const float *global_model, uint16_t param_count);

/*
 * fl_agg_add - incorporate one shard's dense gradient update.
 * Must be called for each complete shard update before fl_agg_finalize.
 */
void fl_agg_add(FLAggregator *agg, const float *shard_update,
                uint16_t n_params, uint16_t shard_data_size);

/*
 * fl_agg_finalize - compute the aggregated delta.
 * delta_out: output buffer, length param_count
 * Applies the aggregation rule and writes the global delta.
 */
void fl_agg_finalize(FLAggregator *agg, float *delta_out);

/*
 * fl_agg_reset - zero accumulator for the next round.
 */
void fl_agg_reset(FLAggregator *agg);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_AGGREGATOR_H */
