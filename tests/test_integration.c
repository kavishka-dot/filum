#include "fl_error.h"
/*
 * tests/test_integration.c
 *
 * Integration test: full FL round through Herald + Shard via pipe loopback.
 * Exercises fl_herald.c, fl_shard.c, fl_fragment_pool.c, fl_frame.c,
 * fl_crc.c, hal_host.c code paths — the files with low coverage.
 *
 * No hardware. No threads. Single process, synchronous.
 * Uses POSIX pipes as the loopback transport.
 */

#define _POSIX_C_SOURCE 200809L

#include "fl_herald.h"
#include "fl_shard.h"
#include "fl_model.h"
#include "fl_train.h"
#include "fl_sparse.h"
#include "fl_frame.h"
#include "fl_dp.h"
#include "fl_crypto.h"
#include "hal.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

/* Suppress warn_unused_result for pipe() in tests */
static inline void open_pipe(int fd[2]) { if(pipe(fd)<0) fd[0]=fd[1]=-1; }

static int R=0, P=0;
#define CHECK(c,n) do{ R++; if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s (line %d)\n",n,__LINE__);} }while(0)
#define EQ(a,b,e) (fabsf((a)-(b))<(e))

/* =========================================================================
 * Model config
 * ========================================================================= */
#define FEAT    4
#define HIDDEN  8
#define CLASSES 2
#define N_PARAMS (FEAT*HIDDEN+HIDDEN+HIDDEN*CLASSES+CLASSES)

static const FLLayerDesc g_layers[] = {
    {.type=FL_LAYER_LINEAR,.activation=FL_ACT_RELU,
     .in_features=FEAT,.out_features=HIDDEN,
     .param_count=FEAT*HIDDEN+HIDDEN},
    {.type=FL_LAYER_LINEAR,.activation=FL_ACT_SIGMOID,
     .in_features=HIDDEN,.out_features=CLASSES,
     .param_count=HIDDEN*CLASSES+CLASSES},
};

/* =========================================================================
 * In-memory loopback (length-prefixed byte queue via pipe)
 * ========================================================================= */
#define Q_SZ (1<<16)
typedef struct { uint8_t b[Q_SZ]; uint32_t h,t; } BQ;

static void bq_push(BQ *q, const uint8_t *d, uint32_t n)
    { for(uint32_t i=0;i<n;i++) q->b[(q->t++)%Q_SZ]=d[i]; }

static int bq_pop(BQ *q, uint8_t *o, uint32_t *n) {
    if(q->h==q->t){*n=0;return 0;}
    uint8_t fl=q->b[q->h%Q_SZ];
    if(q->t-q->h<(uint32_t)(1+fl)){*n=0;return 0;}
    q->h++;
    for(uint8_t i=0;i<fl;i++) o[i]=q->b[(q->h++)%Q_SZ];
    *n=fl; return 1;
}

static void bq_push_frame(BQ *q, const uint8_t *f, uint8_t n)
    { bq_push(q,&n,1); bq_push(q,f,n); }

static BQ g_h2s, g_s2h;

/* =========================================================================
 * Shard HAL backed by BQ
 * ========================================================================= */
static int   hal_send(const uint8_t *b,uint8_t n) { bq_push_frame(&g_s2h,b,n); return 0; }
static int   hal_recv(uint8_t *b,uint8_t *n,uint32_t t)
             { (void)t; uint32_t r=0; int ok=bq_pop(&g_h2s,b,&r); *n=(uint8_t)r; return ok?0:1; }
static void  hal_slp(uint32_t ms){(void)ms;}
static void  hal_dslp(uint32_t s){(void)s;}
static int   hal_nw(uint32_t o,const void*d,size_t l){(void)o;(void)d;(void)l;return 0;}
static int   hal_nr(uint32_t o,void*d,size_t l){(void)o;(void)d;(void)l;return -1;}
static uint32_t hal_tick(void){return 0;}

static const FLHal g_hal={
    .lora_send=hal_send,.lora_recv=hal_recv,.lora_set_sf=NULL,
    .sleep_ms=hal_slp,.deep_sleep_rtc=hal_dslp,.get_tick_ms=hal_tick,
    .nvs_write=hal_nw,.nvs_read=hal_nr,.log=NULL,
};

