#ifndef FILUM_FL_FRAME_H
#define FILUM_FL_FRAME_H

/**
 * @file fl_frame.h
 * @brief Filum wire protocol frame definitions.
 *
 * Every LoRa packet is a ::FLFrame. The header is 10 bytes fixed.
 * Payload is up to ::FL_PAYLOAD_MAX bytes depending on spreading factor.
 *
 * Frame layout (little-endian):
 *
 * | Offset | Size | Field       | Description                          |
 * |--------|------|-------------|--------------------------------------|
 * | 0      | 2    | magic       | Always 0x464C ('FL')                  |
 * | 2      | 1    | frame_type  | See ::FLFrameType                    |
 * | 3      | 2    | shard_id    | Sender ID; 0xFFFF = Herald           |
 * | 5      | 1    | round_id    | FL round number, wraps at 255        |
 * | 6      | 1    | frag_index  | 0-based fragment index               |
 * | 7      | 1    | frag_total  | Total fragments (1 = single packet)  |
 * | 8      | 2    | crc16       | CRC-16/CCITT-FALSE over [0..7]+payload|
 * | 10     | N    | payload     | Up to ::FL_PAYLOAD_MAX bytes         |
 *
 * Gradient entries are packed as 3 bytes each:
 * - 2 bytes: param_index (uint16_t, little-endian)
 * - 1 byte:  delta_q8 (int8_t, Q8 fixed-point: real = delta_q8 / 128.0)
 *
 * At SF7 (222-byte LoRa payload, 10-byte header): 71 gradient entries/packet.
 */

#include "fl_error.h"
#include "fl_config.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** @brief Frame magic bytes: ASCII 'FL'. */
#define FL_MAGIC              0x464Cu

/** @brief Fixed frame header size in bytes. */
#define FL_HEADER_SIZE        10u

/**
 * @brief Maximum payload size in bytes.
 *
 * Conservative budget for SF7 at 125 kHz bandwidth.
 * Reduce to 51 for SF12 deployments by setting FL_LORA_SF=12.
 */
#define FL_PAYLOAD_MAX        214u

/** @brief Maximum total frame size (header + payload). */
#define FL_FRAME_MAX_SIZE     (FL_HEADER_SIZE + FL_PAYLOAD_MAX)

/** @brief Shard ID used by the Herald in all outgoing frames. */
#define FL_SHARD_ID_HERALD    0xFFFFu

/** @brief Sentinel round ID meaning "no round active". */
#define FL_ROUND_ID_NONE      0xFFu

/* -------------------------------------------------------------------------
 * Frame types
 * ------------------------------------------------------------------------- */

/**
 * @brief Frame type codes carried in FLFrame::frame_type.
 */
typedef enum {
    /** Herald -> Shard: new FL round announced. Payload: ::FLBeaconPayload. */
    FL_FRAME_BEACON       = 0x01,

    /** Herald -> Shard: aggregated model delta fragment. Payload: ::FLGradientEntry[]. */
    FL_FRAME_DELTA        = 0x02,

    /** Shard -> Herald: local gradient update fragment. Payload: ::FLGradientEntry[]. */
    FL_FRAME_UPDATE       = 0x03,

    /** Herald -> Shard: all update fragments received. Payload: empty. */
    FL_FRAME_ACK          = 0x04,

    /** Herald -> Shard: round is closed; shard may sleep. Payload: ::FLRoundClosePayload. */
    FL_FRAME_ROUND_CLOSE  = 0x05,

    /** Shard -> Herald: ECDH public key for session establishment. Payload: ::FLHandshakePayload. */
    FL_FRAME_HANDSHAKE    = 0x06,

    /** Herald -> Shard: ECDH public key reply. Payload: ::FLHandshakePayload. */
    FL_FRAME_HANDSHAKE_ACK= 0x07,
} FLFrameType;

/* -------------------------------------------------------------------------
 * Raw frame struct
 * ------------------------------------------------------------------------- */

/**
 * @brief Wire-format frame. Packed for direct memory-to-radio transmission.
 *
 * Do not read or write fields directly. Use ::fl_frame_encode and
 * ::fl_frame_decode to guarantee correct byte order and CRC computation.
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;                   /**< Must equal ::FL_MAGIC.          */
    uint8_t  frame_type;              /**< ::FLFrameType value.            */
    uint16_t shard_id;                /**< Sender ID.                      */
    uint8_t  round_id;                /**< FL round number.                */
    uint8_t  frag_index;              /**< Fragment index (0-based).       */
    uint8_t  frag_total;              /**< Total fragments for this update.*/
    uint16_t crc16;                   /**< CRC-16/CCITT-FALSE.             */
    uint8_t  payload[FL_PAYLOAD_MAX]; /**< Frame payload bytes.            */
} FLFrame;

/* -------------------------------------------------------------------------
 * Payload structures
 * ------------------------------------------------------------------------- */

