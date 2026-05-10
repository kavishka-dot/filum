#ifndef FILUM_FL_HERALD_H
#define FILUM_FL_HERALD_H

/*
 * filum/herald/include/fl_herald.h
 *
 * Herald coordinator public API.
 *
 * The Herald runs on a Linux host (Raspberry Pi, BeagleBone, x86 server)
 * and manages: round lifecycle, fragment reassembly, aggregation, delta
 * distribution, and LoRa gateway communication.
 *
 * Usage:
 *
 *   FLHerald h;
 *   fl_herald_init(&h, &config);
 *   fl_herald_run(&h);   // blocking event loop
 */

#include "fl_error.h"
#include "fl_frame.h"
#include "fl_round.h"
#include "fl_aggregator.h"
#include "fl_fragment_pool.h"
#include "fl_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Herald configuration
 * ------------------------------------------------------------------------- */

typedef struct {
    /* Transport */
    const char *serial_port;        /* e.g. "/dev/ttyUSB0" (to LoRa gateway) */
    uint32_t    baud_rate;          /* typically 115200 */

    /* Model */
    uint16_t    model_param_count;  /* total parameters in global model */
    float      *global_model;       /* pointer to global model params */

    /* Round policy */
    FLRoundPolicy round_policy;

    /* Aggregation */
    FLAggregatorType aggregator;

    /* Callbacks (optional) */
    void (*on_round_complete)(uint8_t round_id, uint8_t shards_aggregated);
    void (*on_shard_update)(uint16_t shard_id, uint8_t round_id);
    void (*on_error)(const char *msg);

    /* Logging */
    int  log_level;   /* 0=off, 1=error, 2=info, 3=debug */
} FLHeraldConfig;

/* -------------------------------------------------------------------------
 * Herald context
 * ------------------------------------------------------------------------- */

typedef struct {
    FLHeraldConfig   config;
    FLRoundScheduler scheduler;
    FLFragmentPool   frag_pool;
    FLAggregator     aggregator;

    uint8_t          current_round;
    int              serial_fd;       /* serial port file descriptor */
    volatile int     running;
} FLHerald;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/*
 * fl_herald_init - initialize Herald with config.
 * Opens serial port, sets up fragment pool and round scheduler.
 * Returns 0 on success, -1 on error.
 */
/**
 * @brief Initialize Herald and open serial port.
 * @return FL_OK or FL_ERR_TRANSPORT if serial port cannot be opened.
 */
FLError fl_herald_init(FLHerald *herald, const FLHeraldConfig *config);

/*
 * fl_herald_run - blocking event loop.
 * Handles: TX beacons, RX fragments, aggregation, TX deltas.
 * Returns when herald->running is set to 0 (e.g. from signal handler).
 */
/**
 * @brief Run the blocking Herald event loop.
 * @return FL_OK when stopped cleanly via fl_herald_stop().
 */
FLError fl_herald_run(FLHerald *herald);

/*
 * fl_herald_stop - signal the event loop to exit cleanly.
 * Safe to call from a signal handler.
 */
void fl_herald_stop(FLHerald *herald);

/*
 * fl_herald_send_beacon - broadcast a new round beacon immediately.
 * Normally called automatically by the round scheduler.
 */
/**
 * @brief Broadcast a BEACON frame to all shards.
 * @return FL_OK or FL_ERR_TRANSPORT.
 */
FLError fl_herald_send_beacon(FLHerald *herald, uint8_t round_id);

/*
 * fl_herald_send_delta - send aggregated model delta to all shards.
 * delta:        dense float delta, length config.model_param_count
 * topk:         how many entries to send (sparse compression)
 */
/**
 * @brief Send aggregated model delta to all shards.
 * @return FL_OK or FL_ERR_TRANSPORT.
 */
FLError fl_herald_send_delta(FLHerald *herald, const float *delta, uint16_t topk);

/*
 * fl_herald_init_fd - initialize Herald with a pre-opened file descriptor.
 * Useful for testing via pipes, or custom transports (SPI, CAN, pty).
 * The fd must be readable and writable; Herald does not close it on destroy.
 */
/**
 * @brief Initialize Herald with a pre-opened file descriptor.
 * @return FL_OK or FL_ERR_INVALID_ARG.
 */
FLError fl_herald_init_fd(FLHerald *herald, const FLHeraldConfig *config, int fd);

/*
 * fl_herald_destroy - release resources (close serial port, free state).
 */
void fl_herald_destroy(FLHerald *herald);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_HERALD_H */