/* Herald reads from s2h, writes to h2s */
/* We fake serial I/O: write herald output into h2s, read shard output from s2h */

/* Herald transport shim: serial_write -> push to h2s, serial_read -> pop from s2h */
/* Since fl_herald_init_fd takes a real fd, we use a pipe pair */
static int g_herald_rx[2];  /* herald reads shard data: g_herald_rx[0] */
static int g_herald_tx[2];  /* herald writes to shard:  g_herald_tx[1] */

/* =========================================================================
 * Data callback
 * ========================================================================= */
static int g_sidx=0;
static float g_data[20][FEAT+CLASSES]={
    {0.2f,0.2f,0.2f,0.2f, 1.f,0.f},
    {0.8f,0.8f,0.8f,0.8f, 0.f,1.f},
    {0.15f,0.25f,0.2f,0.2f, 1.f,0.f},
    {0.85f,0.75f,0.8f,0.8f, 0.f,1.f},
};
#define N_SAMPLES 4

int fl_user_data_cb(void *c, FLSample *s) {
    (void)c;
    if(g_sidx>=N_SAMPLES){g_sidx=0;return 0;}
    const float *r=g_data[g_sidx++];
    for(int i=0;i<FEAT;i++) s->input[i]=r[i];
    for(int i=0;i<CLASSES;i++) s->label[i]=r[FEAT+i];
    s->input_len=FEAT; s->label_len=CLASSES;
    return 1;
}

/* =========================================================================
 * Test: full FL round via fl_herald_init_fd + pipe loopback
 * ========================================================================= */
static void test_herald_init_fd(void) {
    printf("--- herald_init_fd ---\n");

    /* Pipes: herald_rx reads shard->herald, herald_tx writes herald->shard */
    open_pipe(g_herald_rx);
    open_pipe(g_herald_tx);

    float global_model[N_PARAMS]={0};
    FLHeraldConfig cfg={
        .serial_port=NULL, .baud_rate=0,
        .model_param_count=N_PARAMS,
        .global_model=global_model,
        .round_policy={.window_seconds=60,.inter_round_delay_s=0,
                       .min_shards=1,.max_shards=1,
                       .local_epochs=2,.learning_rate=0.05f},
        .aggregator=FL_AGG_FEDAVG,
        .log_level=0,
    };

    FLHerald herald;
    /* Herald reads from shard-to-herald pipe */
    int ret = fl_herald_init_fd(&herald, &cfg, g_herald_rx[0]);
    CHECK(ret==0, "fl_herald_init_fd returns 0");
    CHECK(herald.serial_fd==g_herald_rx[0], "serial_fd set to pipe");
    CHECK(herald.running==1, "running flag set");

    fl_herald_destroy(&herald);
    close(g_herald_rx[0]); close(g_herald_rx[1]);
    close(g_herald_tx[0]); close(g_herald_tx[1]);
}

/* =========================================================================
 * Test: fl_herald_send_beacon writes a valid frame
 * ========================================================================= */
static void test_herald_send_beacon(void) {
    printf("--- herald_send_beacon ---\n");

    int pfd[2]; open_pipe(pfd);
    float gm[N_PARAMS]={0};
    FLHeraldConfig cfg={
        .serial_port=NULL,.baud_rate=0,
        .model_param_count=N_PARAMS,.global_model=gm,
        .round_policy={.window_seconds=3600,.min_shards=1,.max_shards=10,
                       .local_epochs=3,.learning_rate=0.01f},
        .aggregator=FL_AGG_FEDAVG,.log_level=0,
    };
    FLHerald herald; fl_herald_init_fd(&herald, &cfg, pfd[1]);
    int r = fl_herald_send_beacon(&herald, 5);
    CHECK(r==0, "send_beacon returns 0");

    /* Read frame from pipe and validate (herald writes raw, no length prefix) */
    uint8_t wire[FL_FRAME_MAX_SIZE];
    ssize_t n = read(pfd[0], wire, sizeof(wire));
    CHECK(n > FL_HEADER_SIZE, "frame has payload");
    FLFrame frame;
    int dec = fl_frame_decode(wire, (size_t)n, &frame);
    CHECK(dec==0, "decoded beacon is valid");
    CHECK(frame.frame_type==FL_FRAME_BEACON, "type=BEACON");
    CHECK(frame.round_id==5, "round_id=5");

    FLBeaconPayload bp;
    memcpy(&bp, frame.payload, sizeof(bp));
    CHECK(bp.local_epochs==3, "beacon.local_epochs=3");
    CHECK(EQ(bp.learning_rate,0.01f,1e-5f), "beacon.learning_rate=0.01");

    fl_herald_destroy(&herald);
    close(pfd[0]); close(pfd[1]);
}

