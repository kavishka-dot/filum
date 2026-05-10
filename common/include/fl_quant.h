#ifndef FILUM_FL_QUANT_H
#define FILUM_FL_QUANT_H

/*
 * filum/common/include/fl_quant.h
 *
 * Fixed-point quantization for gradient compression.
 *
 * Wire format: Q8 signed fixed-point.
 * Encoding:  q = clamp(round(x * 128), -128, 127)
 * Decoding:  x = q / 128.0
 *
 * For gradients outside [-1,1], callers pre-normalize by L-inf norm.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_Q8_SCALE  128.0f

/* --- Scalar ------------------------------------------------------------ */

static inline int8_t fl_quant_q8(float x)
{
    float s = x * FL_Q8_SCALE;
    if (s >  127.0f) return  127;
    if (s < -128.0f) return -128;
    return (int8_t)(int32_t)s;
}

static inline float fl_dequant_q8(int8_t q)
{
    return (float)q / FL_Q8_SCALE;
}

/* --- Vector ------------------------------------------------------------ */

/*
 * fl_quant_vec_q8 - quantize n floats to Q8.
 * If scale_out != NULL: normalizes by L-inf norm first, writes norm to *scale_out.
 * If scale_out == NULL: assumes src already in [-1,1).
 */
void fl_quant_vec_q8(const float *src, int8_t *dst,
                     uint16_t n, float *scale_out);

/*
 * fl_dequant_vec_q8 - dequantize n Q8 values.
 * scale: the norm written by fl_quant_vec_q8 (0.0f = use FL_Q8_SCALE).
 */
void fl_dequant_vec_q8(const int8_t *src, float *dst,
                       uint16_t n, float scale);

/* --- Gradient utilities ------------------------------------------------ */

float fl_grad_linf(const float *v, uint16_t n);
float fl_grad_l2(const float *v, uint16_t n);
void  fl_grad_clip(float *v, uint16_t n, float max_norm);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_QUANT_H */
