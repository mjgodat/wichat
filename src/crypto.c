/* crypto.c — WiChat AES-CCM encryption
 *
 * Uses tinycrypt AES-128-CCM.  The 32-byte key derived from the passphrase
 * is stored in crypto_ctx_t; the first 16 bytes are used as the AES-128
 * key for CCM operations (tinycrypt only supports AES-128).
 */

#include "crypto.h"
#include "config.h"
#include "tinycrypt/aes.h"
#include "tinycrypt/ccm_mode.h"
#include "tinycrypt/constants.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Key derivation: zero-pad passphrase into 32 bytes, then iterate   */
/*  1000x AES-128-encrypting each half with the block as key.         */
/* ------------------------------------------------------------------ */
static void derive_key(const char *passphrase, uint8_t out[WICHAT_KEY_LEN])
{
    struct tc_aes_key_sched_struct sched;
    uint8_t block[WICHAT_KEY_LEN];
    uint8_t tmp[TC_AES_BLOCK_SIZE];
    int i;

    /* Zero-pad passphrase into 32-byte block */
    memset(block, 0, sizeof(block));
    if (passphrase) {
        size_t plen = strlen(passphrase);
        if (plen > WICHAT_KEY_LEN) plen = WICHAT_KEY_LEN;
        memcpy(block, passphrase, plen);
    }

    /* Iterate 1000 times: AES-128-encrypt each half with block as key */
    for (i = 0; i < 1000; i++) {
        /* Use first 16 bytes of block as AES key */
        tc_aes128_set_encrypt_key(&sched, block);

        /* Encrypt block[0:16] in-place */
        tc_aes_encrypt(tmp, block, &sched);
        memcpy(block, tmp, TC_AES_BLOCK_SIZE);

        /* Encrypt block[16:32] in-place */
        tc_aes_encrypt(tmp, block + TC_AES_BLOCK_SIZE, &sched);
        memcpy(block + TC_AES_BLOCK_SIZE, tmp, TC_AES_BLOCK_SIZE);
    }

    memcpy(out, block, WICHAT_KEY_LEN);

    /* Scrub temporaries */
    memset(block, 0, sizeof(block));
    memset(tmp, 0, sizeof(tmp));
    memset(&sched, 0, sizeof(sched));
}

/* ------------------------------------------------------------------ */
/*  Build 13-byte CCM nonce from msg_id + sender MAC                  */
/* ------------------------------------------------------------------ */
static void build_nonce(uint8_t nonce[WICHAT_NONCE_LEN],
                        uint32_t msg_id, const uint8_t *sender)
{
    memset(nonce, 0, WICHAT_NONCE_LEN);

    /* bytes 0-3: msg_id in little-endian */
    nonce[0] = (uint8_t)(msg_id      );
    nonce[1] = (uint8_t)(msg_id >>  8);
    nonce[2] = (uint8_t)(msg_id >> 16);
    nonce[3] = (uint8_t)(msg_id >> 24);

    /* bytes 4-9: sender MAC (6 bytes) */
    if (sender)
        memcpy(nonce + 4, sender, 6);

    /* bytes 10-12: zeros (already zeroed) */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int crypto_init(crypto_ctx_t *ctx, const char *passphrase)
{
    if (!ctx) return 0;

    if (!passphrase || passphrase[0] == '\0') {
        memset(ctx->key, 0, WICHAT_KEY_LEN);
        ctx->enabled = 0;
        return 0;
    }

    derive_key(passphrase, ctx->key);
    ctx->enabled = 1;
    return 0;
}

void crypto_disable(crypto_ctx_t *ctx)
{
    if (!ctx) return;
    memset(ctx->key, 0, WICHAT_KEY_LEN);
    ctx->enabled = 0;
}

int crypto_encrypt(crypto_ctx_t *ctx, uint8_t *data, size_t len,
                   uint32_t msg_id, const uint8_t *sender)
{
    struct tc_aes_key_sched_struct sched;
    struct tc_ccm_mode_struct     ccm;
    uint8_t nonce[WICHAT_NONCE_LEN];
    uint8_t tmp[WICHAT_MAX_PAYLOAD + WICHAT_TAG_LEN];
    int     rc;

    if (!ctx || !data) return -1;
    if (!ctx->enabled)  return (int)len;
    if (len == 0)       return -1;
    if (len + WICHAT_TAG_LEN > sizeof(tmp)) return -1;

    /* Set up AES key (first 16 bytes of derived key) */
    tc_aes128_set_encrypt_key(&sched, ctx->key);

    /* Build nonce */
    build_nonce(nonce, msg_id, sender);

    /* Configure CCM: nonce length=13, mac length=8 */
    rc = tc_ccm_config(&ccm, &sched, nonce, WICHAT_NONCE_LEN, WICHAT_TAG_LEN);
    if (rc != TC_CRYPTO_SUCCESS) return -1;

    /* Encrypt: no associated data, payload = data[0..len) */
    rc = tc_ccm_generation_encryption(
        tmp,                         /* out */
        (unsigned int)(len + WICHAT_TAG_LEN),  /* olen */
        NULL,                        /* associated_data */
        0,                           /* alen */
        data,                        /* payload */
        (unsigned int)len,           /* plen */
        &ccm);

    if (rc != TC_CRYPTO_SUCCESS) return -1;

    /* Copy result back in-place */
    memcpy(data, tmp, len + WICHAT_TAG_LEN);

    /* Scrub temporaries */
    memset(tmp, 0, sizeof(tmp));
    memset(&sched, 0, sizeof(sched));

    return (int)(len + WICHAT_TAG_LEN);
}

int crypto_decrypt(crypto_ctx_t *ctx, uint8_t *data, size_t len,
                   uint32_t msg_id, const uint8_t *sender)
{
    struct tc_aes_key_sched_struct sched;
    struct tc_ccm_mode_struct     ccm;
    uint8_t nonce[WICHAT_NONCE_LEN];
    uint8_t tmp[WICHAT_MAX_PAYLOAD];
    int     rc;
    size_t  plain_len;

    if (!ctx || !data) return -1;
    if (!ctx->enabled)  return (int)len;
    if (len <= WICHAT_TAG_LEN) return -1;

    plain_len = len - WICHAT_TAG_LEN;
    if (plain_len > sizeof(tmp)) return -1;

    /* Set up AES key (first 16 bytes of derived key) */
    tc_aes128_set_encrypt_key(&sched, ctx->key);

    /* Build nonce */
    build_nonce(nonce, msg_id, sender);

    /* Configure CCM */
    rc = tc_ccm_config(&ccm, &sched, nonce, WICHAT_NONCE_LEN, WICHAT_TAG_LEN);
    if (rc != TC_CRYPTO_SUCCESS) return -1;

    /* Decrypt and verify: payload = ciphertext+tag, plen = len (cipher+tag) */
    rc = tc_ccm_decryption_verification(
        tmp,                         /* out (plaintext) */
        (unsigned int)plain_len,     /* olen */
        NULL,                        /* associated_data */
        0,                           /* alen */
        data,                        /* payload (ciphertext + tag) */
        (unsigned int)len,           /* plen (cipher + tag length) */
        &ccm);

    if (rc != TC_CRYPTO_SUCCESS) return -1;  /* auth failure */

    /* Copy plaintext back in-place */
    memcpy(data, tmp, plain_len);

    /* Scrub temporaries */
    memset(tmp, 0, sizeof(tmp));
    memset(&sched, 0, sizeof(sched));

    return (int)plain_len;
}