/* =========================================================================
 * Test: fl_herald_send_delta encodes and sends sparse delta
 * ========================================================================= */
static void test_herald_send_delta(void) {
    printf("--- herald_send_delta ---\n");

    int pfd[2]; open_pipe(pfd);
    float gm[N_PARAMS];
    for(int i=0;i<N_PARAMS;i++) gm[i]=(float)i*0.01f;
    FLHeraldConfig cfg={
        .serial_port=NULL,.baud_rate=0,
        .model_param_count=N_PARAMS,.global_model=gm,
        .round_policy={.window_seconds=60,.min_shards=1,.max_shards=1,
                       .local_epochs=1,.learning_rate=0.01f},
        .aggregator=FL_AGG_FEDAVG,.log_level=0,
    };
    FLHerald herald; fl_herald_init_fd(&herald, &cfg, pfd[1]);

    float delta[N_PARAMS];
    for(int i=0;i<N_PARAMS;i++) delta[i]=(i%2==0)?0.1f:-0.1f;
    uint16_t topk=N_PARAMS/4; if(topk<1)topk=1;

    int r = fl_herald_send_delta(&herald, delta, topk);
    CHECK(r==0, "send_delta returns 0");

    /* At least one DELTA frame should be readable (raw, no length prefix) */
    uint8_t wire[FL_FRAME_MAX_SIZE];
    ssize_t n = read(pfd[0], wire, sizeof(wire));
    CHECK(n > FL_HEADER_SIZE, "delta frame written to pipe");
    FLFrame frame; fl_frame_decode(wire,(size_t)n,&frame);
    CHECK(frame.frame_type==FL_FRAME_DELTA, "type=DELTA");

    fl_herald_destroy(&herald);
    close(pfd[0]); close(pfd[1]);
}

/* =========================================================================
 * Test: full round — shard trains, herald aggregates, delta applied
 * ========================================================================= */
