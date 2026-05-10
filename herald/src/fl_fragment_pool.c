/*
 * filum/herald/src/fl_fragment_pool.c
 */

#include "fl_fragment_pool.h"
#include <string.h>
#include <time.h>

void fl_pool_init(FLFragmentPool *pool, uint32_t frag_timeout_s)
{
    memset(pool, 0, sizeof(FLFragmentPool));
    pool->frag_timeout_s = frag_timeout_s;
}

static FLFragSlot *find_slot(FLFragmentPool *pool, uint16_t shard_id)
{
    for (int i = 0; i < FL_POOL_MAX_SHARDS; i++)
        if (pool->slots[i].state == FL_FRAG_SLOT_ACTIVE &&
            pool->slots[i].shard_id == shard_id)
            return &pool->slots[i];
    return NULL;
}

static FLFragSlot *alloc_slot(FLFragmentPool *pool)
{
    for (int i = 0; i < FL_POOL_MAX_SHARDS; i++)
        if (pool->slots[i].state == FL_FRAG_SLOT_EMPTY)
            return &pool->slots[i];
    return NULL;
}

int fl_pool_ingest(FLFragmentPool *pool, const FLFrame *frame, uint8_t payload_len)
{
    uint16_t shard_id  = frame->shard_id;
    uint8_t  frag_idx  = frame->frag_index;
    uint8_t  frag_tot  = frame->frag_total;

    if (frag_tot > FL_POOL_MAX_FRAGS) return -1;

    FLFragSlot *slot = find_slot(pool, shard_id);
    if (!slot) {
        if (frag_idx != 0) return -1;   /* missed first fragment */
        slot = alloc_slot(pool);
        if (!slot) return -1;           /* pool full */

        memset(slot, 0, sizeof(FLFragSlot));
        slot->state      = FL_FRAG_SLOT_ACTIVE;
        slot->shard_id   = shard_id;
        slot->round_id   = frame->round_id;
        slot->frag_total = frag_tot;
    }

    /* Duplicate check */
    uint8_t bit = (uint8_t)(1u << (frag_idx % 8));
    if (slot->frag_bitmap[frag_idx / 8] & bit) return 0;  /* duplicate */

    slot->frag_bitmap[frag_idx / 8] |= bit;
    slot->frag_received++;
    slot->last_frag_at = time(NULL);

    fl_sparse_read_fragment(frame->payload, payload_len, &slot->sparse_buf);

    if (slot->frag_received >= slot->frag_total) {
        slot->state = FL_FRAG_SLOT_COMPLETE;
        pool->total_complete++;
        return 1;
    }
    return 0;
}

const FLSparseBuffer *fl_pool_get_complete(const FLFragmentPool *pool,
                                           uint16_t shard_id)
{
    for (int i = 0; i < FL_POOL_MAX_SHARDS; i++) {
        const FLFragSlot *s = &pool->slots[i];
        if (s->state == FL_FRAG_SLOT_COMPLETE && s->shard_id == shard_id)
            return &s->sparse_buf;
    }
    return NULL;
}

void fl_pool_release(FLFragmentPool *pool, uint16_t shard_id)
{
    for (int i = 0; i < FL_POOL_MAX_SHARDS; i++)
        if (pool->slots[i].shard_id == shard_id)
            memset(&pool->slots[i], 0, sizeof(FLFragSlot));
}

int fl_pool_expire(FLFragmentPool *pool)
{
    int expired = 0;
    time_t now  = time(NULL);
    for (int i = 0; i < FL_POOL_MAX_SHARDS; i++) {
        FLFragSlot *s = &pool->slots[i];
        if (s->state == FL_FRAG_SLOT_ACTIVE &&
            (now - s->last_frag_at) > (time_t)pool->frag_timeout_s) {
            s->state = FL_FRAG_SLOT_EXPIRED;
            pool->total_dropped++;
            memset(s, 0, sizeof(FLFragSlot));
            expired++;
        }
    }
    return expired;
}

uint8_t fl_pool_complete_count(const FLFragmentPool *pool)
{
    uint8_t n = 0;
    for (int i = 0; i < FL_POOL_MAX_SHARDS; i++)
        if (pool->slots[i].state == FL_FRAG_SLOT_COMPLETE) n++;
    return n;
}
