#ifndef DISPLAYJET_CRYPTO_H
#define DISPLAYJET_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define DJ_AES256_KEY_SIZE   32
#define DJ_CHACHA20_KEY_SIZE 32
#define DJ_CHACHA20_NONCE_SIZE 12
#define DJ_CHACHA20_BLOCK_SIZE 64
#define DJ_SHA256_DIGEST_SIZE 32
#define DJ_SHA256_BLOCK_SIZE  64
#define DJ_EPHEMERAL_KEY_SIZE 32

void dj_chacha20_init(uint32_t state[16], const uint8_t key[32],
                       uint32_t counter, const uint8_t nonce[12]);
void dj_chacha20_block(const uint32_t state[16], uint8_t output[64]);
void dj_chacha20_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                          uint32_t counter, const uint8_t *in,
                          uint8_t *out, size_t len);

void dj_sha256_init(uint32_t state[8]);
void dj_sha256_update(uint32_t state[8], uint8_t buf[64],
                       uint64_t *bitlen, const uint8_t *data, size_t len);
void dj_sha256_final(uint32_t state[8], uint8_t buf[64],
                      uint64_t bitlen, uint8_t out[32]);
void dj_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

void dj_hkdf(const uint8_t *salt, size_t salt_len,
              const uint8_t *ikm, size_t ikm_len,
              const uint8_t *info, size_t info_len,
              uint8_t *out, size_t out_len);

void dj_secure_zero(void *buf, size_t len);

#endif
