/*
 * filum/common/src/fl_frame.c
 */

#include "fl_frame.h"
#include <string.h>
#include <stdint.h>

uint16_t fl_crc16(const uint8_t *data, size_t len);

static uint16_t frame_crc(const FLFrame *f, uint8_t payload_len)
{
    uint16_t crc = 0xFFFF;
    const uint8_t *raw = (const uint8_t *)f;
    for (int i = 0; i < 8; i++) {
        crc ^= (uint16_t)raw[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000)
                ? (uint16_t)((crc << 1) ^ 0x1021)
                : (uint16_t)(crc << 1);
    }
    for (int i = 0; i < payload_len; i++) {
        crc ^= (uint16_t)f->payload[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000)
                ? (uint16_t)((crc << 1) ^ 0x1021)
                : (uint16_t)(crc << 1);
    }
    return crc;
}

FLError fl_frame_encode(FLFrame       *frame,
                        FLFrameType    type,
                        uint16_t       shard_id,
                        uint8_t        round_id,
                        uint8_t        frag_index,
                        uint8_t        frag_total,
                        const uint8_t *payload,
                        uint8_t        payload_len)
{
    if (!frame)                       return FL_ERR_INVALID_ARG;
    if (payload_len > FL_PAYLOAD_MAX) return FL_ERR_BUFFER_TOO_SMALL;

    frame->magic      = FL_MAGIC;
    frame->frame_type = (uint8_t)type;
    frame->shard_id   = shard_id;
    frame->round_id   = round_id;
    frame->frag_index = frag_index;
    frame->frag_total = frag_total;

    memset(frame->payload, 0, FL_PAYLOAD_MAX);
    if (payload && payload_len > 0)
        memcpy(frame->payload, payload, payload_len);

    frame->crc16 = frame_crc(frame, payload_len);
    return FL_OK;
}

FLError fl_frame_decode(const uint8_t *raw, size_t raw_len, FLFrame *frame)
{
    if (!raw || !frame)          return FL_ERR_INVALID_ARG;
    if (raw_len < FL_HEADER_SIZE) return FL_ERR_CAPACITY;

    memcpy(frame, raw,
           raw_len < FL_FRAME_MAX_SIZE ? raw_len : FL_FRAME_MAX_SIZE);

    if (frame->magic != FL_MAGIC) return FL_ERR_BAD_MAGIC;

    uint8_t  payload_len = (uint8_t)(raw_len - FL_HEADER_SIZE);
    uint16_t expected    = frame_crc(frame, payload_len);
    if (frame->crc16 != expected) return FL_ERR_CRC;

    return FL_OK;
}

const char *fl_frame_type_str(FLFrameType type)
{
    switch (type) {
        case FL_FRAME_BEACON:        return "BEACON";
        case FL_FRAME_DELTA:         return "DELTA";
        case FL_FRAME_UPDATE:        return "UPDATE";
        case FL_FRAME_ACK:           return "ACK";
        case FL_FRAME_ROUND_CLOSE:   return "ROUND_CLOSE";
        case FL_FRAME_HANDSHAKE:     return "HANDSHAKE";
        case FL_FRAME_HANDSHAKE_ACK: return "HANDSHAKE_ACK";
        default:                     return "UNKNOWN";
    }
}
