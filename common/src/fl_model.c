/*
 * filum - fl_model.c
 * Model initialization, weight management, SGD step.
 */

#include "fl_model.h"
#include <string.h>

/* ── PRNG ──────────────────────────────────────────────────────────────── */

uint32_t fl_xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

fl_f32 fl_rand_uniform(uint32_t *state, fl_f32 low, fl_f32 high)
{
    uint32_t r = fl_xorshift32(state);
    /* map to [0, 1) */
    fl_f32 f = (fl_f32)(r >> 8) / (fl_f32)(1 << 24);
    return low + f * (high - low);
}

/* ── fl_model_init ─────────────────────────────────────────────────────── */

void fl_model_init(FLModel *model, const FLModelDesc *desc,
                   fl_f32 *weights, fl_f32 *grads)
{
    model->desc      = desc;
    model->weights   = weights;
    model->gradients = grads;
    memset(weights, 0, desc->total_params * sizeof(fl_f32));
    memset(grads,   0, desc->total_params * sizeof(fl_f32));
}

/* ── fl_model_xavier_init ──────────────────────────────────────────────── */
/*
 * Xavier uniform: U(-sqrt(6/(fan_in+fan_out)), +sqrt(6/(fan_in+fan_out)))
 * Applied to linear layer weights only; biases initialized to zero.
 */

static fl_f32 fl_sqrtf(fl_f32 x)
{
    /* Newton-Raphson sqrt — good enough for init, avoids libm dependency   */
    if (x <= 0.0f) return 0.0f;
    fl_f32 r = x;
    for (int i = 0; i < 16; i++)
        r = 0.5f * (r + x / r);
    return r;
}

void fl_model_xavier_init(FLModel *model, uint32_t *rng_state)
{
    const FLModelDesc *desc = model->desc;
    for (uint8_t l = 0; l < desc->layer_count; l++) {
        const FLLayerDesc *layer = &desc->layers[l];
        if (layer->type != FL_LAYER_LINEAR) continue;

        uint16_t fan_in  = layer->in_features;
        uint16_t fan_out = layer->out_features;
        fl_f32   limit   = fl_sqrtf(6.0f / (fl_f32)(fan_in + fan_out));

        /* weights */
        fl_f32 *w = &model->weights[layer->weight_offset];
        for (uint16_t i = 0; i < layer->weight_count; i++)
            w[i] = fl_rand_uniform(rng_state, -limit, limit);

        /* biases = 0 */
        fl_f32 *b = &model->weights[layer->bias_offset];
        for (uint16_t i = 0; i < layer->bias_count; i++)
            b[i] = 0.0f;
    }
}

/* ── fl_model_zero_grad ────────────────────────────────────────────────── */

void fl_model_zero_grad(FLModel *model)
{
    memset(model->gradients, 0,
           model->desc->total_params * sizeof(fl_f32));
}

/* ── fl_model_copy_weights ─────────────────────────────────────────────── */

void fl_model_copy_weights(FLModel *dst, const FLModel *src)
{
    memcpy(dst->weights, src->weights,
           src->desc->total_params * sizeof(fl_f32));
}

/* ── fl_model_sgd_step ─────────────────────────────────────────────────── */

void fl_model_sgd_step(FLModel *model, fl_f32 lr)
{
    uint16_t n = model->desc->total_params;
    for (uint16_t i = 0; i < n; i++)
        model->weights[i] -= lr * model->gradients[i];
}

/* ── fl_model_layer_weights ────────────────────────────────────────────── */

fl_f32 *fl_model_layer_weights(FLModel *model, uint8_t layer_idx)
{
    return &model->weights[model->desc->layers[layer_idx].weight_offset];
}

/* ── fl_model_layer_biases ─────────────────────────────────────────────── */

fl_f32 *fl_model_layer_biases(FLModel *model, uint8_t layer_idx)
{
    return &model->weights[model->desc->layers[layer_idx].bias_offset];
}
