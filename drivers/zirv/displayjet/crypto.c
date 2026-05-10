#include "crypto.h"
#include <string.h>

/* ── ChaCha20 ─────────────────────────────────────────────────────────────── */
#define DJ_LE32(p) ((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | \
                    ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24))
#define DJ_LE32ENC(p, v) do { \
    (p)[0] = (uint8_t)(v); (p)[1] = (uint8_t)((v) >> 8); \
    (p)[2] = (uint8_t)((v) >> 16); (p)[3] = (uint8_t)((v) >> 24); \
} while (0)

#define DJ_ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b; *d ^= *a; *d = DJ_ROTL32(*d, 16);
    *c += *d; *b ^= *c; *b = DJ_ROTL32(*b, 12);
    *a += *b; *d ^= *a; *d = DJ_ROTL32(*d, 8);
    *c += *d; *b ^= *c; *b = DJ_ROTL32(*b, 7);
}

void dj_chacha20_init(uint32_t state[16], const uint8_t key[32],
                       uint32_t counter, const uint8_t nonce[12])
{
    state[0]  = 0x61707865;
    state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;
    state[3]  = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state[4 + i] = DJ_LE32(key + i * 4);
    state[12] = counter;
    state[13] = DJ_LE32(nonce);
    state[14] = DJ_LE32(nonce + 4);
    state[15] = DJ_LE32(nonce + 8);
}

void dj_chacha20_block(const uint32_t state[16], uint8_t output[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = state[i];

    for (int i = 0; i < 10; i++) {
        quarter_round(&x[0], &x[4], &x[8],  &x[12]);
        quarter_round(&x[1], &x[5], &x[9],  &x[13]);
        quarter_round(&x[2], &x[6], &x[10], &x[14]);
        quarter_round(&x[3], &x[7], &x[11], &x[15]);
        quarter_round(&x[0], &x[5], &x[10], &x[15]);
        quarter_round(&x[1], &x[6], &x[11], &x[12]);
        quarter_round(&x[2], &x[7], &x[8],  &x[13]);
        quarter_round(&x[3], &x[4], &x[9],  &x[14]);
    }
    for (int i = 0; i < 16; i++)
        DJ_LE32ENC(output + i * 4, x[i] + state[i]);
}

void dj_chacha20_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                          uint32_t counter, const uint8_t *in,
                          uint8_t *out, size_t len)
{
    uint32_t state[16];
    uint8_t  block[DJ_CHACHA20_BLOCK_SIZE];

    for (size_t off = 0; off < len; off += DJ_CHACHA20_BLOCK_SIZE) {
        dj_chacha20_init(state, key, counter++, nonce);
        dj_chacha20_block(state, block);
        size_t chunk = len - off;
        if (chunk > DJ_CHACHA20_BLOCK_SIZE) chunk = DJ_CHACHA20_BLOCK_SIZE;
        for (size_t i = 0; i < chunk; i++)
            out[off + i] = in[off + i] ^ block[i];
    }
    dj_secure_zero(block, sizeof(block));
    dj_secure_zero(state, sizeof(state));
}

/* ── SHA-256 ──────────────────────────────────────────────────────────────── */
#define ROTR32(v, n) (((v) >> (n)) | ((v) << (32 - (n))))
#define CH(x,y,z)    (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z)   (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)       (ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define EP1(x)       (ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define SIG0(x)      (ROTR32(x, 7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define SIG1(x)      (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void dj_sha256_init(uint32_t state[8])
{
    state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
    state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
    state[4] = 0x510e527f; state[5] = 0x9b05688c;
    state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
}

static void transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t W[64], a, b, c, d, e, f, g, h, T1, T2;
    for (int i = 0; i < 16; i++)
        W[i] = DJ_LE32(block + i * 4);
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 64; i++) {
        T1 = h + EP1(e) + CH(e,f,g) + K[i] + W[i];
        T2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void dj_sha256_update(uint32_t state[8], uint8_t buf[64],
                       uint64_t *bitlen, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[(*bitlen >> 3) % 64] = data[i];
        (*bitlen) += 8;
        if ((*bitlen >> 3) % 64 == 0)
            transform(state, buf);
    }
}

void dj_sha256_final(uint32_t state[8], uint8_t buf[64],
                      uint64_t bitlen, uint8_t out[32])
{
    uint64_t bits = bitlen;
    size_t idx = (size_t)(bits >> 3) % 64;
    buf[idx++] = 0x80;

    if (idx > 56) {
        while (idx < 64) buf[idx++] = 0;
        transform(state, buf);
        idx = 0;
    }
    while (idx < 56) buf[idx++] = 0;

    buf[56] = (uint8_t)(bits >> 56);
    buf[57] = (uint8_t)(bits >> 48);
    buf[58] = (uint8_t)(bits >> 40);
    buf[59] = (uint8_t)(bits >> 32);
    buf[60] = (uint8_t)(bits >> 24);
    buf[61] = (uint8_t)(bits >> 16);
    buf[62] = (uint8_t)(bits >> 8);
    buf[63] = (uint8_t)(bits);
    transform(state, buf);

    for (int i = 0; i < 8; i++)
        DJ_LE32ENC(out + i * 4, state[i]);
}

void dj_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint32_t state[8];
    uint8_t  buf[64];
    uint64_t bitlen = 0;
    dj_sha256_init(state);
    memset(buf, 0, sizeof(buf));
    dj_sha256_update(state, buf, &bitlen, data, len);
    dj_sha256_final(state, buf, bitlen, out);
}

/* ── HKDF (simplified — single-step extract-then-expand) ────────────────── */
void dj_hkdf(const uint8_t *salt, size_t salt_len,
              const uint8_t *ikm, size_t ikm_len,
              const uint8_t *info, size_t info_len,
              uint8_t *out, size_t out_len)
{
    uint8_t prk[DJ_SHA256_DIGEST_SIZE];
    uint8_t tmp[32 + 256];
    size_t  tmp_len;

    /* Extract */
    if (salt && salt_len > 0) {
        uint8_t s_salted[64 + 32];
        memcpy(s_salted, salt, salt_len < 64 ? salt_len : 64);
        memset(s_salted + salt_len, 0, 64 - salt_len);
        memcpy(s_salted + 64, ikm, ikm_len);
        dj_sha256(s_salted, 64 + ikm_len, prk);
    } else {
        dj_sha256(ikm, ikm_len, prk);
    }

    /* Expand (simplified — no iterated HMAC) */
    tmp_len = 0;
    for (size_t off = 0; off < out_len; ) {
        size_t cpy = out_len - off;
        if (cpy > 32) cpy = 32;

        memcpy(tmp, prk, 32);
        if (info && info_len > 0)
            memcpy(tmp + 32, info, info_len);
        uint8_t cnt = (uint8_t)((off / 32) + 1);
        tmp[32 + info_len] = cnt;

        uint8_t hash[32];
        dj_sha256(tmp, 32 + info_len + 1, hash);
        memcpy(out + off, hash, cpy);
        off += cpy;
    }

    dj_secure_zero(prk, sizeof(prk));
    dj_secure_zero(tmp, sizeof(tmp));
}

/* ── Secure zeroing ─────────────────────────────────────────────────────── */
void dj_secure_zero(void *buf, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    for (size_t i = 0; i < len; i++) p[i] = 0;
}
