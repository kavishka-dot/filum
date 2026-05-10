#ifndef FILUM_FL_SPARSE_H
#define FILUM_FL_SPARSE_H

/*
 * filum/common/include/fl_sparse.h
 *
 * Sparse gradient encoding/decoding.
 *
 * Filum sends only the top-k gradient entries by absolute magnitude.
 * This dramatically reduces LoRa transmission time.
 *
 * Wire encoding per entry: 2B param_index + 1B delta_q8 = 3 bytes.
 * At FL_PAYLOAD_MAX=214: 71 entries per packet.
 *
 * Sparsity strategy: top-k selection by |gradient| magnitude.
 * Residual accumulation (error feedback) is supported to prevent
 * information loss across rounds.
 */

#include <stdint.h>
#include "fl_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FLSparseBuffer - holds encoded sparse gradient entries.
 * Statically sized; no heap allocation.
 */
#define FL_SPARSE_MAX_ENTRIES  1024

typedef struct {
    FLGradientEntry entries[FL_SPARSE_MAX_ENTRIES];
    uint16_t        count;
} FLSparseBuffer;

/* -------------------------------------------------------------------------
 * Encoding
 * ------------------------------------------------------------------------- */

/*
 * fl_sparse_encode - select top-k entries from dense gradient vector.
 *
 * grad:        dense float gradient, length param_count
 * param_count: total parameters
 * topk:        number of entries to select (clamped to FL_SPARSE_MAX_ENTRIES)
 * residual:    if non-NULL, accumulated residual from previous rounds.
 *              grad[i] += residual[i] before selection.
 *              After selection, unselected residual[i] += grad[i].
 * out:         output sparse buffer (must be pre-allocated by caller)
 *
 * Returns number of entries written (== min(topk, param_count)).
 */
uint16_t fl_sparse_encode(const float    *grad,
                          uint16_t        param_count,
                          uint16_t        topk,
                          float          *residual,
                          FLSparseBuffer *out);

/* -------------------------------------------------------------------------
 * Decoding
 * ------------------------------------------------------------------------- */

/*
 * fl_sparse_decode - scatter sparse entries into dense gradient vector.
 *
 * buf:         sparse buffer from fl_sparse_encode or packet reassembly
 * grad:        output dense gradient vector (zeroed by caller or accumulated)
 * param_count: length of grad
 * accumulate:  if nonzero, adds to grad[]; if zero, zeroes grad first
 */
void fl_sparse_decode(const FLSparseBuffer *buf,
                      float               *grad,
                      uint16_t             param_count,
                      int                  accumulate);

/* -------------------------------------------------------------------------
 * Fragmentation helpers
 * ------------------------------------------------------------------------- */

/*
 * fl_sparse_fragment_count - how many LoRa packets needed to send buf.
 */
uint8_t fl_sparse_fragment_count(const FLSparseBuffer *buf);

/*
 * fl_sparse_write_fragment - write fragment frag_index of buf into payload.
 *
 * payload:       output buffer, must be FL_PAYLOAD_MAX bytes
 * payload_len:   actual bytes written
 * buf:           full sparse buffer
 * frag_index:    which fragment to write
 *
 * Returns 0 on success, -1 if frag_index out of range.
 */
int fl_sparse_write_fragment(uint8_t              *payload,
                             uint8_t              *payload_len,
                             const FLSparseBuffer *buf,
                             uint8_t               frag_index);

/*
 * fl_sparse_read_fragment - parse gradient entries from a received payload.
 *
 * payload:     received bytes
 * payload_len: length
 * out:         destination buffer (caller manages count accumulation)
 *
 * Returns number of entries parsed, -1 on format error.
 */
int fl_sparse_read_fragment(const uint8_t  *payload,
                            uint8_t         payload_len,
                            FLSparseBuffer *out);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_SPARSE_H */
