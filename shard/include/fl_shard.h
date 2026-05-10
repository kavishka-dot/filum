#ifndef FILUM_FL_SHARD_H
#define FILUM_FL_SHARD_H

/*
 * filum/shard/include/fl_shard.h
 *
 * Public API for the Filum Shard runtime.
 *
 * A Shard is a single edge device (MCU) participating in federated learning.
 * It maintains a local model, trains on private sensor data, and exchanges
 * gradient fragments with the Herald over LoRa.
 *
 * Typical usage:
 *
 *   FLShard shard;
 *   fl_shard_init(&shard, &my_hal, shard_id, &model);
 *
 *   while (1) {
 *       fl_shard_tick(&shard);   // call from main loop or RTOS task
 *       fl_shard_sleep(&shard);  // enter low-power mode between events
 *   }
 */

#include "fl_error.h"
#include "fl_model.h"
#include "fl_frame.h"
#include "fl_sparse.h"
#include "fl_dp.h"
#include "fl_crypto.h"
#include "fl_error.h"
#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */

#define FL_SHARD_TX_BUFFER_SIZE   FL_FRAME_MAX_SIZE
#define FL_SHARD_RX_BUFFER_SIZE   FL_FRAME_MAX_SIZE
#define FL_SHARD_MAX_FRAGS        32      /* max fragments per update */
#define FL_SHARD_RECV_TIMEOUT_MS  2000    /* downlink receive window */
#define FL_SHARD_TOPK_RATIO       10      /* send top 1/10 of gradients */

/* -------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */

typedef enum {
    FL_SHARD_STATE_IDLE,          /* waiting for beacon              */
    FL_SHARD_STATE_TRAINING,      /* running local SGD               */
    FL_SHARD_STATE_ENCODING,      /* sparsifying gradients           */
    FL_SHARD_STATE_TX,            /* transmitting update fragments   */
    FL_SHARD_STATE_AWAIT_ACK,     /* waiting for Herald ACK          */
    FL_SHARD_STATE_RECV_DELTA,    /* receiving model delta from Herald */
    FL_SHARD_STATE_ERROR,         /* unrecoverable error, needs reset */
} FLShardState;

/* -------------------------------------------------------------------------
 * Shard context (all static, no heap)
 * ------------------------------------------------------------------------- */

typedef struct {
    /* Identity */
    uint16_t       shard_id;
    uint8_t        current_round;

    /* Hardware abstraction */
    const FLHal   *hal;

    /* Model */
    FLModel       *model;

    /* Sparse gradient buffer for outgoing updates */
    FLSparseBuffer update_buf;

    /* Sparse gradient buffer for incoming delta */
    FLSparseBuffer delta_buf;

    /* Residual accumulation for error feedback */
    float          residual[FL_MODEL_MAX_PARAMS];

    /* Transmission state */
    uint8_t        tx_frag_sent;    /* how many fragments sent so far */
    uint8_t        tx_frag_total;   /* total fragments to send */
    uint32_t       tx_duty_reset_ms; /* when duty cycle budget resets */

    /* Reception state */
    uint8_t        rx_frag_recv;
    uint8_t        rx_frag_total;

    /* Round parameters (from beacon) */
    FLBeaconPayload beacon;

    /* State */
    FLShardState   state;

    /* TX/RX buffers */
    uint8_t        tx_buf[FL_SHARD_TX_BUFFER_SIZE];
    uint8_t        rx_buf[FL_SHARD_RX_BUFFER_SIZE];

    /* Differential Privacy */
    FLDPConfig     dp;             /* DP config (dp_enabled=0 disables)   */

    /* Encryption */
    FLCryptoCtx    crypto;         /* per-shard ECDH + ChaCha20 context   */

    /* Stats */
    uint32_t       rounds_completed;
    uint32_t       tx_errors;
    uint32_t       rx_errors;
} FLShard;

/* -------------------------------------------------------------------------
 * Security configuration helpers
 * ------------------------------------------------------------------------- */

/*
 * fl_shard_enable_dp - enable differential privacy with given parameters.
 * Call after fl_shard_init. Safe to call multiple times.
 *
 * epsilon:     privacy budget per round (smaller = more private, less accurate)
 * delta:       failure probability (typically 1e-5)
 * sensitivity: L2 clip norm (match FLTrainConfig.grad_clip_norm)
 */
