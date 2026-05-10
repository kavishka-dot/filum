/*
 * filum/herald/src/fl_round.c
 */

#include "fl_round.h"
#include <string.h>
#include <time.h>

void fl_round_init(FLRoundScheduler *sched, const FLRoundPolicy *policy)
{
    memset(sched, 0, sizeof(FLRoundScheduler));
    sched->policy = *policy;
    sched->phase  = FL_ROUND_IDLE;
}

uint8_t fl_round_open(FLRoundScheduler *sched)
{
    sched->shards_complete = 0;
    sched->shards_partial  = 0;
    sched->window_open_at  = time(NULL);
    sched->window_close_at = sched->window_open_at +
                             (time_t)sched->policy.window_seconds;
    sched->phase           = FL_ROUND_COLLECTING;
    return sched->round_id;
}

int fl_round_shard_complete(FLRoundScheduler *sched, uint16_t shard_id)
{
    (void)shard_id;
    sched->shards_complete++;
    return (sched->shards_complete >= sched->policy.max_shards);
}

int fl_round_should_close(const FLRoundScheduler *sched)
{
    if (sched->phase != FL_ROUND_COLLECTING) return 0;
    if (time(NULL) >= sched->window_close_at)  return 1;
    if (sched->shards_complete >= sched->policy.max_shards) return 1;
    return 0;
}

void fl_round_close(FLRoundScheduler *sched)
{
    sched->phase = FL_ROUND_AGGREGATING;
    sched->rounds_total++;
    sched->round_id = (uint8_t)((sched->round_id + 1) & 0xFF);
}

int fl_round_is_window_open(const FLRoundScheduler *sched)
{
    return sched->phase == FL_ROUND_COLLECTING;
}
