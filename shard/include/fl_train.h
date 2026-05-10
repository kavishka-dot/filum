#ifndef FILUM_FL_TRAIN_H
#define FILUM_FL_TRAIN_H

/*
 * filum/shard/include/fl_train.h
 *
 * Local training primitives for Shard.
 *
 * Filum restricts on-device training to simple SGD over linear models.
 * No autograd - backward kernels are hand-coded for the supported layer types.
 *
 * Sensor data is provided through a callback to avoid buffering large
 * datasets in RAM. The callback returns one sample at a time.
 */

#include "fl_model.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FLSample - one training sample.
 * Caller fills input (features) and label (class index or regression target).
 */
typedef struct {
    float    input[64];     /* max input features */
    float    label[8];      /* one-hot or regression target */
    uint8_t  input_len;
    uint8_t  label_len;
} FLSample;

/*
 * FLDataCallback - called by fl_train_epoch to get the next sample.
 * ctx:    user-provided context pointer
 * sample: output, filled by callback
 * Returns 1 if sample is valid, 0 if no more samples (end of epoch).
 */
typedef int (*FLDataCallback)(void *ctx, FLSample *sample);

/* -------------------------------------------------------------------------
 * Loss functions
 * ------------------------------------------------------------------------- */

typedef enum {
    FL_LOSS_MSE      = 0x01,   /* mean squared error (regression) */
    FL_LOSS_XENTROPY = 0x02,   /* cross-entropy (classification)  */
} FLLossType;

/*
 * fl_loss_forward - compute scalar loss and loss gradient w.r.t. output.
 *
 * pred:      model output, length n
 * target:    ground truth, length n
 * loss_grad: output, dL/d(pred), length n
 * n:         output dimension
 *
 * Returns scalar loss value.
 */
float fl_loss_forward(const float *pred,
                      const float *target,
                      float       *loss_grad,
                      uint8_t      n,
                      FLLossType   type);

/* -------------------------------------------------------------------------
 * Training loop
 * ------------------------------------------------------------------------- */

/*
 * FLTrainConfig - hyperparameters for local training.
 */
typedef struct {
    uint8_t    epochs;          /* number of passes over local data */
    float      learning_rate;   /* SGD step size                    */
    float      grad_clip_norm;  /* L2 clip before accumulation (0 = no clip) */
    FLLossType loss_type;
    uint8_t    topk_ratio;      /* 1/topk_ratio of params sent (e.g. 10 = 10%) */
} FLTrainConfig;

/*
 * fl_train_epoch - run one epoch of SGD on the local dataset.
 *
 * model:     model to train (params updated in-place)
 * config:    training hyperparameters
 * data_cb:   callback providing training samples
 * ctx:       passed through to data_cb
 * intermediates: scratch buffer, FL_MODEL_MAX_PARAMS floats (caller provides)
 *
 * Returns number of samples processed.
 */
uint16_t fl_train_epoch(FLModel           *model,
                        const FLTrainConfig *config,
                        FLDataCallback      data_cb,
                        void               *ctx,
                        float              *intermediates);

/*
 * fl_train_run - run multiple epochs (calls fl_train_epoch in a loop).
 * Returns total samples processed across all epochs.
 */
uint32_t fl_train_run(FLModel            *model,
                      const FLTrainConfig *config,
                      FLDataCallback      data_cb,
                      void               *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_TRAIN_H */
