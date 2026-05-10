#ifndef FILUM_FL_FRAGMENT_POOL_H
#define FILUM_FL_FRAGMENT_POOL_H

/*
 * filum/herald/include/fl_fragment_pool.h
 *
 * Fragment reassembly pool for the Herald.
 *
 * LoRa updates arrive as multi-packet streams. This pool tracks
 * incoming fragments per shard and signals when a full update
 * has been reassembled.
 *
 * Max concurrent shards: FL_POOL_MAX_SHARDS
 * Max fragments per shard: FL_POOL_MAX_FRAGS
 */

#include "fl_frame.h"
#include "fl_sparse.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_POOL_MAX_SHARDS  64
#define FL_POOL_MAX_FRAGS   32

typedef enum {
    FL_FRAG_SLOT_EMPTY    = 0,
    FL_FRAG_SLOT_ACTIVE   = 1,
    FL_FRAG_SLOT_COMPLETE = 2,
    FL_FRAG_SLOT_EXPIRED  = 3,
} FLFragSlotState;

typedef struct {
    FLFragSlotState state;
    uint16_t        shard_id;
    uint8_t         round_id;
    uint8_t         frag_total;
    uint8_t         frag_received;
    uint8_t         frag_bitmap[FL_POOL_MAX_FRAGS / 8 + 1];
    FLSparseBuffer  sparse_buf;
    time_t          last_frag_at;   /* for timeout expiry */
} FLFragSlot;

typedef struct {
    FLFragSlot  slots[FL_POOL_MAX_SHARDS];
    uint32_t    frag_timeout_s;   /* drop incomplete slots after this */
    uint32_t    total_complete;
    uint32_t    total_dropped;
} FLFragmentPool;

/*
 * fl_pool_init - initialize pool with fragment timeout.
 */
void fl_pool_init(FLFragmentPool *pool, uint32_t frag_timeout_s);

/*
 * fl_pool_ingest - process an incoming UPDATE frame.
 *
 * Returns:
 *   1  if this was the completing fragment (update ready to aggregate)
 *   0  if more fragments expected
 *  -1  on error (bad frame, pool full, duplicate)
 */
int fl_pool_ingest(FLFragmentPool *pool, const FLFrame *frame,
                   uint8_t payload_len);

/*
 * fl_pool_get_complete - get pointer to a complete sparse update buffer.
 * shard_id: which shard
 * Returns NULL if not yet complete.
 */
const FLSparseBuffer *fl_pool_get_complete(const FLFragmentPool *pool,
                                           uint16_t shard_id);

/*
 * fl_pool_release - mark a completed slot as empty (after aggregation).
 */
void fl_pool_release(FLFragmentPool *pool, uint16_t shard_id);

/*
 * fl_pool_expire - scan and drop slots older than frag_timeout_s.
 * Returns number of slots expired.
 */
int fl_pool_expire(FLFragmentPool *pool);

/*
 * fl_pool_complete_count - number of complete (unprocessed) updates.
 */
uint8_t fl_pool_complete_count(const FLFragmentPool *pool);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_FRAGMENT_POOL_H */
