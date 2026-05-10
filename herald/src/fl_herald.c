/*
 * filum/herald/src/fl_herald.c
 *
 * Herald coordinator - event loop and round management.
 */

#include "fl_herald.h"
#include "fl_error.h"
#include "fl_error.h"
#include "fl_sparse.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Logging
 * ------------------------------------------------------------------------- */

#define LOG(h, level, fmt, ...) do { \
    if ((h)->config.log_level >= (level)) \
        fprintf(stderr, "[Herald] " fmt "\n", ##__VA_ARGS__); \
} while (0)

/* -------------------------------------------------------------------------
 * Serial transport (to LoRa gateway via UART)
 * ------------------------------------------------------------------------- */

static int serial_open(const char *port, uint32_t baud)
{
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) { close(fd); return -1; }

    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 115200: speed = B115200; break;
        default:     speed = B115200;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    cfmakeraw(&tty);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return -1; }
    return fd;
}

static int serial_write(int fd, const uint8_t *buf, size_t len)
{
    ssize_t n = write(fd, buf, len);
    return (n == (ssize_t)len) ? 0 : -1;
}

static int serial_read(int fd, uint8_t *buf, size_t max_len, size_t *read_len)
{
    ssize_t n = read(fd, buf, max_len);
    if (n < 0) {
        if (errno == EAGAIN) { *read_len = 0; return 0; }
        return -1;
    }
    *read_len = (size_t)n;
    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/* Internal init shared by fl_herald_init and fl_herald_init_fd */
static FLError herald_init_common(FLHerald *herald, const FLHeraldConfig *config)
{
    memset(herald, 0, sizeof(FLHerald));
    herald->config  = *config;
    herald->running = 1;

    fl_round_init(&herald->scheduler, &config->round_policy);
    fl_pool_init(&herald->frag_pool, 300);
    fl_agg_init(&herald->aggregator, config->aggregator,
                config->global_model, config->model_param_count);
    return FL_OK;
}

FLError fl_herald_init(FLHerald *herald, const FLHeraldConfig *config)
{
    if (!herald || !config) return FL_ERR_INVALID_ARG;
    herald_init_common(herald, config);

    herald->serial_fd = serial_open(config->serial_port, config->baud_rate);
    if (herald->serial_fd < 0) {
        LOG(herald, 1, "Failed to open serial port %s", config->serial_port);
        return FL_ERR_TRANSPORT;
    }

    LOG(herald, 2, "Initialized. Serial: %s @ %u baud",
        config->serial_port, config->baud_rate);
    return FL_OK;
}

/*
 * fl_herald_init_fd - initialize Herald with a pre-opened file descriptor.
 * Used for testing (pipes) and for custom transports (SPI, CAN, etc.).
 * fd: readable/writable fd (pipe, socket, pty, etc.)
 */
FLError fl_herald_init_fd(FLHerald *herald, const FLHeraldConfig *config, int fd)
{
    if (!herald || !config) return FL_ERR_INVALID_ARG;
    herald_init_common(herald, config);
    herald->serial_fd = fd;
    LOG(herald, 2, "Initialized with fd=%d", fd);
    return FL_OK;
}

FLError fl_herald_send_beacon(FLHerald *herald, uint8_t round_id)
{
    FLBeaconPayload bp = {
        .round_id         = round_id,
        .window_seconds   = herald->config.round_policy.window_seconds,
        .model_param_count= herald->config.model_param_count,
        .min_shards       = herald->config.round_policy.min_shards,
        .local_epochs     = herald->config.round_policy.local_epochs,
        .learning_rate    = herald->config.round_policy.learning_rate,
    };

    FLFrame frame;
    fl_frame_encode(&frame, FL_FRAME_BEACON, FL_SHARD_ID_HERALD,
                    round_id, 0, 1, (const uint8_t *)&bp, sizeof(bp));

    uint8_t wire[FL_FRAME_MAX_SIZE];
    memcpy(wire, &frame, FL_HEADER_SIZE + sizeof(bp));

    LOG(herald, 2, "Sending BEACON round=%d", round_id);
    return serial_write(herald->serial_fd, wire, FL_HEADER_SIZE + sizeof(bp)) == 0
           ? FL_OK : FL_ERR_TRANSPORT;
}

FLError fl_herald_send_delta(FLHerald *herald, const float *delta, uint16_t topk)
{
    FLSparseBuffer sbuf;
    memset(&sbuf, 0, sizeof(sbuf));
    fl_sparse_encode(delta, herald->config.model_param_count,
                     topk, NULL, &sbuf);

    uint8_t  frag_total = fl_sparse_fragment_count(&sbuf);
    int      errors     = 0;

    for (uint8_t f = 0; f < frag_total; f++) {
        uint8_t payload[FL_PAYLOAD_MAX];
        uint8_t payload_len = 0;
        fl_sparse_write_fragment(payload, &payload_len, &sbuf, f);

        FLFrame frame;
        fl_frame_encode(&frame, FL_FRAME_DELTA, FL_SHARD_ID_HERALD,
                        herald->current_round, f, frag_total,
                        payload, payload_len);

        if (serial_write(herald->serial_fd, (uint8_t *)&frame,
                         FL_HEADER_SIZE + payload_len) != 0)
            errors++;

        /* Inter-packet gap to respect gateway duty cycle */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L }; /* 100ms */
        nanosleep(&ts, NULL);
    }

    LOG(herald, 2, "Sent DELTA: %d frags, %d entries, %d errors",
        frag_total, sbuf.count, errors);
    return errors ? FL_ERR_TRANSPORT : FL_OK;
}

