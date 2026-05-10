#include "fl_aggregator.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s (line %d)\n",n,__LINE__);}}while(0)
#define EQ(a,b,e) (fabsf((a)-(b))<(e))

static void test_median_odd(void){
    printf("--- median_odd (3 shards) ---\n");
    float g[4]={0};
    FLAggregator a; fl_agg_init(&a,FL_AGG_MEDIAN,g,4);
    float u1[]={1.f,2.f,3.f,4.f};
    float u2[]={10.f,20.f,30.f,40.f};  /* outlier (Byzantine) */
    float u3[]={1.2f,2.2f,3.2f,4.2f};
    fl_agg_add(&a,u1,4,10);
    fl_agg_add(&a,u2,4,10);  /* this is the Byzantine shard */
    fl_agg_add(&a,u3,4,10);
    float d[4]; fl_agg_finalize(&a,d);
    /* Median should be ~1.1, ~2.1, ~3.1, ~4.1 (ignores outlier) */
    CHECK(EQ(d[0],1.1f,0.2f),"median[0] ignores outlier");
    CHECK(EQ(d[1],2.1f,0.2f),"median[1] ignores outlier");
    CHECK(EQ(d[2],3.1f,0.2f),"median[2] ignores outlier");
}

static void test_median_even(void){
    printf("--- median_even (4 shards) ---\n");
    float g[2]={0};
    FLAggregator a; fl_agg_init(&a,FL_AGG_MEDIAN,g,2);
    float u1[]={1.f,2.f}, u2[]={3.f,4.f};
    float u3[]={5.f,6.f}, u4[]={7.f,8.f};
    fl_agg_add(&a,u1,2,10); fl_agg_add(&a,u2,2,10);
    fl_agg_add(&a,u3,2,10); fl_agg_add(&a,u4,2,10);
    float d[2]; fl_agg_finalize(&a,d);
    /* Even median: average of middle two = (3+5)/2=4, (4+6)/2=5 */
    CHECK(EQ(d[0],4.f,0.01f),"even median[0]=4");
    CHECK(EQ(d[1],5.f,0.01f),"even median[1]=5");
}

static void test_byzantine_robustness(void){
    printf("--- byzantine (5 honest, 2 malicious) ---\n");
    /* With 7 shards and coord-wise median, up to 3 can be Byzantine.
     * Here 2 malicious shards push toward 100 but median is unaffected. */
    float g[2]={0};
    FLAggregator a; fl_agg_init(&a,FL_AGG_MEDIAN,g,2);
    float honest[]={1.f,1.f};
    float evil[]={100.f,100.f};
    for(int i=0;i<5;i++) fl_agg_add(&a,honest,2,10);
    for(int i=0;i<2;i++) fl_agg_add(&a,evil,2,10);
    float d[2]; fl_agg_finalize(&a,d);
    CHECK(EQ(d[0],1.f,0.01f),"Byzantine attack failed on param[0]");
    CHECK(EQ(d[1],1.f,0.01f),"Byzantine attack failed on param[1]");
}

static void test_fedavg_still_works(void){
    printf("--- fedavg unaffected ---\n");
    float g[2]={0};
    FLAggregator a; fl_agg_init(&a,FL_AGG_FEDAVG,g,2);
    float u1[]={2.f,4.f}, u2[]={4.f,8.f};
    fl_agg_add(&a,u1,2,10); fl_agg_add(&a,u2,2,10);
    float d[2]; fl_agg_finalize(&a,d);
    CHECK(EQ(d[0],3.f,0.01f),"FedAvg[0]=3");
    CHECK(EQ(d[1],6.f,0.01f),"FedAvg[1]=6");
}

int main(void){
    printf("=== test_median ===\n");
    test_median_odd();
    test_median_even();
    test_byzantine_robustness();
    test_fedavg_still_works();
    printf("\n%d/%d passed\n",P,R);
    return P==R?0:1;
}
