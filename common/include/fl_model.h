#ifndef FILUM_FL_MODEL_H
#define FILUM_FL_MODEL_H

/*
 * filum/common/include/fl_model.h
 *
 * Portable model descriptor for Filum.
 *
 * A FLModel is a flat array of float parameters with a layer descriptor
 * table. The layout is fully static - no dynamic allocation.
 *
 * Memory layout:
 *   params[0 .. layer[0].param_count - 1]          <- layer 0 weights
 *   params[layer[0].offset .. +layer[1].param_count] <- layer 1 weights
 *   ...
 *
 * This matches the wire format: param_index is a global flat index.
 *
 * Supported layer types:
 *   FL_LAYER_LINEAR  - fully connected (weight matrix + bias vector)
 *   FL_LAYER_BN      - batch norm (scale + shift, no running stats on MCU)
 *
 * Training is restricted to these types so backward pass can be
 * hand-coded without autograd.
 */

#include "fl_error.h"
#include "fl_config.h"
#include <stdint.h>
#include <stddef.h>
#include "fl_error.h"
#include "fl_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FL_MODEL_MAX_LAYERS and FL_MODEL_MAX_PARAMS are defined in fl_config.h */
/* Override via CMake: -DFL_MODEL_MAX_PARAMS=256 */

typedef enum {
    FL_LAYER_LINEAR = 0x01,
    FL_LAYER_BN     = 0x02,
} FLLayerType;

typedef enum {
    FL_ACT_NONE    = 0x00,
    FL_ACT_RELU    = 0x01,
    FL_ACT_SIGMOID = 0x02,
    FL_ACT_TANH    = 0x03,
} FLActivation;

typedef struct {
    FLLayerType  type;
    FLActivation activation;
    uint16_t     in_features;
    uint16_t     out_features;
    uint16_t     param_offset;   /* offset into FLModel.params */
    uint16_t     param_count;    /* weights + biases for this layer */
} FLLayerDesc;

typedef struct {
    float        params[FL_MODEL_MAX_PARAMS];
    float        grad[FL_MODEL_MAX_PARAMS];    /* accumulated gradient */
    FLLayerDesc  layers[FL_MODEL_MAX_LAYERS];
    uint8_t      layer_count;
    uint16_t     total_params;
    uint8_t      round_id;                     /* last round applied */
} FLModel;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/*
 * fl_model_init - zero-initialize model and set layer descriptors.
 * layers_desc: array of layer descriptions, length layer_count.
 * Returns 0 on success, -1 if total params exceed FL_MODEL_MAX_PARAMS.
 */
/**
 * @brief Initialize model with a layer descriptor table.
 * @return FL_OK, FL_ERR_INVALID_ARG, or FL_ERR_CAPACITY.
 */
FLError fl_model_init(FLModel           *model,
                      const FLLayerDesc *layers_desc,
                      uint8_t            layer_count);

/*
 * fl_model_init_random - initialize params with Xavier uniform.
 * Call after fl_model_init.
 * seed: for deterministic init (Herald and Shard start from same weights).
 */
void fl_model_init_random(FLModel *model, uint32_t seed);

/* -------------------------------------------------------------------------
 * Forward / backward
 * ------------------------------------------------------------------------- */

/*
 * fl_model_forward - run inference.
 * input:  feature vector, length model->layers[0].in_features
 * output: prediction, length model->layers[last].out_features
 * intermediates: scratch buffer, length FL_MODEL_MAX_PARAMS (caller provides)
 */
void fl_model_forward(const FLModel *model,
                      const float   *input,
                      float         *output,
                      float         *intermediates);

/*
 * fl_model_backward - compute gradients and accumulate into model->grad.
 * loss_grad: dL/doutput, length of last layer output
 * input:     original input to forward pass
 * intermediates: same buffer passed to fl_model_forward
 */
void fl_model_backward(FLModel     *model,
                       const float *loss_grad,
                       const float *input,
                       float       *intermediates);

/*
 * fl_model_zero_grad - zero model->grad before a new backward pass.
 */
void fl_model_zero_grad(FLModel *model);

/* -------------------------------------------------------------------------
 * Parameter update
 * ------------------------------------------------------------------------- */

/*
 * fl_model_sgd_step - in-place SGD update: params -= lr * grad.
 */
void fl_model_sgd_step(FLModel *model, float lr);

/*
 * fl_model_apply_delta - add a dense gradient delta to params.
 * Used by Shard to apply the Herald's global model delta.
 */
void fl_model_apply_delta(FLModel *model, const float *delta, uint16_t len);

/* -------------------------------------------------------------------------
 * Serialization
 * ------------------------------------------------------------------------- */

/*
 * fl_model_save / fl_model_load - serialize params to/from raw bytes.
 * buf must be at least model->total_params * sizeof(float) bytes.
 */
void fl_model_save(const FLModel *model, uint8_t *buf, size_t buf_len);
/**
 * @brief Load model parameters from raw bytes.
 * @return FL_OK or FL_ERR_BUFFER_TOO_SMALL.
 */
FLError fl_model_load(FLModel *model, const uint8_t *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_MODEL_H */
