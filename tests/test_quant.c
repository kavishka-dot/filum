#include "fl_quant.h"
#include <stdio.h>
#include <math.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s\n",n);}}while(0)
#define EQ(a,b,e) (fabsf((a)-(b))<(e))
static void test_scalar(void){
    printf("--- scalar ---\n");
    CHECK(fl_quant_q8(0.0f)==0,"zero");
    CHECK(fl_quant_q8(100.f)==127,"pos clamp");
    CHECK(fl_quant_q8(-100.f)==-128,"neg clamp");
    float vals[]={0.5f,-0.5f,0.99f,-0.99f};
    for(int i=0;i<4;i++) CHECK(EQ(fl_dequant_q8(fl_quant_q8(vals[i])),vals[i],0.02f),"roundtrip");
}
static void test_vector(void){
    printf("--- vector ---\n");
    float s[]={0.1f,-0.3f,0.7f,-0.9f}; int8_t d[4]; float sc;
    fl_quant_vec_q8(s,d,4,&sc);
    float r[4]; fl_dequant_vec_q8(d,r,4,sc);
    for(int i=0;i<4;i++) CHECK(EQ(r[i],s[i],0.02f),"vec roundtrip");
}
static void test_clip(void){
    printf("--- clip ---\n");
    float v[]={3.0f,4.0f};
    fl_grad_clip(v,2,1.0f);
    CHECK(EQ(fl_grad_l2(v,2),1.0f,1e-5f),"clip to norm 1");
}
int main(void){
    printf("=== test_quant ===\n");
    test_scalar(); test_vector(); test_clip();
    printf("\n%d/%d passed\n",P,R); return P==R?0:1;
}
