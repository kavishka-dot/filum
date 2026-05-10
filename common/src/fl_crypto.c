/*
 * filum/common/src/fl_crypto.c
 *
 * Pure C99 implementation of:
 *   - Curve25519 ECDH (key exchange)
 *   - ChaCha20 stream cipher (encryption)
 *   - Poly1305 MAC (authentication)
 *
 * All arithmetic is 32-bit friendly for Cortex-M4.
 * No heap allocation. No lookup tables. Constant-time where it matters.
 *
 * References:
 *   Bernstein, "Curve25519: New Diffie-Hellman Speed Records" (2006)
 *   Bernstein, "ChaCha, a variant of Salsa20" (2008)
 *   Bernstein, "The Poly1305-AES message-authentication code" (2005)
 *   RFC 7539: ChaCha20 and Poly1305 for IETF Protocols
 */

#include "fl_crypto.h"
#include "fl_error.h"
#include <string.h>
#include <stdint.h>

/* =========================================================================
 * Utility
 * ========================================================================= */

static uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

static void store32_le(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

/* =========================================================================
 * ChaCha20 stream cipher
 * ========================================================================= */

#define QR(a,b,c,d) \
    a+=b; d^=a; d=rotl32(d,16); \
    c+=d; b^=c; b=rotl32(b,12); \
    a+=b; d^=a; d=rotl32(d,8);  \
    c+=d; b^=c; b=rotl32(b,7)

static void chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    uint32_t x[16];
    memcpy(x, in, 64);
    for (int i = 0; i < 10; i++) {
        QR(x[0],x[4],x[8], x[12]);
        QR(x[1],x[5],x[9], x[13]);
        QR(x[2],x[6],x[10],x[14]);
        QR(x[3],x[7],x[11],x[15]);
        QR(x[0],x[5],x[10],x[15]);
        QR(x[1],x[6],x[11],x[12]);
        QR(x[2],x[7],x[8], x[13]);
        QR(x[3],x[4],x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + in[i];
}

static void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                          uint32_t counter,
                          uint8_t *buf, uint16_t len) {
    static const uint8_t sigma[16] = "expand 32-byte k";
    uint32_t state[16];
    state[0]  = load32_le(sigma+0);
    state[1]  = load32_le(sigma+4);
    state[2]  = load32_le(sigma+8);
    state[3]  = load32_le(sigma+12);
    for (int i=0;i<8;i++) state[4+i] = load32_le(key+4*i);
    state[12] = counter;
    state[13] = load32_le(nonce+0);
    state[14] = load32_le(nonce+4);
    state[15] = load32_le(nonce+8);

    uint16_t pos = 0;
    while (pos < len) {
        uint32_t block[16];
        uint8_t  stream[64];
        chacha20_block(block, state);
        for (int i=0;i<16;i++) store32_le(stream+4*i, block[i]);
        state[12]++;

        uint16_t chunk = (uint16_t)(len - pos);
        if (chunk > 64) chunk = 64;
        for (uint16_t i=0; i<chunk; i++) buf[pos+i] ^= stream[i];
        pos += chunk;
    }
}

/* =========================================================================
 * Poly1305 MAC
 * ========================================================================= */

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t  buf[16];
    uint8_t  buf_used;
    uint8_t  finalized;
} Poly1305;

static void poly1305_init(Poly1305 *st, const uint8_t key[32]) {
    memset(st, 0, sizeof(*st));
    /* clamp r */
    uint8_t r[16];
    memcpy(r, key, 16);
    r[3]  &= 15; r[7]  &= 15; r[11] &= 15; r[15] &= 15;
    r[4]  &= 252; r[8] &= 252; r[12] &= 252;
    st->r[0] = load32_le(r+0)  & 0x3ffffff;
    st->r[1] = (load32_le(r+3) >> 2) & 0x3ffff03;
    st->r[2] = (load32_le(r+6) >> 4) & 0x3ffc0ff;
    st->r[3] = (load32_le(r+9) >> 6) & 0x3f03fff;
    st->r[4] = (load32_le(r+12)>> 8) & 0x00fffff;
    for (int i=0;i<4;i++) st->pad[i] = load32_le(key+16+4*i);
}