/**
 * @brief One sparse gradient entry. Three bytes on the wire.
 *
 * Dequantization: @c real_value = delta_q8 / 128.0f
 *
 * At FL_PAYLOAD_MAX=214 bytes: 71 entries per packet.
 */
typedef struct __attribute__((packed)) {
    uint16_t param_index; /**< Index into flattened parameter vector. */
    int8_t   delta_q8;   /**< Quantized gradient in Q8 signed fixed-point. */
} FLGradientEntry;

/** @brief Maximum gradient entries per frame. */
#define FL_GRADIENT_ENTRIES_PER_FRAME     ((uint8_t)(FL_PAYLOAD_MAX / sizeof(FLGradientEntry)))

/**
 * @brief Payload for ::FL_FRAME_BEACON.
 *
 * Herald broadcasts this to open a new FL round. Shards use the embedded
 * hyperparameters for local training.
 */
typedef struct __attribute__((packed)) {
    uint8_t  round_id;           /**< Round identifier (0-255, wraps).          */
    uint32_t window_seconds;     /**< Collection window duration in seconds.     */
    uint16_t model_param_count;  /**< Total parameters in the global model.      */
    uint8_t  min_shards;         /**< Minimum shards before aggregation.         */
    uint8_t  local_epochs;       /**< Local SGD epochs shards should run.        */
    float    learning_rate;      /**< Local SGD learning rate.                   */
    float    dp_sigma;           /**< DP noise std dev (0.0 = DP disabled).      */
} FLBeaconPayload;

/**
 * @brief Payload for ::FL_FRAME_ROUND_CLOSE.
 */
typedef struct __attribute__((packed)) {
    uint8_t  round_id;           /**< Closed round identifier.                   */
    uint8_t  shards_aggregated;  /**< Number of shard updates aggregated.        */
    uint32_t next_round_in_sec;  /**< Seconds until next beacon (sleep hint).    */
} FLRoundClosePayload;

/**
 * @brief Payload for ::FL_FRAME_HANDSHAKE and ::FL_FRAME_HANDSHAKE_ACK.
 */
typedef struct __attribute__((packed)) {
    uint8_t public_key[32]; /**< Curve25519 ephemeral public key. */
} FLHandshakePayload;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * @brief Encode a frame ready for transmission.
 *
 * Fills all header fields, copies @p payload_len bytes from @p payload into
 * the frame payload, and computes the CRC-16/CCITT-FALSE over header + payload.
 *
 * @param[out] frame       Destination frame buffer. Must not be NULL.
 * @param[in]  type        Frame type (::FLFrameType).
 * @param[in]  shard_id    Sender shard ID (use ::FL_SHARD_ID_HERALD for Herald).
 * @param[in]  round_id    Current FL round identifier.
 * @param[in]  frag_index  Fragment index (0-based).
 * @param[in]  frag_total  Total number of fragments for this update.
 * @param[in]  payload     Payload bytes. May be NULL if payload_len is 0.
 * @param[in]  payload_len Number of payload bytes. Must be <= ::FL_PAYLOAD_MAX.
 *
 * @retval FL_OK                 Success.
 * @retval FL_ERR_INVALID_ARG   @p frame is NULL.
 * @retval FL_ERR_BUFFER_TOO_SMALL @p payload_len > ::FL_PAYLOAD_MAX.
 */
FLError fl_frame_encode(FLFrame       *frame,
                        FLFrameType    type,
                        uint16_t       shard_id,
                        uint8_t        round_id,
                        uint8_t        frag_index,
                        uint8_t        frag_total,
                        const uint8_t *payload,
                        uint8_t        payload_len);

/**
 * @brief Decode and validate raw bytes into a ::FLFrame.
 *
 * Checks magic bytes and CRC. If both pass, copies the frame into @p frame.
 *
 * @param[in]  raw      Received bytes. Must not be NULL.
 * @param[in]  raw_len  Number of bytes in @p raw.
 * @param[out] frame    Destination frame. Must not be NULL.
 *
 * @retval FL_OK              Success; @p frame is populated.
 * @retval FL_ERR_INVALID_ARG @p raw or @p frame is NULL.
 * @retval FL_ERR_BAD_MAGIC   Magic bytes do not equal ::FL_MAGIC.
 * @retval FL_ERR_CRC         CRC mismatch; frame is corrupt.
 * @retval FL_ERR_CAPACITY    @p raw_len < ::FL_HEADER_SIZE.
 */
FLError fl_frame_decode(const uint8_t *raw,
                        size_t         raw_len,
                        FLFrame       *frame);

/**
 * @brief Return a human-readable name for a frame type.
 *
 * Always returns a valid non-NULL pointer to a static string.
 * Suitable for logging. Returns @c "UNKNOWN" for unrecognised values.
 *
 * @param[in] type  Frame type to describe.
 * @return          Pointer to a static string; do not free.
 */
const char *fl_frame_type_str(FLFrameType type);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_FRAME_H */
