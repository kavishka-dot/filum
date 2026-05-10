#ifndef FILUM_FL_DP_H
#define FILUM_FL_DP_H

/*
 * filum/common/include/fl_dp.h
 *
 * Differential Privacy for Filum gradient updates.
 *
 * Implements the Gaussian mechanism for (epsilon, delta)-DP.
 * Noise is added to the parameter delta on the Shard BEFORE sparse
 * encoding and transmission. The Herald never sees raw gradients.
 *
 * Mechanism:
 *   1. Clip gradient L2 norm to sensitivity S (gradient clipping).
 *   2. Add Gaussian noise N(0, sigma^2) to each coordinate.
 *      sigma = S * sqrt(2 * ln(1.25/delta)) / epsilon
 *
 * Privacy accounting:
 *   Per-round: (epsilon_r, delta_r)
 *   Over T rounds (basic composition): (T * epsilon_r, T * delta_r)
 *   Moments accountant gives tighter bounds but requires floating-point
 *   log/exp which is available on STM32F411 (FPU).
 *
 * Usage:
 *   FLDPConfig dp = fl_dp_config(1.0f, 1e-5f, 1.0f);  // eps=1, delta=1e-5
 *   fl_dp_apply(&dp, grad, n_params, shard_id);
 *
 * Setting dp_enabled=0 in FLDPConfig disables DP entirely (zero overhead).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    epsilon;       /* privacy budget per round                     */
    float    delta;         /* failure probability per round                */
    float    sensitivity;   /* L2 sensitivity (= gradient clip norm)        */
    float    sigma;         /* noise std dev, computed from eps/delta/S     */
    uint8_t  dp_enabled;    /* 0 = no-op, 1 = add noise                    */
    uint32_t rounds_spent;  /* basic composition counter                    */
} FLDPConfig;

/*
 * fl_dp_config - compute sigma from (epsilon, delta, sensitivity).
 * Call once at init. Returns a ready-to-use FLDPConfig.
 *
 * Recommended values for a typical FL deployment:
 *   epsilon    = 1.0  (strong privacy)
 *   delta      = 1e-5 (standard for user-level DP)
 *   sensitivity= 1.0  (match grad_clip_norm in FLTrainConfig)
 */
FLDPConfig fl_dp_config(float epsilon, float delta, float sensitivity);

/*
 * fl_dp_apply - add calibrated Gaussian noise to gradient vector in-place.
 *
 * grad:      parameter delta vector, length n
 * n:         number of parameters
 * shard_id:  used to seed per-shard RNG (ensures different noise per device)
 * round_id:  mixed into RNG seed for per-round uniqueness
 *
 * If dp_enabled == 0, this is a no-op.
 * Increments cfg->rounds_spent for basic composition tracking.
 */
void fl_dp_apply(FLDPConfig *cfg, float *grad, uint16_t n,
                 uint16_t shard_id, uint8_t round_id);

/*
 * fl_dp_privacy_spent - return total (epsilon, delta) spent so far
 * under basic composition.
 *
 * epsilon_total = rounds_spent * cfg->epsilon
 * delta_total   = rounds_spent * cfg->delta
 */
void fl_dp_privacy_spent(const FLDPConfig *cfg,
                         float *epsilon_total, float *delta_total);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_DP_H */
