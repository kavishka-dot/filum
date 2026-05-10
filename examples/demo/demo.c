/*
 * filum/examples/demo/demo.c
 *
 * End-to-end performance demo: 1 Herald + 1 Shard, synthetic data.
 * Single-threaded. In-memory loopback replaces LoRa radio.
 *
 * Build:  cmake -B build -DFILUM_TARGET=host && cmake --build build
 * Run:    ./build/filum_demo
 */

#include "fl_model.h"
#include "fl_train.h"
#include "fl_shard.h"
#include "fl_herald.h"
#include "fl_sparse.h"
#include "fl_frame.h"
#include "hal.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* =========================================================================
 * Config
 * ========================================================================= */
#define FEAT          4
#define CLASSES       2
#define HIDDEN        8
#define N_TRAIN       80
#define N_TEST        40
#define N_ROUNDS      6
#define LOCAL_EPOCHS  3
#define LR            0.05f
#define TOPK_RATIO    4       /* send top 25% of param deltas */

#define N_PARAMS  (FEAT*HIDDEN + HIDDEN + HIDDEN*CLASSES + CLASSES)

/* =========================================================================
 * Synthetic dataset
 * Class 0: N(0.2, 0.08)   Class 1: N(0.8, 0.08)
 * ========================================================================= */
static uint32_t g_rng = 42;

static float ru(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / (float)(1 << 24);
}
static float rn(float m, float s) {
    float u1 = ru() + 1e-7f, u2 = ru();
    return m + s * sqrtf(-2.f * logf(u1)) * cosf(6.2831853f * u2);
}

typedef struct { float x[FEAT]; int label; } Sample;
static Sample g_train[N_TRAIN], g_test[N_TEST];

static void make_data(void) {
    for (int i = 0; i < N_TRAIN + N_TEST; i++) {
        Sample *s = i < N_TRAIN ? &g_train[i] : &g_test[i-N_TRAIN];
        s->label = i % 2;
        float c = s->label ? 0.8f : 0.2f;
        for (int f = 0; f < FEAT; f++) s->x[f] = rn(c, 0.08f);
    }
}

/* =========================================================================
 * In-memory loopback (length-prefixed byte queues)
 * ========================================================================= */
#define Q_SZ 65536
typedef struct { uint8_t b[Q_SZ]; uint32_t h, t; } BQ;
static BQ g_h2s, g_s2h;   /* herald->shard, shard->herald */

static void q_push(BQ *q, const uint8_t *d, uint32_t n)
    { for (uint32_t i=0;i<n;i++) q->b[(q->t++)%Q_SZ]=d[i]; }

static int q_pop(BQ *q, uint8_t *o, uint32_t *n) {
    if (q->h==q->t) { *n=0; return 0; }
    uint8_t fl = q->b[q->h%Q_SZ];
    if (q->t-q->h < (uint32_t)(1+fl)) { *n=0; return 0; }
    q->h++;
    for (uint8_t i=0;i<fl;i++) o[i]=q->b[(q->h++)%Q_SZ];
    *n=fl; return 1;
}

static void q_push_frame(BQ *q, const uint8_t *f, uint8_t n)
    { q_push(q,&n,1); q_push(q,f,n); }

/* =========================================================================
 * Shard HAL  (reads from g_h2s, writes to g_s2h)
 * ========================================================================= */
static int   hal_send(const uint8_t *b, uint8_t n)
             { q_push_frame(&g_s2h,b,n); return 0; }
static int   hal_recv(uint8_t *b, uint8_t *n, uint32_t t)
             { (void)t; uint32_t r=0; int ok=q_pop(&g_h2s,b,&r); *n=(uint8_t)r; return ok?0:1; }
static void  hal_slp(uint32_t ms)  { (void)ms; }
static void  hal_dslp(uint32_t s)  { (void)s; }
static int   hal_nw(uint32_t o,const void*d,size_t l){(void)o;(void)d;(void)l;return 0;}
static int   hal_nr(uint32_t o,void*d,size_t l)      {(void)o;(void)d;(void)l;return -1;}
static uint32_t hal_tick(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint32_t)(ts.tv_sec*1000+ts.tv_nsec/1000000);
}
static const FLHal g_hal = {
    .lora_send=hal_send, .lora_recv=hal_recv, .lora_set_sf=NULL,
    .sleep_ms=hal_slp, .deep_sleep_rtc=hal_dslp, .get_tick_ms=hal_tick,
    .nvs_write=hal_nw, .nvs_read=hal_nr, .log=NULL,
};

/* =========================================================================
 * Data callback
 * ========================================================================= */
static uint16_t g_sidx = 0;

int fl_user_data_cb(void *c, FLSample *s) {
    (void)c;
    if (g_sidx >= N_TRAIN) { g_sidx=0; return 0; }
    const Sample *r = &g_train[g_sidx++];
    for (int i=0;i<FEAT;i++) s->input[i]=r->x[i];
    s->label[0]=(r->label==0)?1.f:0.f;
    s->label[1]=(r->label==1)?1.f:0.f;
    s->input_len=FEAT; s->label_len=CLASSES;
    return 1;
}

