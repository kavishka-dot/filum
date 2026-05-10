#include "fl_sparse.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s (line %d)\n",n,__LINE__);}}while(0)
#define EQ(a,b,e) (fabsf((a)-(b))<(e))
static void test_topk(void){
    printf("--- topk ---\n");
    float g[100]={0}; g[10]=0.9f; g[50]=-0.8f; g[99]=0.7f;
    FLSparseBuffer b; memset(&b,0,sizeof(b));
    uint16_t k=fl_sparse_encode(g,100,3,NULL,&b);
    CHECK(k==3,"3 selected");
    CHECK(b.entries[0].param_index==10,"top-1 idx=10");
    CHECK(b.entries[1].param_index==50,"top-2 idx=50");
}
static void test_decode(void){
    printf("--- decode ---\n");
    float g[50]={0}; g[5]=0.5f; g[20]=-0.6f;
    FLSparseBuffer b; memset(&b,0,sizeof(b));
    fl_sparse_encode(g,50,5,NULL,&b);
    float r[50]={0}; fl_sparse_decode(&b,r,50,0);
    CHECK(EQ(r[5],0.5f,0.02f),"idx5 recovered");
    CHECK(EQ(r[20],-0.6f,0.02f),"idx20 recovered");
    CHECK(EQ(r[0],0.0f,1e-6f),"zero preserved");
}
static void test_frag(void){
    printf("--- frag ---\n");
    FLSparseBuffer b; b.count=200;
    for(int i=0;i<200;i++){b.entries[i].param_index=(uint16_t)i;b.entries[i].delta_q8=(int8_t)(i%127);}
    uint8_t nf=fl_sparse_fragment_count(&b);
    CHECK(nf==3,"200 entries -> 3 frags");
    FLSparseBuffer r; memset(&r,0,sizeof(r));
    for(uint8_t f=0;f<nf;f++){
        uint8_t pl[FL_PAYLOAD_MAX],plen=0;
        fl_sparse_write_fragment(pl,&plen,&b,f);
        fl_sparse_read_fragment(pl,plen,&r);
    }
    CHECK(r.count==200,"reassembled count=200");
}
int main(void){
    printf("=== test_sparse ===\n");
    test_topk(); test_decode(); test_frag();
    printf("\n%d/%d passed\n",P,R); return P==R?0:1;
}