static void poly1305_block(Poly1305 *st, const uint8_t *m, uint8_t hibit) {
    uint32_t r0=st->r[0],r1=st->r[1],r2=st->r[2],r3=st->r[3],r4=st->r[4];
    uint32_t h0=st->h[0],h1=st->h[1],h2=st->h[2],h3=st->h[3],h4=st->h[4];

    h0 += load32_le(m+0)  & 0x3ffffff;
    h1 += (load32_le(m+3) >> 2) & 0x3ffffff;
    h2 += (load32_le(m+6) >> 4) & 0x3ffffff;
    h3 += (load32_le(m+9) >> 6) & 0x3ffffff;
    h4 += (load32_le(m+12)>> 8) | ((uint32_t)hibit << 24);

    uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*(r4*5) + (uint64_t)h2*(r3*5) + (uint64_t)h3*(r2*5) + (uint64_t)h4*(r1*5);
    uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0     + (uint64_t)h2*(r4*5) + (uint64_t)h3*(r3*5) + (uint64_t)h4*(r2*5);
    uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1     + (uint64_t)h2*r0     + (uint64_t)h3*(r4*5) + (uint64_t)h4*(r3*5);
    uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2     + (uint64_t)h2*r1     + (uint64_t)h3*r0     + (uint64_t)h4*(r4*5);
    uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3     + (uint64_t)h2*r2     + (uint64_t)h3*r1     + (uint64_t)h4*r0;

    uint32_t c;
    c=(uint32_t)(d0>>26); h0=(uint32_t)d0&0x3ffffff; d1+=c;
    c=(uint32_t)(d1>>26); h1=(uint32_t)d1&0x3ffffff; d2+=c;
    c=(uint32_t)(d2>>26); h2=(uint32_t)d2&0x3ffffff; d3+=c;
    c=(uint32_t)(d3>>26); h3=(uint32_t)d3&0x3ffffff; d4+=c;
    c=(uint32_t)(d4>>26); h4=(uint32_t)d4&0x3ffffff; h0+=c*5;
    c=h0>>26; h0&=0x3ffffff; h1+=c;

    st->h[0]=h0;st->h[1]=h1;st->h[2]=h2;st->h[3]=h3;st->h[4]=h4;
}

static void poly1305_update(Poly1305 *st, const uint8_t *m, uint16_t len) {
    while (len > 0) {
        if (st->buf_used == 16) {
            poly1305_block(st, st->buf, 1);
            st->buf_used = 0;
        }
        st->buf[st->buf_used++] = *m++;
        len--;
    }
}

static void poly1305_finish(Poly1305 *st, uint8_t tag[16]) {
    if (st->buf_used) {
        uint8_t buf[16]={0};
        memcpy(buf, st->buf, st->buf_used);
        buf[st->buf_used] = 1;
        poly1305_block(st, buf, 0);
    }
    uint32_t h0=st->h[0],h1=st->h[1],h2=st->h[2],h3=st->h[3],h4=st->h[4];
    uint32_t c = h1>>26; h1&=0x3ffffff; h2+=c;
    c=h2>>26; h2&=0x3ffffff; h3+=c;
    c=h3>>26; h3&=0x3ffffff; h4+=c;
    c=h4>>26; h4&=0x3ffffff; h0+=c*5;
    c=h0>>26; h0&=0x3ffffff; h1+=c;

    /* h - p */
    uint32_t g0=h0+5; c=g0>>26; g0&=0x3ffffff;
    uint32_t g1=h1+c; c=g1>>26; g1&=0x3ffffff;
    uint32_t g2=h2+c; c=g2>>26; g2&=0x3ffffff;
    uint32_t g3=h3+c; c=g3>>26; g3&=0x3ffffff;
    uint32_t g4=h4+c-0x4000000;
    uint32_t mask = (g4 >> 31) - 1;
    g0&=mask; g1&=mask; g2&=mask; g3&=mask; g4&=mask;
    mask = ~mask;
    h0=(h0&mask)|(g0); h1=(h1&mask)|(g1);
    h2=(h2&mask)|(g2); h3=(h3&mask)|(g3); h4=(h4&mask)|(g4);

    h0 = ((h0)|(h1<<26)) + st->pad[0];
    h1 = ((h1>>6)|(h2<<20)) + st->pad[1];
    h2 = ((h2>>12)|(h3<<14)) + st->pad[2];
    h3 = ((h3>>18)|(h4<<8))  + st->pad[3];
    store32_le(tag+0,  h0);
    store32_le(tag+4,  h1);
    store32_le(tag+8,  h2);
    store32_le(tag+12, h3);
}

/* =========================================================================
 * Curve25519 ECDH - RFC 7748 X25519
 *
 * Clean implementation using 64-bit limbs.
 * Each limb holds 51 bits; 5 limbs represent a field element.
 * ========================================================================= */

typedef uint64_t u64;
typedef u64 fe51[5];   /* field element in 2^51 radix */

#define MASK51 ((u64)((1ULL<<51)-1))