FLError fl_herald_run(FLHerald *herald)
{
    static uint8_t rx_buf[FL_FRAME_MAX_SIZE];
    static float   dense_update[FL_MODEL_MAX_PARAMS];
    static float   agg_delta[FL_MODEL_MAX_PARAMS];

    LOG(herald, 2, "Event loop started");
    uint8_t round_id = fl_round_open(&herald->scheduler);
    fl_herald_send_beacon(herald, round_id);

    while (herald->running) {

        /* --- RX path ---------------------------------------------------- */
        size_t rx_len = 0;
        if (serial_read(herald->serial_fd, rx_buf, sizeof(rx_buf), &rx_len) == 0
            && rx_len > 0) {

            FLFrame frame;
            if (fl_frame_decode(rx_buf, rx_len, &frame) == 0 &&
                frame.frame_type == FL_FRAME_UPDATE) {

                uint8_t payload_len = (uint8_t)(rx_len - FL_HEADER_SIZE);
                int complete = fl_pool_ingest(&herald->frag_pool, &frame, payload_len);

                if (complete == 1) {
                    uint16_t shard_id = frame.shard_id;
                    LOG(herald, 3, "Shard %04X update complete", shard_id);

                    /* Send ACK */
                    FLFrame ack;
                    fl_frame_encode(&ack, FL_FRAME_ACK, FL_SHARD_ID_HERALD,
                                    herald->current_round, 0, 1, NULL, 0);
                    serial_write(herald->serial_fd, (uint8_t *)&ack, FL_HEADER_SIZE);

                    /* Decode and accumulate */
                    const FLSparseBuffer *sbuf =
                        fl_pool_get_complete(&herald->frag_pool, shard_id);
                    if (sbuf) {
                        fl_sparse_decode(sbuf, dense_update,
                                         herald->config.model_param_count, 0);
                        fl_agg_add(&herald->aggregator, dense_update,
                                   herald->config.model_param_count, 100);
                        fl_pool_release(&herald->frag_pool, shard_id);
                        fl_round_shard_complete(&herald->scheduler, shard_id);
                    }

                    if (herald->config.on_shard_update)
                        herald->config.on_shard_update(shard_id, round_id);
                }
            }
        }

        /* --- Round management ------------------------------------------- */
        fl_pool_expire(&herald->frag_pool);

        if (fl_round_should_close(&herald->scheduler)) {
            LOG(herald, 2, "Round %d closing. Shards: %d",
                herald->scheduler.round_id,
                herald->scheduler.shards_complete);

            fl_round_close(&herald->scheduler);

            if (herald->scheduler.shards_complete > 0) {
                /* Aggregate */
                fl_agg_finalize(&herald->aggregator, agg_delta);

                /* Apply to global model */
                float *gm = herald->config.global_model;
                for (uint16_t i = 0; i < herald->config.model_param_count; i++)
                    gm[i] += agg_delta[i];

                /* Send delta to shards */
                uint16_t topk = herald->config.model_param_count / 10;
                fl_herald_send_delta(herald, agg_delta, topk ? topk : 1);
            }

            /* ROUND_CLOSE broadcast */
            FLRoundClosePayload rcp = {
                .round_id          = round_id,
                .shards_aggregated = herald->scheduler.shards_complete,
                .next_round_in_sec = herald->config.round_policy.inter_round_delay_s,
            };
            FLFrame close_frame;
            fl_frame_encode(&close_frame, FL_FRAME_ROUND_CLOSE,
                            FL_SHARD_ID_HERALD, round_id, 0, 1,
                            (const uint8_t *)&rcp, sizeof(rcp));
            serial_write(herald->serial_fd, (uint8_t *)&close_frame,
                         FL_HEADER_SIZE + sizeof(rcp));

            if (herald->config.on_round_complete)
                herald->config.on_round_complete(round_id,
                    herald->scheduler.shards_complete);

            fl_agg_reset(&herald->aggregator);

            /* Wait inter-round delay then open next round */
            sleep(herald->config.round_policy.inter_round_delay_s);
            round_id = fl_round_open(&herald->scheduler);
            herald->current_round = round_id;
            fl_herald_send_beacon(herald, round_id);
        }

        /* Yield */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000L }; /* 10ms */
        nanosleep(&ts, NULL);
    }

    LOG(herald, 2, "Event loop stopped");
    return FL_OK;
}

void fl_herald_stop(FLHerald *herald)
{
    herald->running = 0;
}

void fl_herald_destroy(FLHerald *herald)
{
    if (herald->serial_fd >= 0)
        close(herald->serial_fd);
}
