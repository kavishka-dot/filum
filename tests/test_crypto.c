#include "fl_error.h"
#include "fl_crypto.h"
#include <stdio.h>
#include <string.h>
static int R=0,P=0;
#define CHECK(c,n) do{R++;if(c){P++;printf("  PASS  %s\n",n);}else{printf("  FAIL  %s (line %d)\n",n,__LINE__);}}while(0)

static void test_keygen(void){
    printf("--- keygen ---\n");
    FLCryptoCtx ctx; fl_crypto_init(&ctx, NULL);
    CHECK(ctx.encrypt_enabled==1, "enabled after init");
    /* Public key must be nonzero */
    int nz=0; for(int i=0;i<32;i++) nz|=ctx.public_key[i];
    CHECK(nz!=0, "public key nonzero");
}

static void test_ecdh(void){
    printf("--- ecdh ---\n");
    FLCryptoCtx alice, bob;
    /* Seeds must differ in bytes not clamped (byte[0]&=248 zeroes low 3 bits) */
    uint8_t sa[32]={0x48,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
                    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                    0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x40};
    uint8_t sb[32]={0x50,0x22,0x33,0x44,0x55,0x66,0x77,0x08,
                    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
                    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                    0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x60};
    fl_crypto_init(&alice,sa); fl_crypto_init(&bob,sb);
    int ra=fl_crypto_handshake(&alice,bob.public_key);
    int rb=fl_crypto_handshake(&bob,alice.public_key);
    CHECK(ra==FL_OK&&rb==FL_OK,"handshake returns FL_OK");
    CHECK(memcmp(alice.session_key,bob.session_key,32)==0,"shared keys match");
    int nz=0; for(int i=0;i<32;i++) nz|=alice.session_key[i];
    CHECK(nz!=0,"session key nonzero");
}

static void test_encrypt_decrypt(void){
    printf("--- encrypt_decrypt ---\n");
    FLCryptoCtx alice, bob;
    uint8_t sa[32]={0x48,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                    0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,
                    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
                    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x40};
    uint8_t sb[32]={0x50,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,
                    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
                    0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x60};
    fl_crypto_init(&alice,sa); fl_crypto_init(&bob,sb);
    fl_crypto_handshake(&alice,bob.public_key);
    fl_crypto_handshake(&bob,alice.public_key);

    uint8_t msg[32]="Hello Filum LoRa gradient!";
    uint8_t ciphertext[32]; memcpy(ciphertext,msg,32);
    uint8_t nonce[12],tag[16];
    fl_crypto_make_nonce(nonce,1,0,0x0042);
    fl_crypto_encrypt(&alice,ciphertext,26,nonce,tag);
    CHECK(memcmp(ciphertext,msg,26)!=0,"encrypted differs from plaintext");

    int ret=fl_crypto_decrypt(&bob,ciphertext,26,nonce,tag);
    CHECK(ret==FL_OK,"decrypt returns FL_OK");
    CHECK(memcmp(ciphertext,msg,26)==0,"decrypted matches original");
}

static void test_tamper_detect(void){
    printf("--- tamper_detect ---\n");
    FLCryptoCtx alice,bob;
    uint8_t sa[32]={0x58,0x33,0x44,0x55,0x66,0x77,0x88,0x99,
                    0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11,
                    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
                    0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x40};
    uint8_t sb[32]={0x60,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,
                    0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11,0x22,
                    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
                    0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x60};
    fl_crypto_init(&alice,sa); fl_crypto_init(&bob,sb);
    fl_crypto_handshake(&alice,bob.public_key);
    fl_crypto_handshake(&bob,alice.public_key);

    uint8_t buf[16]="GradientData!!!";
    uint8_t nonce[12],tag[16];
    fl_crypto_make_nonce(nonce,2,1,0x0001);
    fl_crypto_encrypt(&alice,buf,15,nonce,tag);

    buf[0]^=0xFF;  /* tamper with ciphertext */
    int ret=fl_crypto_decrypt(&bob,buf,15,nonce,tag);
    CHECK(ret==FL_ERR_AUTH,"tampered frame rejected");
}

static void test_nonce_unique(void){
    printf("--- nonce_unique ---\n");
    uint8_t n1[12],n2[12];
    fl_crypto_make_nonce(n1,1,0,0x0001);
    fl_crypto_make_nonce(n2,2,0,0x0001);  /* different round */
    CHECK(memcmp(n1,n2,12)!=0,"different rounds -> different nonces");
    fl_crypto_make_nonce(n2,1,0,0x0002);  /* different shard */
    CHECK(memcmp(n1,n2,12)!=0,"different shards -> different nonces");
}

int main(void){
    printf("=== test_crypto ===\n");
    test_keygen();
    test_ecdh();
    test_encrypt_decrypt();
    test_tamper_detect();
    test_nonce_unique();
    printf("\n%d/%d passed\n",P,R);
    return P==R?0:1;
}
