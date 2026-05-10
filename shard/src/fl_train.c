/*
 * filum/shard/src/fl_train.c
 */

#include "fl_train.h"
#include "fl_model.h"
#include "fl_quant.h"
#include <string.h>
#include <math.h>

float fl_loss_forward(const float *pred, const float *target,
                      float *loss_grad, uint8_t n, FLLossType type)
{
    float loss = 0.0f;

    switch (type) {

    case FL_LOSS_MSE:
        for (uint8_t i = 0; i < n; i++) {
            float d = pred[i] - target[i];
            loss += d * d;
            loss_grad[i] = 2.0f * d / (float)n;
        }
        loss /= (float)n;
        break;

    case FL_LOSS_XENTROPY: {
        /* Softmax + cross-entropy fused for numerical stability */
        float max_v = pred[0];
        for (uint8_t i = 1; i < n; i++)
            if (pred[i] > max_v) max_v = pred[i];

        float sum_exp = 0.0f;
        static float softmax[64];
        for (uint8_t i = 0; i < n; i++) {
            softmax[i] = expf(pred[i] - max_v);
            sum_exp += softmax[i];
        }
        for (uint8_t i = 0; i < n; i++) {
            softmax[i] /= sum_exp;
            loss_grad[i] = (softmax[i] - target[i]) / (float)n;
            if (target[i] > 0.5f)
                loss -= logf(softmax[i] + 1e-9f);
        }
        break;
    }
    }

    return loss;
}

uint16_t fl_train_epoch(FLModel *model, const FLTrainConfig *config,
                        FLDataCallback data_cb, void *ctx,
                        float *intermediates)
{
    static FLSample  sample;
    static float     output[64];
    static float     loss_grad[64];

    uint16_t n_samples = 0;
    uint8_t  out_features =
        model->layers[model->layer_count - 1].out_features;

    fl_model_zero_grad(model);

    while (data_cb(ctx, &sample)) {
        /* Forward */
        fl_model_forward(model, sample.input, output, intermediates);

        /* Loss */
        fl_loss_forward(output, sample.label, loss_grad,
                        out_features, config->loss_type);

        /* Backward (accumulate) */
        fl_model_backward(model, loss_grad, sample.input, intermediates);

        /* Per-sample SGD step */
        if (config->grad_clip_norm > 0.0f)
            fl_grad_clip(model->grad, model->total_params,
                         config->grad_clip_norm);

        fl_model_sgd_step(model, config->learning_rate);
        fl_model_zero_grad(model);

        n_samples++;
    }

    return n_samples;
}

uint32_t fl_train_run(FLModel *model, const FLTrainConfig *config,
                      FLDataCallback data_cb, void *ctx)
{
    static float intermediates[FL_MODEL_MAX_PARAMS];
    uint32_t total = 0;
    for (uint8_t e = 0; e < config->epochs; e++)
        total += fl_train_epoch(model, config, data_cb, ctx, intermediates);
    return total;
}
