#include "fl_round.h"
#include <stdio.h>
#include <unistd.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s\n",n);}}while(0)
int main(void){
    printf("=== test_round ===\n");
    FLRoundPolicy p={.window_seconds=1,.min_shards=1,.max_shards=5,.local_epochs=2,.learning_rate=0.01f};
    FLRoundScheduler s; fl_round_init(&s,&p);
    CHECK(s.phase==FL_ROUND_IDLE,"starts IDLE");
    fl_round_open(&s);
    CHECK(s.phase==FL_ROUND_COLLECTING,"open->COLLECTING");
    CHECK(fl_round_is_window_open(&s),"window open");
    fl_round_shard_complete(&s,1);
    CHECK(s.shards_complete==1,"shard counted");
    sleep(2);
    CHECK(fl_round_should_close(&s),"closes after timeout");
    fl_round_close(&s);
    CHECK(s.phase==FL_ROUND_AGGREGATING,"closed->AGGREGATING");

    FLRoundPolicy p2={.window_seconds=3600,.min_shards=1,.max_shards=2,.learning_rate=0.01f};
    FLRoundScheduler s2; fl_round_init(&s2,&p2); fl_round_open(&s2);
    fl_round_shard_complete(&s2,1);
    CHECK(!fl_round_should_close(&s2),"not closed at 1/2");
    fl_round_shard_complete(&s2,2);
    CHECK(fl_round_should_close(&s2),"closes at 2/2");

    printf("\n%d/%d passed\n",P,R); return P==R?0:1;
}
