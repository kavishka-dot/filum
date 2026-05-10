/*
 * filum/common/src/fl_sparse.c
 *
 * Top-k sparse gradient selection and fragmentation.
 * No dynamic allocation; uses a simple partial selection sort.
 * For small param counts (< 4096) this is fast enough on MCU.
 */

#include "fl_sparse.h"
#include "fl_quant.h"
#include "fl_model.h"
#include <string.h>
#include <stdint.h>

/* Partial selection sort to find top-k by absolute magnitude.
 * O(n*k) - acceptable for n < 4096, k < 200. */
uint16_t fl_sparse_encode(const float    *grad,
                          uint16_t        param_count,
                          uint16_t        topk,
                          float          *residual,
                          FLSparseBuffer *out)
{
    if (topk > FL_SPARSE_MAX_ENTRIES) topk = FL_SPARSE_MAX_ENTRIES;
    if (topk > param_count)           topk = param_count;

    /* Temporary buffer: effective gradient = grad + residual */
    static float eff[FL_MODEL_MAX_PARAMS];  /* static: no stack blow-up on MCU */
    if (param_count > FL_MODEL_MAX_PARAMS)  param_count = FL_MODEL_MAX_PARAMS;

    for (uint16_t i = 0; i < param_count; i++) {
        eff[i] = grad[i];
        if (residual) eff[i] += residual[i];
    }

    /* Track which indices have been selected */
    static uint8_t selected[FL_MODEL_MAX_PARAMS / 8 + 1];
    memset(selected, 0, (param_count / 8) + 1);

    out->count = 0;

    for (uint16_t k = 0; k < topk; k++) {
        float   best_val = 0.0f;
        uint16_t best_idx = 0xFFFF;

        for (uint16_t i = 0; i < param_count; i++) {
            if (selected[i / 8] & (1u << (i % 8))) continue;
            float a = eff[i] < 0.0f ? -eff[i] : eff[i];
            if (best_idx == 0xFFFF || a > best_val) {
                best_val = a;
                best_idx = i;
            }
        }

        if (best_idx == 0xFFFF || best_val < 1e-9f) break;

        selected[best_idx / 8] |= (uint8_t)(1u << (best_idx % 8));
        out->entries[out->count].param_index = best_idx;
        out->entries[out->count].delta_q8    = fl_quant_q8(eff[best_idx]);
        out->count++;
    }

    /* Update residual: unselected entries carry forward */
    if (residual) {
        for (uint16_t i = 0; i < param_count; i++) {
            if (selected[i / 8] & (1u << (i % 8)))
                residual[i] = 0.0f;
            else
                residual[i] = eff[i];
        }
    }

    return out->count;
}

void fl_sparse_decode(const FLSparseBuffer *buf,
                      float               *grad,
                      uint16_t             param_count,
                      int                  accumulate)
{
    if (!accumulate)
        memset(grad, 0, param_count * sizeof(float));

    for (uint16_t i = 0; i < buf->count; i++) {
        uint16_t idx = buf->entries[i].param_index;
        if (idx < param_count)
            grad[idx] += fl_dequant_q8(buf->entries[i].delta_q8);
    }
}

uint8_t fl_sparse_fragment_count(const FLSparseBuffer *buf)
{
    uint16_t per_frag = FL_GRADIENT_ENTRIES_PER_FRAME;
    return (uint8_t)((buf->count + per_frag - 1) / per_frag);
}

int fl_sparse_write_fragment(uint8_t              *payload,
                             uint8_t              *payload_len,
                             const FLSparseBuffer *buf,
                             uint8_t               frag_index)
{
    uint16_t per_frag = FL_GRADIENT_ENTRIES_PER_FRAME;
    uint16_t start    = (uint16_t)frag_index * per_frag;
    if (start >= buf->count) return -1;

    uint16_t count = buf->count - start;
    if (count > per_frag) count = per_frag;

    uint8_t  entry_size = (uint8_t)sizeof(FLGradientEntry);
    *payload_len = (uint8_t)(count * entry_size);
    memcpy(payload, &buf->entries[start], *payload_len);
    return 0;
}

int fl_sparse_read_fragment(const uint8_t  *payload,
                            uint8_t         payload_len,
                            FLSparseBuffer *out)
{
    uint8_t entry_size = (uint8_t)sizeof(FLGradientEntry);
    if (payload_len % entry_size != 0) return -1;

    uint16_t count = payload_len / entry_size;
    if (out->count + count > FL_SPARSE_MAX_ENTRIES) return -1;

    memcpy(&out->entries[out->count], payload, payload_len);
    out->count += count;
    return (int)count;
}
