#include "fl_error.h"
#include "fl_frame.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s (line %d)\n",n,__LINE__);}}while(0)
static void test_encode_decode(void){
    printf("--- encode_decode ---\n");
    FLFrame f; uint8_t p[]={0x01,0x02,0x03,0xAB};
    CHECK(fl_frame_encode(&f,FL_FRAME_UPDATE,0x42,7,0,1,p,4)==0,"encode OK");
    CHECK(f.magic==FL_MAGIC,"magic");
    CHECK(f.frame_type==FL_FRAME_UPDATE,"type");
    CHECK(f.shard_id==0x42,"shard_id");
    CHECK(f.payload[3]==0xAB,"payload");
    FLFrame d; uint8_t w[FL_FRAME_MAX_SIZE];
    memcpy(w,&f,FL_HEADER_SIZE+4);
    CHECK(fl_frame_decode(w,FL_HEADER_SIZE+4,&d)==0,"decode OK");
    CHECK(d.payload[3]==0xAB,"decoded payload");
}
static void test_bad_magic(void){
    printf("--- bad_magic ---\n");
    uint8_t w[FL_FRAME_MAX_SIZE]={0}; FLFrame f;
    CHECK(fl_frame_decode(w,FL_HEADER_SIZE,&f)==FL_ERR_BAD_MAGIC,"bad magic -> FL_ERR_BAD_MAGIC");
}
static void test_crc_corrupt(void){
    printf("--- crc_corrupt ---\n");
    FLFrame f; uint8_t p[]={0xCA,0xFE};
    fl_frame_encode(&f,FL_FRAME_BEACON,0xFFFF,1,0,1,p,2);
    uint8_t w[FL_FRAME_MAX_SIZE]; memcpy(w,&f,FL_HEADER_SIZE+2);
    w[10]^=0xFF; FLFrame d;
    CHECK(fl_frame_decode(w,FL_HEADER_SIZE+2,&d)==FL_ERR_CRC,"crc corrupt -> FL_ERR_CRC");
}
static void test_oversized(void){
    printf("--- oversized ---\n");
    FLFrame f; uint8_t big[FL_PAYLOAD_MAX+1]={0};
    CHECK(fl_frame_encode(&f,FL_FRAME_UPDATE,1,1,0,1,big,FL_PAYLOAD_MAX+1)==FL_ERR_BUFFER_TOO_SMALL,"oversized -> FL_ERR_BUFFER_TOO_SMALL");
}
int main(void){
    printf("=== test_frame ===\n");
    test_encode_decode(); test_bad_magic(); test_crc_corrupt(); test_oversized();
    printf("\n%d/%d passed\n",P,R); return P==R?0:1;
}