/**
 * @brief Enable differential privacy on this shard.
 * @return FL_OK or FL_ERR_INVALID_ARG if parameters are out of range.
 */
FLError fl_shard_enable_dp(FLShard *shard,
                           float epsilon, float delta, float sensitivity);

/*
 * fl_shard_enable_crypto - initialize ECDH keypair for this shard.
 * seed: 32 bytes of entropy. NULL = use fixed test seed (debug only).
 * After calling this, the shard will send FL_FRAME_HANDSHAKE on next beacon.
 */
/**
 * @brief Initialize ECDH keypair for this shard.
 * @param seed 32 bytes of entropy. NULL uses a fixed test seed (debug only).
 * @return FL_OK always (keypair generation cannot fail with valid inputs).
 */
FLError fl_shard_enable_crypto(FLShard *shard, const uint8_t *seed);

/*
 * fl_shard_privacy_report - log current privacy budget spent.
 */
/**
 * @brief Log the cumulative privacy budget spent so far.
 *
 * Uses basic composition: epsilon_total = rounds_spent * epsilon_per_round.
 * Output goes to hal->log if non-NULL; does nothing otherwise.
 *
 * @param[in] shard  Shard context. Must not be NULL.
 */
void fl_shard_privacy_report(const FLShard *shard);

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/*
 * fl_shard_init - initialize shard context.
 *
 * shard:    pre-allocated FLShard (stack or static)
 * hal:      hardware abstraction layer, must remain valid for lifetime
 * shard_id: unique 16-bit device ID (burned into flash or derived from UID)
 * model:    pre-initialized FLModel (see fl_model_init)
 *
 * Returns 0 on success.
 */
/**
 * @brief Initialize a Shard context.
 * @return FL_OK or FL_ERR_INVALID_ARG.
 */
FLError fl_shard_init(FLShard       *shard,
                      const FLHal   *hal,
                      uint16_t       shard_id,
                      FLModel       *model);

/*
 * fl_shard_tick - advance the shard state machine.
 *
 * Call this from your main loop or a FreeRTOS task.
 * Handles: listening for beacon, training, encoding, transmitting, ACK wait.
 * Returns the next state after processing.
 */
/**
 * @brief Advance the shard state machine by one step.
 *
 * Call from your main loop or FreeRTOS task. Each call handles one
 * transition: receives a packet, trains one epoch, sends one fragment, etc.
 * Returns the new state after processing.
 *
 * @param[in,out] shard  Shard context. Must not be NULL.
 * @return               Current ::FLShardState after this tick.
 */
FLShardState fl_shard_tick(FLShard *shard);

/*
 * fl_shard_on_rx - feed a received LoRa packet into the shard.
 *
 * Call from your LoRa RX interrupt or DIO0 callback.
 * raw:     received bytes
 * raw_len: length
 */
/**
 * @brief Feed a received LoRa packet into the shard.
 *
 * Decodes the frame and dispatches it to the appropriate handler.
 * Call from your LoRa RX interrupt or DIO0 callback, or from fl_shard_tick.
 *
 * @param[in,out] shard    Shard context. Must not be NULL.
 * @param[in]     raw      Received bytes. Must not be NULL.
 * @param[in]     raw_len  Number of received bytes.
 */
void fl_shard_on_rx(FLShard *shard, const uint8_t *raw, size_t raw_len);

/*
 * fl_shard_sleep - enter low-power mode.
 *
 * Calls hal->deep_sleep_until_rtc with a sleep duration appropriate for
 * the current state (e.g., if beacon is expected in 3600s, sleep 3590s).
 */
/**
 * @brief Enter low-power mode appropriate for the current state.
 *
 * Calls hal->deep_sleep_rtc() when IDLE (long sleep until next beacon),
 * or hal->sleep_ms() for short inter-tick delays in active states.
 *
 * @param[in,out] shard  Shard context. Must not be NULL.
 */
void fl_shard_sleep(FLShard *shard);

/*
 * fl_shard_state_str - human-readable state name.
 */
/**
 * @brief Return a human-readable name for a shard state.
 * @param[in] state  State to describe.
 * @return           Pointer to a static string; do not free.
 */
const char *fl_shard_state_str(FLShardState state);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_SHARD_H */
