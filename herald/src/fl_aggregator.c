/*
 * filum/herald/src/fl_aggregator.c
 *
 * Aggregation strategies:
 *   FedAvg  - weighted average (McMahan et al. 2017)
 *   FedProx - FedAvg + proximal regularization
 *   Median  - coordinate-wise median (Byzantine robust)
 */

#include "fl_aggregator.h"
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Comparison function for qsort (used in median)
 * ------------------------------------------------------------------------- */
static int cmp_float(const void *a, const void *b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void fl_agg_init(FLAggregator *agg, FLAggregatorType type,
                 const float *global_model, uint16_t param_count)
{
    agg->type        = type;
    agg->shard_count = 0;
    agg->param_count = param_count;
    memset(agg->accumulator, 0, param_count * sizeof(float));
    if (global_model)
        memcpy(agg->global_model, global_model, param_count * sizeof(float));
}

void fl_agg_add(FLAggregator *agg, const float *shard_update,
                uint16_t n_params, uint16_t shard_data_size)
{
    if (n_params > agg->param_count) n_params = agg->param_count;
    if (agg->shard_count >= FL_AGG_MEDIAN_MAX_SHARDS) return;

    switch (agg->type) {

    case FL_AGG_FEDAVG:
        /* Unweighted mean — simplest, works well when data sizes are similar */
        for (uint16_t i = 0; i < n_params; i++)
            agg->accumulator[i] += shard_update[i];
        break;

    case FL_AGG_FEDPROX:
        /* Same accumulation as FedAvg; proximal correction in finalize */
        for (uint16_t i = 0; i < n_params; i++)
            agg->accumulator[i] += shard_update[i];
        break;

    case FL_AGG_MEDIAN:
        /* Buffer the full update for coordinate-wise median at finalize */
        memcpy(agg->median_buf[agg->shard_count], shard_update,
               n_params * sizeof(float));
        agg->median_shard_samples[agg->shard_count] = shard_data_size;
        break;
    }

    agg->shard_count++;
    (void)shard_data_size;
}

void fl_agg_finalize(FLAggregator *agg, float *delta_out)
{
    if (agg->shard_count == 0) {
        memset(delta_out, 0, agg->param_count * sizeof(float));
        return;
    }

    switch (agg->type) {

    case FL_AGG_FEDAVG:
        for (uint16_t i = 0; i < agg->param_count; i++)
            delta_out[i] = agg->accumulator[i] / (float)agg->shard_count;
        break;

    case FL_AGG_FEDPROX:
        /*
         * FedProx correction:
         *   delta = avg_delta - mu * (avg_params - global_params)
         *
         * The proximal term pulls the aggregated update toward the global
         * model, reducing drift from heterogeneous (non-IID) data.
         * mu = 0 reduces to FedAvg.
         */
        for (uint16_t i = 0; i < agg->param_count; i++) {
            float avg = agg->accumulator[i] / (float)agg->shard_count;
            delta_out[i] = avg - agg->fedprox_mu *
                           (avg - agg->global_model[i]);
        }
        break;

    case FL_AGG_MEDIAN: {
        /*
         * Coordinate-wise median:
         *   For each parameter i, collect values from all shards,
         *   sort, take the middle value.
         *
         * Byzantine robustness: up to (n-1)/2 malicious shards cannot
         * move the median more than the largest honest gradient magnitude.
         *
         * Cost: O(n_shards * log(n_shards)) per parameter.
         * Acceptable on Herald (Linux); never runs on MCU.
         */
        static float col[FL_AGG_MEDIAN_MAX_SHARDS];
        for (uint16_t i = 0; i < agg->param_count; i++) {
            for (uint8_t s = 0; s < agg->shard_count; s++)
                col[s] = agg->median_buf[s][i];
            qsort(col, agg->shard_count, sizeof(float), cmp_float);
            if (agg->shard_count % 2 == 1)
                delta_out[i] = col[agg->shard_count / 2];
            else
                delta_out[i] = (col[agg->shard_count/2 - 1] +
                                col[agg->shard_count/2]) * 0.5f;
        }
        break;
    }
    }
}

void fl_agg_reset(FLAggregator *agg)
{
    agg->shard_count = 0;
    memset(agg->accumulator, 0, agg->param_count * sizeof(float));
}