/* =========================================================================
 * Helpers
 * ========================================================================= */
static void h_push(FLFrameType t, uint8_t rid,
                   uint8_t fi, uint8_t ft,
                   const uint8_t *p, uint8_t pl) {
    FLFrame f;
    fl_frame_encode(&f,t,FL_SHARD_ID_HERALD,rid,fi,ft,p,pl);
    q_push_frame(&g_h2s,(uint8_t*)&f,(uint8_t)(FL_HEADER_SIZE+pl));
}

static float acc(FLModel *m) {
    static float inter[N_PARAMS], out[CLASSES];
    int c=0;
    for (int i=0;i<N_TEST;i++) {
        fl_model_forward(m,g_test[i].x,out,inter);
        if ((out[1]>out[0]?1:0)==g_test[i].label) c++;
    }
    return 100.f*c/N_TEST;
}

static void eval(FLModel *m, const char *tag) {
    static float inter[N_PARAMS], out[CLASSES];
    int c=0; float loss=0;
    for (int i=0;i<N_TEST;i++) {
        fl_model_forward(m,g_test[i].x,out,inter);
        float mx=out[0]>out[1]?out[0]:out[1];
        float e0=expf(out[0]-mx),e1=expf(out[1]-mx),s=e0+e1;
        float p0=e0/s,p1=e1/s;
        if ((p1>p0?1:0)==g_test[i].label) c++;
        loss += -logf((g_test[i].label==0?p0:p1)+1e-9f);
    }
    printf("    %-28s  acc=%5.1f%%  loss=%.4f\n",
           tag, 100.f*c/N_TEST, loss/N_TEST);
}

/* =========================================================================
 * Main
 * ========================================================================= */