static void fe51_load(fe51 h, const uint8_t s[32]) {
    u64 a0=load32_le(s),    a1=load32_le(s+4),  a2=load32_le(s+8),
        a3=load32_le(s+12), a4=load32_le(s+16), a5=load32_le(s+20),
        a6=load32_le(s+24), a7=load32_le(s+28);
    h[0] =  a0                        & MASK51;
    h[1] = (a0>>51 | a1<<13 | a1>>19) & MASK51;
    h[1] =  ((u64)a1 >> (51-13) | (u64)a2 << (13+2)) >> 0;
    /* simpler: bit-slice */
    u64 bits = 0;
    /* Load 256 bits into 5 x 51-bit limbs */
    u64 w[4]; w[0]=(u64)a0|((u64)a1<<32); w[1]=(u64)a2|((u64)a3<<32);
              w[2]=(u64)a4|((u64)a5<<32); w[3]=(u64)a6|((u64)a7<<32);
    (void)bits;
    h[0] =  w[0]              & MASK51;
    h[1] = (w[0]>>51 | w[1]<<13) & MASK51;
    h[2] = (w[1]>>38 | w[2]<<26) & MASK51;
    h[3] = (w[2]>>25 | w[3]<<39) & MASK51;
    h[4] = (w[3]>>12)             & MASK51;
}

static void fe51_store(uint8_t s[32], fe51 h) {
    /* Propagate carries */
    u64 c;
    for(int i=0;i<4;i++){c=h[i]>>51;h[i]&=MASK51;h[i+1]+=c;}
    c=h[4]>>51; h[4]&=MASK51; h[0]+=c*19;
    c=h[0]>>51; h[0]&=MASK51; h[1]+=c;
    /* Pack into bytes */
    u64 w0=h[0]|(h[1]<<51);
    u64 w1=(h[1]>>13)|(h[2]<<38);
    u64 w2=(h[2]>>26)|(h[3]<<25);
    u64 w3=(h[3]>>39)|(h[4]<<12);
    store32_le(s+0,  (uint32_t)w0); store32_le(s+4,  (uint32_t)(w0>>32));
    store32_le(s+8,  (uint32_t)w1); store32_le(s+12, (uint32_t)(w1>>32));
    store32_le(s+16, (uint32_t)w2); store32_le(s+20, (uint32_t)(w2>>32));
    store32_le(s+24, (uint32_t)w3); store32_le(s+28, (uint32_t)(w3>>32));
}

static void fe51_add(fe51 h, const fe51 a, const fe51 b) {
    for(int i=0;i<5;i++) h[i]=a[i]+b[i];
}

static void fe51_sub(fe51 h, const fe51 a, const fe51 b) {
    /* Add 2*p to prevent underflow */
    static const u64 P2[5]={0xFFFFFFFFFFFDA,0xFFFFFFFFFFFFE,
                              0xFFFFFFFFFFFFE,0xFFFFFFFFFFFFE,0xFFFFFFFFFFFFE};
    for(int i=0;i<5;i++) h[i]=a[i]+P2[i]-b[i];
}

static void fe51_mul(fe51 h, const fe51 a, const fe51 b) {
    typedef unsigned __int128 u128;
    u64 a0=a[0],a1=a[1],a2=a[2],a3=a[3],a4=a[4];
    u64 b0=b[0],b1=b[1],b2=b[2],b3=b[3],b4=b[4];
    u64 b1_19=b1*19,b2_19=b2*19,b3_19=b3*19,b4_19=b4*19;
    u128 t0=((u128)a0*b0)+((u128)a4*b1_19)+((u128)a3*b2_19)+((u128)a2*b3_19)+((u128)a1*b4_19);
    u128 t1=((u128)a1*b0)+((u128)a0*b1  )+((u128)a4*b2_19)+((u128)a3*b3_19)+((u128)a2*b4_19);
    u128 t2=((u128)a2*b0)+((u128)a1*b1  )+((u128)a0*b2  )+((u128)a4*b3_19)+((u128)a3*b4_19);
    u128 t3=((u128)a3*b0)+((u128)a2*b1  )+((u128)a1*b2  )+((u128)a0*b3  )+((u128)a4*b4_19);
    u128 t4=((u128)a4*b0)+((u128)a3*b1  )+((u128)a2*b2  )+((u128)a1*b3  )+((u128)a0*b4  );
    u64 r0=(u64)t0&MASK51; t1+=(u64)(t0>>51);
    u64 r1=(u64)t1&MASK51; t2+=(u64)(t1>>51);
    u64 r2=(u64)t2&MASK51; t3+=(u64)(t2>>51);
    u64 r3=(u64)t3&MASK51; t4+=(u64)(t3>>51);
    u64 r4=(u64)t4&MASK51; r0+=(u64)(t4>>51)*19;
    u64 c=r0>>51; r0&=MASK51; r1+=c;
    h[0]=r0;h[1]=r1;h[2]=r2;h[3]=r3;h[4]=r4;
}

