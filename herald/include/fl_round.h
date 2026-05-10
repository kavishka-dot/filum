#ifndef FILUM_FL_ROUND_H
#define FILUM_FL_ROUND_H

/*
 * filum/herald/include/fl_round.h
 *
 * Round scheduler - manages the lifecycle of a federated learning round.
 *
 * A round:
 *   1. Herald broadcasts a BEACON
 *   2. Collection window opens (time-bounded, not barrier-synchronized)
 *   3. Shards transmit UPDATE fragments over LoRa
 *   4. Herald reassembles and acknowledges each complete update
 *   5. Window closes when: timeout reached OR min_shards received
 *   6. Aggregation runs on collected updates
 *   7. Herald broadcasts DELTA to all shards
 *   8. Herald broadcasts ROUND_CLOSE
 *   9. Next round begins after inter_round_delay_s
 */

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Round policy
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t window_seconds;       /* how long to collect updates */
    uint32_t inter_round_delay_s;  /* gap between rounds */
    uint8_t  min_shards;           /* proceed with at least this many */
    uint8_t  max_shards;           /* cap (ignore late arrivals after this) */
    uint8_t  local_epochs;         /* sent to shards in beacon */
    float    learning_rate;        /* sent to shards in beacon */
} FLRoundPolicy;

/* -------------------------------------------------------------------------
 * Round scheduler
 * ------------------------------------------------------------------------- */

typedef enum {
    FL_ROUND_IDLE,
    FL_ROUND_COLLECTING,
    FL_ROUND_AGGREGATING,
    FL_ROUND_DISTRIBUTING,
} FLRoundPhase;

typedef struct {
    FLRoundPolicy policy;
    FLRoundPhase  phase;
    uint8_t       round_id;
    time_t        window_open_at;
    time_t        window_close_at;
    uint8_t       shards_complete;   /* full updates received */
    uint8_t       shards_partial;    /* incomplete (dropped after window) */
    uint32_t      rounds_total;
} FLRoundScheduler;

/*
 * fl_round_init - initialize scheduler with policy.
 */
void fl_round_init(FLRoundScheduler *sched, const FLRoundPolicy *policy);

/*
 * fl_round_open - start a new collection round.
 * Returns the round_id assigned.
 */
uint8_t fl_round_open(FLRoundScheduler *sched);

/*
 * fl_round_shard_complete - mark a shard's update as fully received.
 * Returns 1 if we should close the window early (max_shards reached).
 */
int fl_round_shard_complete(FLRoundScheduler *sched, uint16_t shard_id);

/*
 * fl_round_should_close - returns 1 if the window should close now.
 * Checks: timeout expired OR min_shards met AND max_shards reached.
 */
int fl_round_should_close(const FLRoundScheduler *sched);

/*
 * fl_round_close - finalize the round, advance to AGGREGATING phase.
 */
void fl_round_close(FLRoundScheduler *sched);

/*
 * fl_round_is_window_open - returns 1 if currently collecting.
 */
int fl_round_is_window_open(const FLRoundScheduler *sched);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_ROUND_H */
