#ifndef FILUM_FL_CRYPTO_H
#define FILUM_FL_CRYPTO_H

/*
 * filum/common/include/fl_crypto.h
 *
 * Lightweight cryptography for Filum gradient encryption.
 *
 * Algorithm choices (MCU-friendly, no external libraries):
 *
 *   Key exchange : Curve25519 ECDH
 *                  32-byte public/private keys, ~1M clock cycles on Cortex-M4
 *                  Both sides derive the same 32-byte shared secret.
 *
 *   Stream cipher: ChaCha20 (RFC 7539)
 *                  Pure 32-bit ARX, no lookup tables, constant-time.
 *                  Encrypts payload in-place. Nonce = round_id || frag_index.
 *
 *   MAC          : Poly1305 (RFC 7539)
 *                  Authenticates encrypted payload. Prevents tampering.
 *                  16-byte tag appended to each encrypted fragment.
 *
 * Key exchange flow:
 *   1. Shard generates ephemeral keypair, sends public key in FL_FRAME_HANDSHAKE
 *   2. Herald replies with its public key in FL_FRAME_HANDSHAKE_ACK
 *   3. Both sides run ECDH -> shared_secret
 *   4. shared_secret -> ChaCha20 session key via HKDF-SHA256 (simplified)
 *   5. All UPDATE and DELTA frames are encrypted from round 1 onward
 *
 * Memory cost per shard: 96 bytes (keys + session state)
 * CPU cost per frame:    ~5000 cycles ChaCha20 (negligible vs LoRa TX time)
 *
 * NOTE: This implements the cryptographic primitives from scratch in C99.
 * For production use, replace with a vetted library (libsodium, mbedTLS).
 * The interface is identical — swap the implementation, not the API.
 */

#include <stdint.h>
#include <stddef.h>
#include "fl_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Key sizes
 * ------------------------------------------------------------------------- */

#define FL_CRYPTO_KEY_SIZE     32   /* Curve25519 key size (bytes)     */
#define FL_CRYPTO_NONCE_SIZE   12   /* ChaCha20 nonce size (bytes)     */
#define FL_CRYPTO_TAG_SIZE     16   /* Poly1305 tag size (bytes)       */

/* -------------------------------------------------------------------------
 * Session state (one per shard, 96 bytes)
 * ------------------------------------------------------------------------- */

typedef struct {
    uint8_t  private_key[FL_CRYPTO_KEY_SIZE];   /* ephemeral private key    */
    uint8_t  public_key[FL_CRYPTO_KEY_SIZE];    /* ephemeral public key     */
    uint8_t  session_key[FL_CRYPTO_KEY_SIZE];   /* derived after ECDH       */
    uint8_t  handshake_done;                    /* 1 = keys exchanged       */
    uint8_t  encrypt_enabled;                   /* 0 = no encryption (debug)*/
} FLCryptoCtx;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/*
 * fl_crypto_init - generate ephemeral Curve25519 keypair.
 * seed: 32 bytes of entropy (from MCU RNG, UID XOR tick counter, etc.)
 *       Pass NULL to use a fixed test seed (debug only).
 */
/**
 * @brief Generate an ephemeral Curve25519 keypair.
 * @param seed 32 bytes of entropy. NULL uses a fixed test seed.
 */
void fl_crypto_init(FLCryptoCtx *ctx, const uint8_t *seed);

/*
 * fl_crypto_handshake - compute shared secret from peer's public key.
 * peer_pubkey: the 32-byte public key received from Herald/Shard.
 * After this call, ctx->session_key is ready and ctx->handshake_done = 1.
 */
/**
 * @brief Complete ECDH: compute shared secret from peer public key.
 * @return FL_OK or FL_ERR_AUTH if shared secret is all-zero (low-order point).
 */
FLError fl_crypto_handshake(FLCryptoCtx *ctx, const uint8_t *peer_pubkey);

/* -------------------------------------------------------------------------
 * Encryption / Decryption
 * ------------------------------------------------------------------------- */

/*
 * fl_crypto_encrypt - encrypt payload in-place and append Poly1305 tag.
 *
 * buf:      payload bytes (modified in-place)
 * len:      payload length
 * nonce:    FL_CRYPTO_NONCE_SIZE bytes (constructed from round_id + frag_index)
 * tag_out:  FL_CRYPTO_TAG_SIZE bytes written here (append to frame)
 */
void fl_crypto_encrypt(FLCryptoCtx *ctx,
                       uint8_t *buf, uint16_t len,
                       const uint8_t *nonce,
                       uint8_t *tag_out);

/*
 * fl_crypto_decrypt - verify Poly1305 tag then decrypt in-place.
 * Returns 0 if tag valid, -1 if authentication fails (discard frame).
 */
/**
 * @brief Verify Poly1305 MAC then decrypt payload in-place.
 * @return FL_OK, FL_ERR_AUTH if MAC fails, FL_ERR_NO_SESSION if no key.
 */
FLError fl_crypto_decrypt(FLCryptoCtx *ctx,
                          uint8_t *buf, uint16_t len,
                          const uint8_t *nonce,
                          const uint8_t *tag);

/*
 * fl_crypto_make_nonce - build a 12-byte nonce from round_id and frag_index.
 * nonce_out: FL_CRYPTO_NONCE_SIZE bytes
 */
void fl_crypto_make_nonce(uint8_t *nonce_out,
                          uint8_t round_id, uint8_t frag_index,
                          uint16_t shard_id);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_CRYPTO_H */