static void test_full_round(void) {
    printf("--- full_round (Herald+Shard) ---\n");

    memset(&g_h2s,0,sizeof(g_h2s)); memset(&g_s2h,0,sizeof(g_s2h));

    /* Setup global model */
    static FLModel global, local;
    fl_model_init(&global, g_layers, 2);
    fl_model_init_random(&global, 0xBEEF);
    memcpy(&local, &global, sizeof(FLModel));

    /* Init shard */
    static FLShard shard;
    fl_shard_init(&shard, &g_hal, 0x0001, &local);

    /* Herald helper: push frame into h2s (shard RX queue) */
    #define H_PUSH(type,rid,fi,ft,p,pl) do { \
        FLFrame _f; fl_frame_encode(&_f,type,FL_SHARD_ID_HERALD,rid,fi,ft,p,pl); \
        bq_push_frame(&g_h2s,(uint8_t*)&_f,(uint8_t)(FL_HEADER_SIZE+pl)); \
    } while(0)

    /* Step 1: Herald sends BEACON */
    FLBeaconPayload bp={.round_id=0,.window_seconds=60,
                        .model_param_count=(uint16_t)global.total_params,
                        .min_shards=1,.local_epochs=2,.learning_rate=0.05f};
    H_PUSH(FL_FRAME_BEACON,0,0,1,(uint8_t*)&bp,(uint8_t)sizeof(bp));

    /* Step 2: Shard receives BEACON -> TRAINING */
    fl_shard_tick(&shard);
    CHECK(shard.state==FL_SHARD_STATE_TRAINING, "shard->TRAINING after beacon");

    /* Step 3: Shard trains */
    g_sidx=0; fl_shard_tick(&shard);
    CHECK(shard.state==FL_SHARD_STATE_ENCODING, "shard->ENCODING after train");

    /* Step 4: Shard encodes */
    fl_shard_tick(&shard);
    CHECK(shard.state==FL_SHARD_STATE_TX, "shard->TX after encode");

    /* Step 5: Shard transmits */
    while(shard.state==FL_SHARD_STATE_TX) fl_shard_tick(&shard);
    CHECK(shard.state==FL_SHARD_STATE_AWAIT_ACK, "shard->AWAIT_ACK after TX");

    /* Step 6: Herald reassembles from s2h */
    static FLFragmentPool pool; fl_pool_init(&pool,300);
    uint8_t rx[FL_FRAME_MAX_SIZE]; uint32_t rn=0;
    int frags_received=0;
    while(bq_pop(&g_s2h,rx,&rn) && rn>0) {
        FLFrame fr; if(fl_frame_decode(rx,rn,&fr)==0 && fr.frame_type==FL_FRAME_UPDATE) {
            fl_pool_ingest(&pool,&fr,(uint8_t)(rn-FL_HEADER_SIZE));
            frags_received++;
        }
        rn=0;
    }
    CHECK(frags_received>0, "herald received at least 1 UPDATE fragment");

    const FLSparseBuffer *sb=fl_pool_get_complete(&pool,0x0001);
    CHECK(sb!=NULL, "fragment pool: update complete for shard 0x0001");
    if(sb) CHECK(sb->count>0, "sparse buffer has entries");

    /* Step 7: Herald aggregates */
    static FLAggregator agg;
    float global_params[N_PARAMS];
    memcpy(global_params,global.params,global.total_params*sizeof(float));
    fl_agg_init(&agg,FL_AGG_FEDAVG,global_params,global.total_params);
    static float dense[N_PARAMS];
    if(sb){ fl_sparse_decode(sb,dense,global.total_params,0);
            fl_agg_add(&agg,dense,global.total_params,N_SAMPLES); }
    static float agg_delta[N_PARAMS]; fl_agg_finalize(&agg,agg_delta);
    fl_model_apply_delta(&global,agg_delta,global.total_params);

    /* Step 8: Herald sends ACK */
    H_PUSH(FL_FRAME_ACK,0,0,1,NULL,0);

    /* Step 9: Shard receives ACK -> RECV_DELTA */
    fl_shard_tick(&shard);
    CHECK(shard.state==FL_SHARD_STATE_RECV_DELTA,"shard->RECV_DELTA after ACK");

    /* Step 10: Herald sends DELTA */
    {
        uint16_t topk=(uint16_t)(global.total_params/4); if(topk<1)topk=1;
        FLSparseBuffer dsb; memset(&dsb,0,sizeof(dsb));
        fl_sparse_encode(agg_delta,global.total_params,topk,NULL,&dsb);
        uint8_t ft=fl_sparse_fragment_count(&dsb); if(ft==0)ft=1;
        for(uint8_t f=0;f<ft;f++){
            uint8_t pl[FL_PAYLOAD_MAX]; uint8_t pn=0;
            if(dsb.count>0) fl_sparse_write_fragment(pl,&pn,&dsb,f);
            H_PUSH(FL_FRAME_DELTA,0,f,ft,pl,pn);
        }
    }

    /* Step 11: Shard receives DELTA -> IDLE */
    {
        int safety=0;
        while(shard.state==FL_SHARD_STATE_RECV_DELTA && safety++<64){
            uint8_t rx2[FL_FRAME_MAX_SIZE]; uint32_t rn2=0;
            if(!bq_pop(&g_h2s,rx2,&rn2)||rn2==0) break;
            fl_shard_on_rx(&shard,rx2,(size_t)rn2);
        }
    }
    CHECK(shard.state==FL_SHARD_STATE_IDLE,"shard->IDLE after delta");
    CHECK(shard.rounds_completed==1,"rounds_completed==1");

    /* Step 12: Herald sends ROUND_CLOSE */
    FLRoundClosePayload rcp={.round_id=0,.shards_aggregated=1,.next_round_in_sec=0};
    H_PUSH(FL_FRAME_ROUND_CLOSE,0,0,1,(uint8_t*)&rcp,(uint8_t)sizeof(rcp));

    #undef H_PUSH
}