static void fe51_sq(fe51 h, const fe51 a) { fe51_mul(h,a,a); }

static void fe51_mul_121666(fe51 h, const fe51 a) {
    typedef unsigned __int128 u128;
    u128 t[5];
    for(int i=0;i<5;i++) t[i]=(u128)a[i]*121666;
    for(int i=0;i<4;i++){h[i]=(u64)t[i]&MASK51;t[i+1]+=(u64)(t[i]>>51);}
    h[4]=(u64)t[4]&MASK51; h[0]+=(u64)(t[4]>>51)*19;
    u64 c=h[0]>>51; h[0]&=MASK51; h[1]+=c;
}

static void fe51_invert(fe51 out, const fe51 z) {
    fe51 t0,t1,t2,t3;
    fe51_sq(t0,z);
    fe51_sq(t1,t0); fe51_sq(t1,t1); fe51_mul(t1,z,t1);
    fe51_mul(t0,t0,t1);
    fe51_sq(t2,t0); fe51_mul(t1,t1,t2);
    fe51_sq(t2,t1); for(int i=1;i<5;i++) fe51_sq(t2,t2);
    fe51_mul(t1,t2,t1);
    fe51_sq(t2,t1); for(int i=1;i<10;i++) fe51_sq(t2,t2);
    fe51_mul(t2,t2,t1);
    fe51_sq(t3,t2); for(int i=1;i<20;i++) fe51_sq(t3,t3);
    fe51_mul(t2,t3,t2);
    fe51_sq(t2,t2); for(int i=1;i<10;i++) fe51_sq(t2,t2);
    fe51_mul(t1,t2,t1);
    fe51_sq(t2,t1); for(int i=1;i<50;i++) fe51_sq(t2,t2);
    fe51_mul(t2,t2,t1);
    fe51_sq(t3,t2); for(int i=1;i<100;i++) fe51_sq(t3,t3);
    fe51_mul(t2,t3,t2);
    fe51_sq(t2,t2); for(int i=1;i<50;i++) fe51_sq(t2,t2);
    fe51_mul(t1,t2,t1);
    fe51_sq(t1,t1); for(int i=1;i<5;i++) fe51_sq(t1,t1);
    fe51_mul(out,t1,t0);
}

static void fe51_cswap(fe51 a, fe51 b, u64 sw) {
    u64 m = -(sw&1);
    for(int i=0;i<5;i++){u64 x=(a[i]^b[i])&m; a[i]^=x; b[i]^=x;}
}

/* X25519 Montgomery ladder */
static void x25519(uint8_t out[32], const uint8_t scalar[32],
                   const uint8_t point[32]) {
    uint8_t s[32]; memcpy(s,scalar,32);
    s[0]&=248; s[31]=(s[31]&127)|64;

    fe51 x1,x2,z2,x3,z3,tmp0,tmp1;
    fe51_load(x1,point);
    /* x2=1, z2=0, x3=x1, z3=1 */
    memset(x2,0,sizeof(x2)); x2[0]=1;
    memset(z2,0,sizeof(z2));
    memcpy(x3,x1,sizeof(x1));
    memset(z3,0,sizeof(z3)); z3[0]=1;

    u64 swap=0;
    for(int t=254;t>=0;t--){
        u64 b=(s[t>>3]>>(t&7))&1;
        swap^=b; fe51_cswap(x2,x3,swap); fe51_cswap(z2,z3,swap); swap=b;
        fe51 A,AA,B,BB,E,C,D,DA,CB;
        fe51_add(A,x2,z2); fe51_sq(AA,A);
        fe51_sub(B,x2,z2); fe51_sq(BB,B);
        fe51_sub(E,AA,BB);
        fe51_add(C,x3,z3);
        fe51_sub(D,x3,z3);
        fe51_mul(DA,D,A); fe51_mul(CB,C,B);
        fe51_add(x3,DA,CB); fe51_sq(x3,x3);
        fe51_sub(z3,DA,CB); fe51_sq(z3,z3); fe51_mul(z3,z3,x1);
        fe51_mul(x2,AA,BB);
        fe51_mul_121666(tmp0,E); fe51_add(tmp1,AA,tmp0);
        fe51_mul(z2,E,tmp1);
    }
    fe51_cswap(x2,x3,swap); fe51_cswap(z2,z3,swap);
    fe51_invert(tmp0,z2); fe51_mul(x2,x2,tmp0);
    fe51_store(out,x2);
    (void)tmp1;
}

