/*
 * filum/examples/herald_linux/shard_sim.c
 *
 * Software shard simulator for integration testing.
 * Runs a Shard in the same process as the Herald via loopback pipes.
 * No hardware required.
 *
 * Usage: ./filum_shard_sim
 */

#include "fl_shard.h"
#include "fl_model.h"
#include "fl_train.h"
#include "fl_herald.h"
#include "hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

/* Loopback pipe pair */
static int shard_to_herald[2];  /* shard writes [1], herald reads [0] */
static int herald_to_shard[2];  /* herald writes [1], shard reads [0] */

/* --- Shard HAL backed by pipes --- */
extern const FLHal g_host_hal;
extern void hal_host_set_fds(int tx_fd, int rx_fd);

/* --- Simulated sensor data --- */
#define SIM_INPUT   8
#define SIM_OUTPUT  2
#define SIM_SAMPLES 20

static uint16_t g_sim_idx = 0;

int fl_user_data_cb(void *ctx, FLSample *s)
{
    (void)ctx;
    if (g_sim_idx >= SIM_SAMPLES) { g_sim_idx = 0; return 0; }
    for (uint8_t i = 0; i < SIM_INPUT;  i++) s->input[i] = (float)(g_sim_idx % 10) / 10.0f;
    s->label[0] = (g_sim_idx % 2 == 0) ? 1.0f : 0.0f;
    s->label[1] = 1.0f - s->label[0];
    s->input_len = SIM_INPUT;
    s->label_len = SIM_OUTPUT;
    g_sim_idx++;
    return 1;
}

/* --- Shard thread --- */
static void *shard_thread(void *arg)
{
    (void)arg;
    FLModel g_model;
    FLShard g_shard;

    static const FLLayerDesc layers[] = {
        { .type=FL_LAYER_LINEAR, .activation=FL_ACT_RELU,
          .in_features=SIM_INPUT, .out_features=16,
          .param_count=SIM_INPUT * 16 + 16 },
        { .type=FL_LAYER_LINEAR, .activation=FL_ACT_SIGMOID,
          .in_features=16, .out_features=SIM_OUTPUT,
          .param_count=16 * SIM_OUTPUT + SIM_OUTPUT },
    };

    hal_host_set_fds(shard_to_herald[1], herald_to_shard[0]);

    fl_model_init(&g_model, layers, 2);
    fl_model_init_random(&g_model, 0xABCD);
    fl_shard_init(&g_shard, &g_host_hal, 0x0001, &g_model);

    printf("[ShardSim] Shard thread started\n");
    for (int i = 0; i < 1000; i++) {
        fl_shard_tick(&g_shard);
        usleep(50000);  /* 50ms tick */
    }
    printf("[ShardSim] Shard thread done. Rounds: %u\n",
           g_shard.rounds_completed);
    return NULL;
}

/* --- Herald thread --- */
static volatile int g_herald_stop = 0;
static FLHerald     g_herald;

static void *herald_thread(void *arg)
{
    (void)arg;

    float global_model[192] = {0};

    FLHeraldConfig cfg = {
        .serial_port       = NULL,    /* overridden below */
        .baud_rate         = 0,
        .model_param_count = 192,
        .global_model      = global_model,
        .round_policy = {
            .window_seconds      = 10,
            .inter_round_delay_s = 2,
            .min_shards          = 1,
            .max_shards          = 1,
            .local_epochs        = 2,
            .learning_rate       = 0.01f,
        },
        .aggregator        = FL_AGG_FEDAVG,
        .on_round_complete = NULL,
        .log_level         = 2,
    };

    /* Directly inject pipe FDs into herald (bypass serial_open) */
    memset(&g_herald, 0, sizeof(g_herald));
    g_herald.config    = cfg;
    g_herald.running   = 1;
    g_herald.serial_fd = shard_to_herald[0];   /* read shard output */

    fl_round_init(&g_herald.scheduler, &cfg.round_policy);
    fl_pool_init(&g_herald.frag_pool, 30);
    fl_agg_init(&g_herald.aggregator, cfg.aggregator,
                global_model, cfg.model_param_count);

    /* Send initial beacon via herald->shard pipe */
    g_herald.serial_fd = herald_to_shard[1];
    uint8_t round_id = fl_round_open(&g_herald.scheduler);
    fl_herald_send_beacon(&g_herald, round_id);
    g_herald.serial_fd = shard_to_herald[0];

    printf("[ShardSim] Herald running (read fd=%d)\n", g_herald.serial_fd);
    fl_herald_run(&g_herald);
    return NULL;
}

int main(void)
{
    pipe(shard_to_herald);
    pipe(herald_to_shard);

    pthread_t shard_t, herald_t;
    pthread_create(&herald_t, NULL, herald_thread, NULL);
    usleep(100000);  /* let herald open round first */
    pthread_create(&shard_t, NULL, shard_thread, NULL);

    pthread_join(shard_t, NULL);
    fl_herald_stop(&g_herald);
    pthread_join(herald_t, NULL);

    printf("[ShardSim] Simulation complete.\n");
    return 0;
}