/* =========================================================================
 * Test: fragment pool — duplicate rejection, timeout, complete count
 * ========================================================================= */
static void test_fragment_pool(void) {
    printf("--- fragment_pool ---\n");

    FLFragmentPool pool; fl_pool_init(&pool, 300);
    CHECK(fl_pool_complete_count(&pool)==0, "pool initially empty");

    /* Build a 2-fragment sparse buffer */
    FLSparseBuffer sbuf; sbuf.count=150;
    for(int i=0;i<150;i++){sbuf.entries[i].param_index=(uint16_t)i;sbuf.entries[i].delta_q8=(int8_t)(i%127);}
    CHECK(fl_sparse_fragment_count(&sbuf)==3,"150 entries -> 3 fragments");

    /* Ingest all fragments */
    for(uint8_t f=0;f<3;f++){
        uint8_t pl[FL_PAYLOAD_MAX]; uint8_t pn=0;
        fl_sparse_write_fragment(pl,&pn,&sbuf,f);
        FLFrame fr;
        fl_frame_encode(&fr,FL_FRAME_UPDATE,0x0042,1,f,3,pl,pn);
        int r=fl_pool_ingest(&pool,&fr,pn);
        if(f<2) CHECK(r==0,"intermediate fragment: not yet complete");
        else    CHECK(r==1,"last fragment: complete");
    }
    CHECK(fl_pool_complete_count(&pool)==1,"pool has 1 complete update");

    /* Duplicate fragment rejected */
    uint8_t pl[FL_PAYLOAD_MAX]; uint8_t pn=0;
    fl_sparse_write_fragment(pl,&pn,&sbuf,0);
    FLFrame dup; fl_frame_encode(&dup,FL_FRAME_UPDATE,0x0042,1,0,3,pl,pn);
    int dup_r=fl_pool_ingest(&pool,&dup,pn);
    CHECK(dup_r==0,"duplicate fragment: ignored (returns 0)");

    /* Get and release */
    const FLSparseBuffer *sb=fl_pool_get_complete(&pool,0x0042);
    CHECK(sb!=NULL,"get_complete returns buffer");
    if(sb) CHECK(sb->count==150,"reassembled count==150");
    fl_pool_release(&pool,0x0042);
    CHECK(fl_pool_complete_count(&pool)==0,"pool empty after release");
}

/* =========================================================================
 * Test: CRC catches all single-bit errors
 * ========================================================================= */
