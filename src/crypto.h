/* crypto.h — WiChat AES-CCM encryption */
#ifndef WICHAT_CRYPTO_H
#define WICHAT_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define WICHAT_KEY_LEN    32   /* AES-256 key bytes (derived from passphrase) */
#define WICHAT_NONCE_LEN  13   /* CCM nonce */
#define WICHAT_TAG_LEN    8    /* CCM auth tag */

typedef struct {
    uint8_t key[WICHAT_KEY_LEN];
    int     enabled;
} crypto_ctx_t;

/*  Derive key from passphrase and enable encryption.
 *  If passphrase is NULL or empty, disable encryption.
 *  Returns 0 always. */
int  crypto_init(crypto_ctx_t *ctx, const char *passphrase);

/*  Zero key material and disable encryption. */
void crypto_disable(crypto_ctx_t *ctx);

/*  Encrypt data in-place using AES-CCM.
 *  Returns new length (len + WICHAT_TAG_LEN), or -1 on error.
 *  If encryption not enabled, returns len unchanged. */
int  crypto_encrypt(crypto_ctx_t *ctx, uint8_t *data, size_t len,
                    uint32_t msg_id, const uint8_t *sender);

/*  Decrypt data in-place using AES-CCM.
 *  Returns new length (len - WICHAT_TAG_LEN), or -1 on auth failure.
 *  If encryption not enabled, returns len unchanged. */
int  crypto_decrypt(crypto_ctx_t *ctx, uint8_t *data, size_t len,
                    uint32_t msg_id, const uint8_t *sender);

#endif /* WICHAT_CRYPTO_H */
