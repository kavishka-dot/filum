#include "fl_dp.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s (line %d)\n",n,__LINE__);}}while(0)
#define EQ(a,b,e) (fabsf((a)-(b))<(e))

static void test_sigma_calibration(void){
    printf("--- sigma_calibration ---\n");
    FLDPConfig cfg = fl_dp_config(1.0f, 1e-5f, 1.0f);
    CHECK(cfg.dp_enabled==1, "enabled");
    CHECK(cfg.sigma > 0.0f,  "sigma positive");
    /* sigma = sqrt(2*ln(1.25/1e-5)) / 1.0 ~= 4.84 */
    CHECK(EQ(cfg.sigma, 4.84f, 0.1f), "sigma ~= 4.84");
    printf("  sigma = %.4f\n", cfg.sigma);
}

static void test_disabled(void){
    printf("--- disabled ---\n");
    FLDPConfig cfg = fl_dp_config(0.0f, 0.0f, 1.0f);
    CHECK(cfg.dp_enabled==0, "disabled when eps=0");
    float v[4]={1.f,2.f,3.f,4.f};
    fl_dp_apply(&cfg,v,4,1,0);
    CHECK(EQ(v[0],1.f,1e-6f)&&EQ(v[3],4.f,1e-6f), "no-op when disabled");
}

static void test_noise_added(void){
    printf("--- noise_added ---\n");
    FLDPConfig cfg = fl_dp_config(1.0f, 1e-5f, 1.0f);
    float v[64]={0};
    fl_dp_apply(&cfg,v,64,0x0001,0);
    /* With sigma~3.84, at least some entries must be nonzero */
    float sum=0;
    for(int i=0;i<64;i++) sum+=v[i]*v[i];
    CHECK(sum > 0.1f, "noise is nonzero");
}

static void test_per_shard_unique(void){
    printf("--- per_shard_unique ---\n");
    FLDPConfig cfg1=fl_dp_config(1.f,1e-5f,1.f);
    FLDPConfig cfg2=fl_dp_config(1.f,1e-5f,1.f);
    float v1[16]={0}, v2[16]={0};
    fl_dp_apply(&cfg1,v1,16,0x0001,0);  /* shard 1 */
    fl_dp_apply(&cfg2,v2,16,0x0002,0);  /* shard 2 */
    float diff=0;
    for(int i=0;i<16;i++) diff+=fabsf(v1[i]-v2[i]);
    CHECK(diff > 0.01f, "different shards get different noise");
}

static void test_composition(void){
    printf("--- composition ---\n");
    FLDPConfig cfg=fl_dp_config(0.5f,1e-5f,1.f);
    float v[4]={0};
    fl_dp_apply(&cfg,v,4,1,0);
    fl_dp_apply(&cfg,v,4,1,1);
    CHECK(cfg.rounds_spent==2,"rounds tracked");
    float et,dt; fl_dp_privacy_spent(&cfg,&et,&dt);
    CHECK(EQ(et,1.0f,1e-4f),"basic composition eps");
}

int main(void){
    printf("=== test_dp ===\n");
    test_sigma_calibration();
    test_disabled();
    test_noise_added();
    test_per_shard_unique();
    test_composition();
    printf("\n%d/%d passed\n",P,R);
    return P==R?0:1;
}