static void test_crc_exhaustive(void) {
    printf("--- crc_exhaustive ---\n");
    FLFrame f;
    uint8_t payload[8]={0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    fl_frame_encode(&f,FL_FRAME_UPDATE,0x0001,1,0,1,payload,8);

    uint8_t wire[FL_FRAME_MAX_SIZE];
    size_t wire_len=FL_HEADER_SIZE+8;
    memcpy(wire,&f,wire_len);

    int caught=0, total=0;
    /* Flip each bit in the payload and check CRC catches it */
    for(int byte=10;byte<(int)wire_len;byte++){
        for(int bit=0;bit<8;bit++){
            uint8_t w2[FL_FRAME_MAX_SIZE]; memcpy(w2,wire,wire_len);
            w2[byte]^=(1<<bit);
            FLFrame d; int r=fl_frame_decode(w2,wire_len,&d);
            if(r==FL_ERR_CRC) caught++;
            total++;
        }
    }
    /* CRC-16 catches all single-bit errors */
    CHECK(caught==total,"CRC catches all single-bit payload errors");
    printf("  caught %d/%d single-bit errors\n",caught,total);
}

/* =========================================================================
 * Test: DP + crypto enabled on shard — state machine still works
 * ========================================================================= */
static void test_shard_with_security(void) {
    printf("--- shard_with_dp_and_crypto ---\n");

    memset(&g_h2s,0,sizeof(g_h2s)); memset(&g_s2h,0,sizeof(g_s2h));

    static FLModel model2;
    fl_model_init(&model2,g_layers,2);
    fl_model_init_random(&model2,0xCAFE);

    static FLShard shard2;
    fl_shard_init(&shard2,&g_hal,0x0002,&model2);

    /* Enable both DP and crypto */
    fl_shard_enable_dp(&shard2, 2.0f, 1e-4f, 1.0f);
    CHECK(shard2.dp.dp_enabled==1,"DP enabled");
    CHECK(shard2.dp.sigma>0,"DP sigma>0");

    uint8_t seed[32]={0x48,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                      0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
                      0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                      0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x40};
    fl_shard_enable_crypto(&shard2, seed);
    CHECK(shard2.crypto.encrypt_enabled==1,"crypto enabled");
    CHECK(!shard2.crypto.handshake_done,"handshake not done yet");

    /* Push beacon directly */
    FLBeaconPayload bp={.round_id=0,.window_seconds=60,
                        .model_param_count=(uint16_t)model2.total_params,
                        .min_shards=1,.local_epochs=1,.learning_rate=0.05f};
    FLFrame bf; fl_frame_encode(&bf,FL_FRAME_BEACON,FL_SHARD_ID_HERALD,0,0,1,(uint8_t*)&bp,(uint8_t)sizeof(bp));
    bq_push_frame(&g_h2s,(uint8_t*)&bf,(uint8_t)(FL_HEADER_SIZE+sizeof(bp)));

    /* Tick: IDLE reads beacon -> TRAINING */
    fl_shard_tick(&shard2);
    CHECK(shard2.state==FL_SHARD_STATE_TRAINING,"shard2->TRAINING");

    /* Train + encode + TX */
    g_sidx=0; fl_shard_tick(&shard2); fl_shard_tick(&shard2);
    while(shard2.state==FL_SHARD_STATE_TX) fl_shard_tick(&shard2);
    CHECK(shard2.state==FL_SHARD_STATE_AWAIT_ACK,"shard2->AWAIT_ACK");
    CHECK(shard2.dp.rounds_spent==1,"DP: 1 round spent");

    /* Privacy report (just exercise the code path) */
    float et,dt; fl_dp_privacy_spent(&shard2.dp,&et,&dt);
    CHECK(et>0,"privacy budget nonzero after round");

    shard2.state=FL_SHARD_STATE_IDLE;
}

/* =========================================================================
 * Test: herald stop / destroy
 * ========================================================================= */
static void test_herald_stop(void) {
    printf("--- herald_stop_destroy ---\n");
    int pfd[2]; open_pipe(pfd);
    float gm[N_PARAMS]={0};
    FLHeraldConfig cfg={
        .serial_port=NULL,.baud_rate=0,
        .model_param_count=N_PARAMS,.global_model=gm,
        .round_policy={.window_seconds=60,.min_shards=1,.max_shards=1,
                       .local_epochs=1,.learning_rate=0.01f},
        .aggregator=FL_AGG_FEDAVG,.log_level=0,
    };
    FLHerald herald; fl_herald_init_fd(&herald,&cfg,pfd[0]);
    CHECK(herald.running==1,"running before stop");
    fl_herald_stop(&herald);
    CHECK(herald.running==0,"running==0 after stop");
    fl_herald_destroy(&herald);
    close(pfd[0]); close(pfd[1]);
}

/* =========================================================================
 * Main
 * ========================================================================= */
int main(void) {
    printf("=== test_integration ===\n");
    test_herald_init_fd();
    test_herald_send_beacon();
    test_herald_send_delta();
    test_full_round();
    test_fragment_pool();
    test_crc_exhaustive();
    test_shard_with_security();
    test_herald_stop();
    printf("\n%d/%d passed\n", P, R);
    return P==R ? 0 : 1;
}
