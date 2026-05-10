#include "fl_aggregator.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s\n",n);}}while(0)
#define EQ(a,b,e) (fabsf((a)-(b))<(e))
int main(void){
    printf("=== test_aggregator ===\n");
    float g[4]={0}; FLAggregator a;
    fl_agg_init(&a,FL_AGG_FEDAVG,g,4);
    float u1[]={0.2f,0.4f,-0.2f,0.6f};
    float u2[]={0.4f,0.2f,-0.4f,0.2f};
    fl_agg_add(&a,u1,4,50); fl_agg_add(&a,u2,4,50);
    float d[4]; fl_agg_finalize(&a,d);
    CHECK(EQ(d[0],0.3f,0.01f),"FedAvg[0]=0.3");
    CHECK(EQ(d[1],0.3f,0.01f),"FedAvg[1]=0.3");
    CHECK(EQ(d[2],-0.3f,0.01f),"FedAvg[2]=-0.3");
    fl_agg_reset(&a);
    CHECK(a.shard_count==0,"reset count");
    CHECK(a.accumulator[0]==0.0f,"reset accum");
    printf("\n%d/%d passed\n",P,R); return P==R?0:1;
}