int main(void) {
    printf("\n");
    printf("=======================================================\n");
    printf("  Filum  -  End-to-End Federated Learning Demo\n");
    printf("  1 Herald  |  1 Shard  |  Synthetic Data  |  No HW\n");
    printf("=======================================================\n\n");

    make_data();
    printf("Dataset  : %d train / %d test  |  %d features  |  %d classes\n",
           N_TRAIN, N_TEST, FEAT, CLASSES);
    printf("Task     : classify N(0.2) vs N(0.8) clusters\n\n");

    /* Model */
    static const FLLayerDesc layers[] = {
        {.type=FL_LAYER_LINEAR,.activation=FL_ACT_RELU,
         .in_features=FEAT,.out_features=HIDDEN,
         .param_count=FEAT*HIDDEN+HIDDEN},
        {.type=FL_LAYER_LINEAR,.activation=FL_ACT_SIGMOID,
         .in_features=HIDDEN,.out_features=CLASSES,
         .param_count=HIDDEN*CLASSES+CLASSES},
    };
    static FLModel g_global, g_local;
    fl_model_init(&g_global, layers, 2);
    fl_model_init_random(&g_global, 0xABCD);
    memcpy(&g_local, &g_global, sizeof(FLModel));

    printf("Model    : %d->[%d]->%d  (%d parameters)\n\n",
           FEAT, HIDDEN, CLASSES, g_global.total_params);
    printf("Baseline : acc=%.1f%% (random weights)\n\n", acc(&g_global));

    /* Shard */
    static FLShard shard;
    fl_shard_init(&shard, &g_hal, 0x0001, &g_local);

    /* Herald-side state */
    static FLFragmentPool pool;
    static FLAggregator   agg;
    static float dense_delta[N_PARAMS], agg_delta[N_PARAMS];

    printf("%-5s  %-10s  %-11s  %-13s  %-11s  %-8s\n",
           "Round","Shard acc","Global acc","Entries TX","Bytes saved","Time ms");
    printf("%-5s  %-10s  %-11s  %-13s  %-11s  %-8s\n",
           "-----","----------","-----------","----------","-----------","-------");

    for (int rid = 0; rid < N_ROUNDS; rid++) {

        memset(&g_h2s,0,sizeof(g_h2s)); memset(&g_s2h,0,sizeof(g_s2h));
        uint32_t t0 = hal_tick();

        /* === HERALD: broadcast BEACON === */
        {
            FLBeaconPayload bp = {
                .round_id=(uint8_t)rid, .window_seconds=60,
                .model_param_count=(uint16_t)g_global.total_params,
                .min_shards=1, .local_epochs=LOCAL_EPOCHS, .learning_rate=LR,
            };
            h_push(FL_FRAME_BEACON,(uint8_t)rid,0,1,(uint8_t*)&bp,(uint8_t)sizeof(bp));
        }

        /* === SHARD: IDLE tick -> reads BEACON -> TRAINING === */
        fl_shard_tick(&shard);   /* IDLE: calls lora_recv, gets beacon */

        /* === SHARD: TRAINING tick -> trains, computes delta -> ENCODING === */
        g_sidx = 0;
        fl_shard_tick(&shard);

        /* === SHARD: ENCODING tick -> sparsifies -> TX === */
        fl_shard_tick(&shard);

        /* === SHARD: TX ticks -> sends all UPDATE fragments === */
        while (shard.state == FL_SHARD_STATE_TX)
            fl_shard_tick(&shard);
        /* shard is now AWAIT_ACK */

        /* === HERALD: drain s2h, reassemble UPDATE === */
        fl_pool_init(&pool, 300);
        {
            uint8_t rx[FL_FRAME_MAX_SIZE]; uint32_t rn=0;
            while (q_pop(&g_s2h,rx,&rn) && rn>0) {
                FLFrame fr;
                if (fl_frame_decode(rx,rn,&fr)==0 &&
                    fr.frame_type==FL_FRAME_UPDATE)
                    fl_pool_ingest(&pool,&fr,(uint8_t)(rn-FL_HEADER_SIZE));
                rn=0;
            }
        }

        /* === HERALD: aggregate === */
        fl_agg_init(&agg,FL_AGG_FEDAVG,g_global.params,g_global.total_params);
        uint16_t entries = 0;
        const FLSparseBuffer *sb = fl_pool_get_complete(&pool,0x0001);
        if (sb) {
            entries = sb->count;
            fl_sparse_decode(sb,dense_delta,g_global.total_params,0);
            fl_agg_add(&agg,dense_delta,g_global.total_params,N_TRAIN);
        }
        fl_agg_finalize(&agg,agg_delta);
        fl_model_apply_delta(&g_global,agg_delta,g_global.total_params);

        /* === HERALD: ACK === */
        h_push(FL_FRAME_ACK,(uint8_t)rid,0,1,NULL,0);

        /* === SHARD: AWAIT_ACK tick -> reads ACK -> RECV_DELTA === */
        fl_shard_tick(&shard);

        /* === HERALD: send DELTA back to shard === */
        {
            uint16_t topk=(uint16_t)(g_global.total_params/TOPK_RATIO);
            if (topk<1) topk=1;
            FLSparseBuffer dsb; memset(&dsb,0,sizeof(dsb));
            fl_sparse_encode(agg_delta,g_global.total_params,topk,NULL,&dsb);
            uint8_t ft=fl_sparse_fragment_count(&dsb);
            if (ft==0) ft=1;  /* send at least an empty frame */
            for (uint8_t f=0;f<ft;f++) {
                uint8_t pl[FL_PAYLOAD_MAX]; uint8_t pn=0;
                if (dsb.count>0) fl_sparse_write_fragment(pl,&pn,&dsb,f);
                h_push(FL_FRAME_DELTA,(uint8_t)rid,f,ft,pl,pn);
            }
        }

        /* === SHARD: RECV_DELTA ticks -> receives delta fragments -> IDLE === */
        {
            int safety = 0;
            while (shard.state == FL_SHARD_STATE_RECV_DELTA && safety++ < 64) {
                uint8_t rx[FL_FRAME_MAX_SIZE]; uint32_t rn=0;
                if (!q_pop(&g_h2s,rx,&rn) || rn==0) break;
                fl_shard_on_rx(&shard,rx,(size_t)rn);
            }
        }

        /* === HERALD: ROUND_CLOSE === */
        {
            FLRoundClosePayload rcp={.round_id=(uint8_t)rid,.shards_aggregated=1,.next_round_in_sec=0};
            h_push(FL_FRAME_ROUND_CLOSE,(uint8_t)rid,0,1,(uint8_t*)&rcp,(uint8_t)sizeof(rcp));
        }

        uint32_t elapsed = hal_tick()-t0;

        uint32_t b_dense  = g_global.total_params*4;
        uint32_t b_sparse = entries*(uint32_t)sizeof(FLGradientEntry);
        uint32_t saved    = b_dense > b_sparse ? b_dense-b_sparse : 0;

        printf("  %3d    %6.1f%%      %6.1f%%      %4d entries   %4u bytes   %4u ms\n",
               rid+1, acc(&g_local), acc(&g_global),
               entries, saved, (unsigned)elapsed);

        shard.state = FL_SHARD_STATE_IDLE;
    }

    /* Final report */
    printf("\nFinal results on held-out test set:\n");
    eval(&g_local,  "shard local model");
    eval(&g_global, "herald global model");

    uint16_t topk        = (uint16_t)(g_global.total_params / TOPK_RATIO);
    uint32_t b_dense     = g_global.total_params * 4;
    uint32_t b_sparse    = topk * (uint32_t)sizeof(FLGradientEntry);
    uint8_t  n_packets   = (uint8_t)((topk + FL_GRADIENT_ENTRIES_PER_FRAME-1)
                            / FL_GRADIENT_ENTRIES_PER_FRAME);
    printf("\nWire efficiency:\n");
    printf("  Dense upload   : %u bytes  (%u params x 4B float)\n", b_dense, g_global.total_params);
    printf("  Sparse upload  : %u bytes  (%u entries x 3B Q8, top-%d%%)\n",
           b_sparse, topk, 100/TOPK_RATIO);
    printf("  Compression    : %.1fx smaller\n", (float)b_dense/b_sparse);
    printf("  LoRa packets   : %d packet(s) per round at SF7\n\n", n_packets);
    return 0;
}