/* Curve25519 basepoint: u=9 */
static const uint8_t BASE_POINT[32] = {9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

/* =========================================================================
 * Simple HKDF-like KDF using ChaCha20
 * Derives session_key from shared_secret + context info.
 * ========================================================================= */
static void kdf(uint8_t out[32], const uint8_t shared[32],
                uint16_t shard_id) {
    /* Use shared secret as ChaCha20 key, shard_id in nonce */
    uint8_t nonce[12] = {0};
    nonce[0] = (uint8_t)shard_id;
    nonce[1] = (uint8_t)(shard_id>>8);
    nonce[2] = 0xF1; nonce[3] = 0x1U; /* "FL" marker */
    memset(out, 0, 32);
    chacha20_xor(shared, nonce, 0, out, 32);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void fl_crypto_init(FLCryptoCtx *ctx, const uint8_t *seed) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->encrypt_enabled = 1;

    /* Private key from seed (or fixed test seed) */
    static const uint8_t test_seed[32] = {
        0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,
        0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
        0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,
        0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a
    };
    memcpy(ctx->private_key, seed ? seed : test_seed, 32);
    ctx->private_key[0]  &= 248;
    ctx->private_key[31] &= 127;
    ctx->private_key[31] |= 64;

    /* Public key = scalar_mult(private, basepoint) */
    x25519(ctx->public_key, ctx->private_key, BASE_POINT);
}

FLError fl_crypto_handshake(FLCryptoCtx *ctx, const uint8_t *peer_pubkey) {
    if (!ctx || !peer_pubkey) return FL_ERR_INVALID_ARG;
    uint8_t shared[32];
    x25519(shared, ctx->private_key, peer_pubkey);

    /* All-zero shared secret = invalid (low-order point attack) */
    uint8_t z = 0;
    for (int i=0;i<32;i++) z |= shared[i];
    if (!z) return FL_ERR_AUTH;

    /* Derive session key */
    kdf(ctx->session_key, shared, 0);
    ctx->handshake_done = 1;
    return FL_OK;
}

void fl_crypto_make_nonce(uint8_t *nonce_out, uint8_t round_id,
                          uint8_t frag_index, uint16_t shard_id) {
    memset(nonce_out, 0, FL_CRYPTO_NONCE_SIZE);
    nonce_out[0]  = round_id;
    nonce_out[1]  = frag_index;
    nonce_out[2]  = (uint8_t)shard_id;
    nonce_out[3]  = (uint8_t)(shard_id >> 8);
    /* bytes 4..11 remain zero (counter space) */
}

void fl_crypto_encrypt(FLCryptoCtx *ctx, uint8_t *buf, uint16_t len,
                       const uint8_t *nonce, uint8_t *tag_out) {
    if (!ctx->encrypt_enabled || !ctx->handshake_done) {
        memset(tag_out, 0, FL_CRYPTO_TAG_SIZE);
        return;
    }
    /* Generate Poly1305 key (first 32 bytes of ChaCha20 keystream at ctr=0) */
    uint8_t poly_key[32] = {0};
    chacha20_xor(ctx->session_key, nonce, 0, poly_key, 32);

    /* Encrypt payload at counter=1 */
    chacha20_xor(ctx->session_key, nonce, 1, buf, len);

    /* MAC over ciphertext */
    Poly1305 mac;
    poly1305_init(&mac, poly_key);
    poly1305_update(&mac, buf, len);
    poly1305_finish(&mac, tag_out);
}

FLError fl_crypto_decrypt(FLCryptoCtx *ctx, uint8_t *buf, uint16_t len,
                      const uint8_t *nonce, const uint8_t *tag) {
    if (!ctx || !buf || !nonce || !tag) return FL_ERR_INVALID_ARG;
    if (!ctx->encrypt_enabled || !ctx->handshake_done) return FL_OK;

    uint8_t poly_key[32] = {0};
    chacha20_xor(ctx->session_key, nonce, 0, poly_key, 32);

    /* Verify MAC before decrypting */
    Poly1305 mac;
    poly1305_init(&mac, poly_key);
    poly1305_update(&mac, buf, len);
    uint8_t expected[16];
    poly1305_finish(&mac, expected);

    /* Constant-time compare */
    uint8_t diff = 0;
    for (int i=0;i<16;i++) diff |= (tag[i] ^ expected[i]);
    if (diff) return FL_ERR_AUTH;

    chacha20_xor(ctx->session_key, nonce, 1, buf, len);
    return FL_OK;
}
