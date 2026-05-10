/*
 * filum/shard/src/fl_shard.c
 *
 * Shard state machine implementation.
 */

#include "fl_shard.h"
#include "fl_train.h"
#include "fl_error.h"
#include <string.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void shard_log(const FLShard *s, const char *msg)
{
    if (s->hal->log)
        s->hal->log("[Shard %04X] %s\n", s->shard_id, msg);
}

static int shard_tx_frame(FLShard *shard, FLFrameType type,
                          const uint8_t *payload, uint8_t payload_len,
                          uint8_t frag_index, uint8_t frag_total)
{
    FLFrame f;
    if (fl_frame_encode(&f, type, shard->shard_id, shard->current_round,
                        frag_index, frag_total, payload, payload_len) != 0)
        return FL_HAL_ERROR;

    /* Encrypt UPDATE frames if crypto is active */
    uint8_t tag[FL_CRYPTO_TAG_SIZE] = {0};
    if (shard->crypto.encrypt_enabled && shard->crypto.handshake_done &&
        type == FL_FRAME_UPDATE && payload_len > 0) {
        uint8_t nonce[FL_CRYPTO_NONCE_SIZE];
        fl_crypto_make_nonce(nonce, shard->current_round,
                             frag_index, shard->shard_id);
        fl_crypto_encrypt(&shard->crypto, f.payload, payload_len, nonce, tag);
        /* Append tag after payload (Herald strips it before decode) */
        if ((uint16_t)payload_len + FL_CRYPTO_TAG_SIZE <= FL_PAYLOAD_MAX)
            memcpy(f.payload + payload_len, tag, FL_CRYPTO_TAG_SIZE);
    }

    uint8_t wire_len = (uint8_t)(FL_HEADER_SIZE + payload_len);
    return shard->hal->lora_send((const uint8_t *)&f, wire_len);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

FLError fl_shard_init(FLShard *shard, const FLHal *hal,
                  uint16_t shard_id, FLModel *model)
{
    if (!shard || !hal || !model) return FL_ERR_INVALID_ARG;
    memset(shard, 0, sizeof(FLShard));
    shard->hal      = hal;
    shard->shard_id = shard_id;
    shard->model    = model;
    shard->state    = FL_SHARD_STATE_IDLE;

    /* Try to load persisted model weights */
    if (hal->nvs_read) {
        uint8_t buf[FL_MODEL_MAX_PARAMS * sizeof(float)];
        if (hal->nvs_read(0, buf, model->total_params * sizeof(float)) == 0)
            fl_model_load(model, buf, model->total_params * sizeof(float));
    }

    /* DP disabled by default — call fl_shard_enable_dp() to activate */
    shard->dp.dp_enabled = 0;

    /* Crypto disabled by default — call fl_shard_enable_crypto() to activate */
    shard->crypto.encrypt_enabled = 0;
    shard->crypto.handshake_done  = 0;

    shard_log(shard, "init OK");
    return FL_OK;
}

void fl_shard_on_rx(FLShard *shard, const uint8_t *raw, size_t raw_len)
{
    FLFrame frame;
    if (fl_frame_decode(raw, raw_len, &frame) != 0) {
        shard->rx_errors++;
        return;
    }

    switch ((FLFrameType)frame.frame_type) {

    case FL_FRAME_BEACON: {
        FLBeaconPayload bp;
        memcpy(&bp, frame.payload, sizeof(bp));
        shard->beacon        = bp;
        shard->current_round = bp.round_id;
        shard->state         = FL_SHARD_STATE_TRAINING;
        shard_log(shard, "beacon received -> TRAINING");
        break;
    }

    case FL_FRAME_DELTA: {
        if (shard->state == FL_SHARD_STATE_RECV_DELTA) {
            if (frame.frag_index == 0) {
                /* First fragment: reset buffer */
                memset(&shard->delta_buf, 0, sizeof(shard->delta_buf));
                shard->rx_frag_total = frame.frag_total;
            }
            fl_sparse_read_fragment(frame.payload,
                                    (uint8_t)(raw_len - FL_HEADER_SIZE),
                                    &shard->delta_buf);
            shard->rx_frag_recv++;

            if (shard->rx_frag_recv >= shard->rx_frag_total) {
                /* All delta fragments received - apply to model */
                float delta_dense[FL_MODEL_MAX_PARAMS];
                fl_sparse_decode(&shard->delta_buf, delta_dense,
                                 shard->model->total_params, 0);
                fl_model_apply_delta(shard->model, delta_dense,
                                     shard->model->total_params);
                shard_log(shard, "delta applied -> IDLE");
                shard->state = FL_SHARD_STATE_IDLE;

                /* Persist updated weights */
                if (shard->hal->nvs_write) {
                    uint8_t buf[FL_MODEL_MAX_PARAMS * sizeof(float)];
                    fl_model_save(shard->model, buf,
                                  shard->model->total_params * sizeof(float));
                    shard->hal->nvs_write(
                        0, buf, shard->model->total_params * sizeof(float));
                }
            }
        }
        break;
    }

    case FL_FRAME_ACK:
        if (shard->state == FL_SHARD_STATE_AWAIT_ACK) {
            shard->rounds_completed++;
            shard->state = FL_SHARD_STATE_RECV_DELTA;
            shard->rx_frag_recv  = 0;
            shard->rx_frag_total = 0;
            memset(&shard->delta_buf, 0, sizeof(shard->delta_buf));
            shard_log(shard, "ACK received -> RECV_DELTA");
        }
        break;

    case FL_FRAME_ROUND_CLOSE: {
        FLRoundClosePayload rcp;
        memcpy(&rcp, frame.payload, sizeof(rcp));
        shard->state = FL_SHARD_STATE_IDLE;
        shard_log(shard, "round closed -> IDLE");
        break;
    }

    case FL_FRAME_HANDSHAKE_ACK: {
        /* Herald replied with its public key — complete ECDH */
        if (shard->crypto.encrypt_enabled && !shard->crypto.handshake_done) {
            FLHandshakePayload hp;
            memcpy(&hp, frame.payload, sizeof(hp));
            fl_crypto_handshake(&shard->crypto, hp.public_key);
            shard_log(shard, "ECDH handshake complete");
        }
        break;
    }

    default:
        break;
    }
}

FLShardState fl_shard_tick(FLShard *shard)
{
    switch (shard->state) {

    case FL_SHARD_STATE_IDLE: {
        /* Send ECDH handshake if crypto enabled and not yet done */
        if (shard->crypto.encrypt_enabled && !shard->crypto.handshake_done) {
            FLHandshakePayload hp;
            memcpy(hp.public_key, shard->crypto.public_key, 32);
            shard_tx_frame(shard, FL_FRAME_HANDSHAKE,
                           (uint8_t*)&hp, (uint8_t)sizeof(hp), 0, 1);
            shard_log(shard, "sent HANDSHAKE");
        }
        /* Listen for beacon (or handshake ACK) */
        uint8_t rx_len = 0;
        int ret = shard->hal->lora_recv(shard->rx_buf, &rx_len, 500);
        if (ret == FL_HAL_OK && rx_len > 0)
            fl_shard_on_rx(shard, shard->rx_buf, rx_len);
        break;
    }

    case FL_SHARD_STATE_TRAINING: {
        
        static float params_before[FL_MODEL_MAX_PARAMS];

        extern int fl_user_data_cb(void *ctx, FLSample *sample);

        /* Snapshot parameters before local training */
        memcpy(params_before, shard->model->params,
               shard->model->total_params * sizeof(float));

        FLTrainConfig cfg = {
            .epochs         = shard->beacon.local_epochs,
            .learning_rate  = shard->beacon.learning_rate,
            .grad_clip_norm = 1.0f,
            .loss_type      = FL_LOSS_XENTROPY,
            .topk_ratio     = FL_SHARD_TOPK_RATIO,
        };

        fl_train_run(shard->model, &cfg, fl_user_data_cb, NULL);

        /* Compute parameter delta = params_after - params_before */
        /* Store in model->grad[] temporarily for the ENCODING step */
        for (uint16_t i = 0; i < shard->model->total_params; i++)
            shard->model->grad[i] = shard->model->params[i] - params_before[i];

        shard->state = FL_SHARD_STATE_ENCODING;
        shard_log(shard, "training done -> ENCODING");
        break;
    }

    case FL_SHARD_STATE_ENCODING: {
        /* Apply differential privacy noise BEFORE sparse encoding */
        if (shard->dp.dp_enabled) {
            fl_dp_apply(&shard->dp, shard->model->grad,
                        shard->model->total_params,
                        shard->shard_id, shard->current_round);
        }

        /* Sparsify gradients */
        uint16_t topk = shard->model->total_params / FL_SHARD_TOPK_RATIO;
        if (topk < 1) topk = 1;

        fl_sparse_encode(shard->model->grad,
                         shard->model->total_params,
                         topk,
                         shard->residual,
                         &shard->update_buf);

        shard->tx_frag_sent  = 0;
        shard->tx_frag_total = fl_sparse_fragment_count(&shard->update_buf);
        shard->state         = FL_SHARD_STATE_TX;
        shard_log(shard, "encoding done -> TX");
        break;
    }

    case FL_SHARD_STATE_TX: {
        /* If nothing to send (e.g. zero gradients), go straight to AWAIT_ACK */
        if (shard->tx_frag_total == 0) {
            shard->state = FL_SHARD_STATE_AWAIT_ACK;
            shard_log(shard, "no updates to send -> AWAIT_ACK");
            break;
        }
        /* Send one fragment per tick (duty cycle managed externally) */
        if (shard->tx_frag_sent < shard->tx_frag_total) {
            uint8_t payload[FL_PAYLOAD_MAX];
            uint8_t payload_len = 0;

            fl_sparse_write_fragment(payload, &payload_len,
                                     &shard->update_buf,
                                     shard->tx_frag_sent);

            int ret = shard_tx_frame(shard, FL_FRAME_UPDATE,
                                     payload, payload_len,
                                     shard->tx_frag_sent,
                                     shard->tx_frag_total);
            if (ret == FL_HAL_OK) {
                shard->tx_frag_sent++;
            } else {
                shard->tx_errors++;
            }

            if (shard->tx_frag_sent >= shard->tx_frag_total) {
                shard->state = FL_SHARD_STATE_AWAIT_ACK;
                shard_log(shard, "TX complete -> AWAIT_ACK");
            }
        }
        break;
    }

    case FL_SHARD_STATE_AWAIT_ACK: {
        uint8_t rx_len = 0;
        int ret = shard->hal->lora_recv(shard->rx_buf, &rx_len,
                                        FL_SHARD_RECV_TIMEOUT_MS);
        if (ret == FL_HAL_OK && rx_len > 0)
            fl_shard_on_rx(shard, shard->rx_buf, rx_len);
        break;
    }

    case FL_SHARD_STATE_RECV_DELTA: {
        uint8_t rx_len = 0;
        int ret = shard->hal->lora_recv(shard->rx_buf, &rx_len,
                                        FL_SHARD_RECV_TIMEOUT_MS);
        if (ret == FL_HAL_OK && rx_len > 0)
            fl_shard_on_rx(shard, shard->rx_buf, rx_len);
        break;
    }

    case FL_SHARD_STATE_ERROR:
        shard_log(shard, "ERROR state - needs reset");
        break;
    }

    return shard->state;
}

void fl_shard_sleep(FLShard *shard)
{
    if (shard->state == FL_SHARD_STATE_IDLE) {
        /* Sleep until next expected beacon window */
        uint32_t sleep_s = shard->beacon.window_seconds > 60
                           ? shard->beacon.window_seconds - 30
                           : 10;
        if (shard->hal->deep_sleep_rtc)
            shard->hal->deep_sleep_rtc(sleep_s);
    } else {
        /* Short sleep between ticks */
        shard->hal->sleep_ms(100);
    }
}

const char *fl_shard_state_str(FLShardState state)
{
    switch (state) {
        case FL_SHARD_STATE_IDLE:        return "IDLE";
        case FL_SHARD_STATE_TRAINING:    return "TRAINING";
        case FL_SHARD_STATE_ENCODING:    return "ENCODING";
        case FL_SHARD_STATE_TX:          return "TX";
        case FL_SHARD_STATE_AWAIT_ACK:   return "AWAIT_ACK";
        case FL_SHARD_STATE_RECV_DELTA:  return "RECV_DELTA";
        case FL_SHARD_STATE_ERROR:       return "ERROR";
        default:                         return "UNKNOWN";
    }
}

/* -------------------------------------------------------------------------
 * Security configuration
 * ------------------------------------------------------------------------- */

FLError fl_shard_enable_dp(FLShard *shard,
                        float epsilon, float delta, float sensitivity)
{
    if (!shard || epsilon <= 0.0f || delta <= 0.0f || sensitivity <= 0.0f)
        return FL_ERR_INVALID_ARG;
    shard->dp = fl_dp_config(epsilon, delta, sensitivity);
    if (shard->hal->log)
        shard->hal->log("[Shard %04X] DP enabled: eps=%.2f delta=%.2e sigma=%.4f\n",
                        shard->shard_id, epsilon, (double)delta, shard->dp.sigma);
    return FL_OK;
}

FLError fl_shard_enable_crypto(FLShard *shard, const uint8_t *seed)
{
    if (!shard) return FL_ERR_INVALID_ARG;
    fl_crypto_init(&shard->crypto, seed);
    shard->crypto.encrypt_enabled = 1;
    shard_log(shard, "crypto initialized, awaiting handshake");
    return FL_OK;
}

void fl_shard_privacy_report(const FLShard *shard)
{
    float eps_total, delta_total;
    fl_dp_privacy_spent(&shard->dp, &eps_total, &delta_total);
    if (shard->hal->log)
        shard->hal->log("[Shard %04X] Privacy spent: eps=%.2f delta=%.2e (%u rounds)\n",
                        shard->shard_id, eps_total, (double)delta_total,
                        shard->dp.rounds_spent);
}
