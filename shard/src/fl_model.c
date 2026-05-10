/*
 * filum/shard/src/fl_model.c
 */

#include "fl_model.h"
#include "fl_error.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

/* --- Lifecycle ----------------------------------------------------------- */

FLError fl_model_init(FLModel *model, const FLLayerDesc *layers, uint8_t count)
{
    if (!model) return FL_ERR_INVALID_ARG;
    memset(model, 0, sizeof(FLModel));
    if (count > FL_MODEL_MAX_LAYERS) return FL_ERR_CAPACITY;

    uint16_t offset = 0;
    for (uint8_t i = 0; i < count; i++) {
        model->layers[i]              = layers[i];
        model->layers[i].param_offset = offset;
        offset += layers[i].param_count;
        if (offset > FL_MODEL_MAX_PARAMS) return FL_ERR_CAPACITY;
    }
    model->layer_count  = count;
    model->total_params = offset;
    return FL_OK;
}

/* Tiny LCG for deterministic init - no stdlib rand dependency */
static uint32_t lcg_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float lcg_uniform(uint32_t *state, float lo, float hi)
{
    uint32_t r = lcg_next(state);
    float f = (float)(r >> 8) / (float)(1 << 24);  /* [0,1) */
    return lo + f * (hi - lo);
}

void fl_model_init_random(FLModel *model, uint32_t seed)
{
    uint32_t state = seed;
    for (uint8_t l = 0; l < model->layer_count; l++) {
        FLLayerDesc *ld = &model->layers[l];
        /* Xavier uniform: limit = sqrt(6 / (fan_in + fan_out)) */
        float limit = sqrtf(6.0f / (float)(ld->in_features + ld->out_features));
        for (uint16_t i = 0; i < ld->param_count; i++)
            model->params[ld->param_offset + i] =
                lcg_uniform(&state, -limit, limit);
    }
}

/* --- Activations --------------------------------------------------------- */

static float apply_act(float x, FLActivation act)
{
    switch (act) {
        case FL_ACT_RELU:    return x > 0.0f ? x : 0.0f;
        case FL_ACT_SIGMOID: return 1.0f / (1.0f + expf(-x));
        case FL_ACT_TANH:    return tanhf(x);
        default:             return x;
    }
}

static float act_grad(float activated, FLActivation act)
{
    switch (act) {
        case FL_ACT_RELU:    return activated > 0.0f ? 1.0f : 0.0f;
        case FL_ACT_SIGMOID: return activated * (1.0f - activated);
        case FL_ACT_TANH:    return 1.0f - activated * activated;
        default:             return 1.0f;
    }
}

/* --- Forward ------------------------------------------------------------- */

void fl_model_forward(const FLModel *model, const float *input,
                      float *output, float *intermediates)
{
    const float *in = input;
    float *inter    = intermediates;

    for (uint8_t l = 0; l < model->layer_count; l++) {
        const FLLayerDesc *ld = &model->layers[l];
        const float *W        = &model->params[ld->param_offset];
        const float *b        = W + ld->in_features * ld->out_features;
        float       *out      = inter + l * ld->out_features;

        for (uint16_t j = 0; j < ld->out_features; j++) {
            float sum = b[j];
            for (uint16_t i = 0; i < ld->in_features; i++)
                sum += W[j * ld->in_features + i] * in[i];
            out[j] = apply_act(sum, ld->activation);
        }
        in = out;
    }

    /* Copy last layer output */
    const FLLayerDesc *last = &model->layers[model->layer_count - 1];
    float *last_out = inter + (model->layer_count - 1) * last->out_features;
    for (uint16_t j = 0; j < last->out_features; j++)
        output[j] = last_out[j];
}

/* --- Backward ------------------------------------------------------------ */

void fl_model_zero_grad(FLModel *model)
{
    memset(model->grad, 0, model->total_params * sizeof(float));
}

void fl_model_backward(FLModel *model, const float *loss_grad,
                       const float *input, float *intermediates)
{
    /* delta_l: dL/d(pre-activation output) of layer l */
    static float delta[FL_MODEL_MAX_PARAMS];
    static float delta_prev[FL_MODEL_MAX_PARAMS];

    const float *d_in = loss_grad;

    for (int l = (int)model->layer_count - 1; l >= 0; l--) {
        const FLLayerDesc *ld = &model->layers[l];
        const float *W        = &model->params[ld->param_offset];
        float *dW             = &model->grad[ld->param_offset];
        float *db             = dW + ld->in_features * ld->out_features;
        float *act_out        = intermediates + l * ld->out_features;
        const float *layer_in = (l == 0) ? input
                              : intermediates + (l-1) * model->layers[l-1].out_features;

        /* Compute delta = d_in * act'(activated) */
        for (uint16_t j = 0; j < ld->out_features; j++)
            delta[j] = d_in[j] * act_grad(act_out[j], ld->activation);

        /* Accumulate weight gradients: dW[j,i] += delta[j] * layer_in[i] */
        for (uint16_t j = 0; j < ld->out_features; j++) {
            for (uint16_t i = 0; i < ld->in_features; i++)
                dW[j * ld->in_features + i] += delta[j] * layer_in[i];
            db[j] += delta[j];
        }

        /* Backprop delta to previous layer: delta_prev[i] = sum_j W[j,i]*delta[j] */
        if (l > 0) {
            for (uint16_t i = 0; i < ld->in_features; i++) {
                float s = 0.0f;
                for (uint16_t j = 0; j < ld->out_features; j++)
                    s += W[j * ld->in_features + i] * delta[j];
                delta_prev[i] = s;
            }
            d_in = delta_prev;
        }
    }
}

/* --- SGD step ------------------------------------------------------------ */

void fl_model_sgd_step(FLModel *model, float lr)
{
    for (uint16_t i = 0; i < model->total_params; i++)
        model->params[i] -= lr * model->grad[i];
}

void fl_model_apply_delta(FLModel *model, const float *delta, uint16_t len)
{
    if (len > model->total_params) len = model->total_params;
    for (uint16_t i = 0; i < len; i++)
        model->params[i] += delta[i];
}

/* --- Serialization ------------------------------------------------------- */

void fl_model_save(const FLModel *model, uint8_t *buf, size_t buf_len)
{
    size_t needed = model->total_params * sizeof(float);
    if (buf_len < needed) return;
    memcpy(buf, model->params, needed);
}

FLError fl_model_load(FLModel *model, const uint8_t *buf, size_t buf_len)
{
    if (!model || !buf) return FL_ERR_INVALID_ARG;
    size_t needed = model->total_params * sizeof(float);
    if (buf_len < needed) return FL_ERR_BUFFER_TOO_SMALL;
    memcpy(model->params, buf, needed);
    return FL_OK;
}
