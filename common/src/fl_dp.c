/*
 * filum/common/src/fl_dp.c
 *
 * Differential Privacy — Gaussian mechanism.
 * Pure C99, no external libraries, runs on STM32F411 with FPU.
 */

#include "fl_dp.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal PCG32 RNG (O'Neill 2014) — better statistical quality than LCG.
 * Each shard seeds independently so noise is uncorrelated across devices.
 * ------------------------------------------------------------------------- */

typedef struct { uint64_t state; uint64_t inc; } PCG32;

static uint32_t pcg32_next(PCG32 *rng) {
    uint64_t old = rng->state;
    rng->state   = old * 6364136223846793005ULL + (rng->inc | 1);
    uint32_t xsh = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xsh >> rot) | (xsh << ((-rot) & 31));
}

static void pcg32_seed(PCG32 *rng, uint64_t seed, uint64_t seq) {
    rng->state = 0; rng->inc = (seq << 1u) | 1u;
    pcg32_next(rng);
    rng->state += seed;
    pcg32_next(rng);
}

static float pcg32_uniform(PCG32 *rng) {
    return (float)(pcg32_next(rng) >> 8) / (float)(1 << 24);
}

/* Box-Muller transform — produces N(0,1) sample */
static float pcg32_normal(PCG32 *rng) {
    float u1 = pcg32_uniform(rng) + 1e-7f;
    float u2 = pcg32_uniform(rng);
    return sqrtf(-2.0f * logf(u1)) * cosf(6.283185307f * u2);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

FLDPConfig fl_dp_config(float epsilon, float delta, float sensitivity) {
    FLDPConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.epsilon     = epsilon;
    cfg.delta       = delta;
    cfg.sensitivity = sensitivity;
    cfg.dp_enabled  = 1;
    cfg.rounds_spent= 0;

    /*
     * Gaussian mechanism sigma calibration:
     *   sigma = S * sqrt(2 * ln(1.25 / delta)) / epsilon
     *
     * Derived from Dwork & Roth (2014), Theorem A.1.
     * Provides (epsilon, delta)-DP guarantee per round.
     */
    if (epsilon > 0.0f && delta > 0.0f && sensitivity > 0.0f) {
        float log_term = logf(1.25f / delta);
        cfg.sigma = sensitivity * sqrtf(2.0f * log_term) / epsilon;
    } else {
        cfg.dp_enabled = 0;
    }
    return cfg;
}

void fl_dp_apply(FLDPConfig *cfg, float *grad, uint16_t n,
                 uint16_t shard_id, uint8_t round_id) {
    if (!cfg->dp_enabled || cfg->sigma <= 0.0f) return;

    /*
     * Seed the per-shard, per-round RNG.
     * Combining shard_id and round_id ensures:
     *   - Different noise per shard (prevents averaging attack)
     *   - Different noise per round (prevents replay)
     */
    PCG32 rng;
    uint64_t seed = ((uint64_t)shard_id << 32) ^ ((uint64_t)round_id << 16) ^ 0xDEADBEEFULL;
    pcg32_seed(&rng, seed, 0xC0FFEEC0FFEEULL);

    /* Add N(0, sigma^2) to each coordinate */
    for (uint16_t i = 0; i < n; i++)
        grad[i] += cfg->sigma * pcg32_normal(&rng);

    cfg->rounds_spent++;
}

void fl_dp_privacy_spent(const FLDPConfig *cfg,
                         float *epsilon_total, float *delta_total) {
    /* Basic composition — loose but simple and MCU-friendly */
    *epsilon_total = (float)cfg->rounds_spent * cfg->epsilon;
    *delta_total   = (float)cfg->rounds_spent * cfg->delta;
}
