/*
 * filum/common/src/fl_quant.c
 */

#include "fl_quant.h"
#include <math.h>
#include <string.h>

float fl_grad_linf(const float *v, uint16_t n)
{
    float m = 0.0f;
    for (uint16_t i = 0; i < n; i++) {
        float a = v[i] < 0.0f ? -v[i] : v[i];
        if (a > m) m = a;
    }
    return m;
}

float fl_grad_l2(const float *v, uint16_t n)
{
    float s = 0.0f;
    for (uint16_t i = 0; i < n; i++) s += v[i] * v[i];
    return sqrtf(s);
}

void fl_grad_clip(float *v, uint16_t n, float max_norm)
{
    float norm = fl_grad_l2(v, n);
    if (norm > max_norm) {
        float scale = max_norm / (norm + 1e-7f);
        for (uint16_t i = 0; i < n; i++) v[i] *= scale;
    }
}

void fl_quant_vec_q8(const float *src, int8_t *dst,
                     uint16_t n, float *scale_out)
{
    if (scale_out) {
        float norm = fl_grad_linf(src, n);
        if (norm < 1e-9f) norm = 1.0f;
        *scale_out = norm;
        for (uint16_t i = 0; i < n; i++)
            dst[i] = fl_quant_q8(src[i] / norm);
    } else {
        for (uint16_t i = 0; i < n; i++)
            dst[i] = fl_quant_q8(src[i]);
    }
}

void fl_dequant_vec_q8(const int8_t *src, float *dst,
                       uint16_t n, float scale)
{
    float factor = (scale != 0.0f) ? (scale / FL_Q8_SCALE) : (1.0f / FL_Q8_SCALE);
    for (uint16_t i = 0; i < n; i++)
        dst[i] = (float)src[i] * factor;
}
