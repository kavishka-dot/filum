/*
 * filum/examples/herald_linux/main.c
 *
 * Filum Herald daemon for Linux (Raspberry Pi / BeagleBone / x86).
 *
 * Usage:
 *   ./filum_herald --port /dev/ttyUSB0 --baud 115200 \
 *                  --params 192 --rounds 100 --window 7200
 */

#include "fl_herald.h"
#include "fl_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static FLHerald g_herald;

static void on_sigint(int sig)
{
    (void)sig;
    fprintf(stderr, "\n[Herald] Caught SIGINT, stopping...\n");
    fl_herald_stop(&g_herald);
}

static void on_round_complete(uint8_t round_id, uint8_t shards)
{
    printf("[Herald] Round %d complete. Shards aggregated: %d\n",
           round_id, shards);
}

static void on_shard_update(uint16_t shard_id, uint8_t round_id)
{
    printf("[Herald] Shard 0x%04X submitted update for round %d\n",
           shard_id, round_id);
}

int main(int argc, char **argv)
{
    const char *port    = "/dev/ttyUSB0";
    uint32_t    baud    = 115200;
    uint16_t    params  = 192;     /* 8*16+16 + 16*2+2 = 192 */
    uint32_t    window  = 7200;    /* 2 hour collection window */
    uint32_t    delay   = 300;     /* 5 min inter-round delay  */
    uint8_t     min_sh  = 2;
    uint8_t     max_sh  = 20;

    /* Minimal arg parsing */
    for (int i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--port"))   port   = argv[i+1];
        if (!strcmp(argv[i], "--baud"))   baud   = (uint32_t)atoi(argv[i+1]);
        if (!strcmp(argv[i], "--params")) params = (uint16_t)atoi(argv[i+1]);
        if (!strcmp(argv[i], "--window")) window = (uint32_t)atoi(argv[i+1]);
        if (!strcmp(argv[i], "--delay"))  delay  = (uint32_t)atoi(argv[i+1]);
        if (!strcmp(argv[i], "--min"))    min_sh = (uint8_t)atoi(argv[i+1]);
        if (!strcmp(argv[i], "--max"))    max_sh = (uint8_t)atoi(argv[i+1]);
    }

    /* Global model (zero-initialized; load from file in production) */
    float *global_model = (float *)calloc(params, sizeof(float));
    if (!global_model) { perror("calloc"); return 1; }

    FLHeraldConfig cfg = {
        .serial_port       = port,
        .baud_rate         = baud,
        .model_param_count = params,
        .global_model      = global_model,
        .round_policy = {
            .window_seconds      = window,
            .inter_round_delay_s = delay,
            .min_shards          = min_sh,
            .max_shards          = max_sh,
            .local_epochs        = 3,
            .learning_rate       = 0.01f,
        },
        .aggregator        = FL_AGG_FEDAVG,
        .on_round_complete = on_round_complete,
        .on_shard_update   = on_shard_update,
        .on_error          = NULL,
        .log_level         = 3,
    };

    signal(SIGINT, on_sigint);

    printf("[Herald] Starting. Port=%s params=%d window=%ds\n",
           port, params, window);

    if (fl_herald_init(&g_herald, &cfg) != 0) {
        fprintf(stderr, "[Herald] Init failed\n");
        free(global_model);
        return 1;
    }

    fl_herald_run(&g_herald);
    fl_herald_destroy(&g_herald);
    free(global_model);
    return 0;
}
