/* CC_SHA256 —— CommonCrypto 的最小替代（只实现 ss_shader_capture 用到的四段式接口）。 */
#include <string.h>

#include "CommonCrypto/CommonDigest.h"

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64], a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

int CC_SHA256_Init(CC_SHA256_CTX *ctx) {
    if (!ctx) return 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->used = 0;
    return 1;
}

int CC_SHA256_Update(CC_SHA256_CTX *ctx, const void *data, CC_LONG len) {
    if (!ctx) return 0;
    const uint8_t *p = (const uint8_t *)data;
    while (len--) {
        ctx->block[ctx->used++] = *p++;
        if (ctx->used == 64) { transform(ctx->state, ctx->block); ctx->used = 0; ctx->bitlen += 512; }
    }
    return 1;
}

int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *ctx) {
    if (!ctx || !md) return 0;
    uint64_t bits = ctx->bitlen + (uint64_t)ctx->used * 8;
    uint8_t pad = 0x80;
    CC_SHA256_Update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx->used != 56) CC_SHA256_Update(ctx, &zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    /* 直接拼长度，绕过 bitlen 累加（Update 会再次修改 used）。 */
    memcpy(ctx->block + 56, length, 8);
    transform(ctx->state, ctx->block);
    for (int i = 0; i < 8; ++i) {
        md[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        md[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        md[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        md[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
    return 1;
}

unsigned char *CC_SHA256(const void *data, CC_LONG len, unsigned char *md) {
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    CC_SHA256_Update(&ctx, data, len);
    CC_SHA256_Final(md, &ctx);
    return md;
}
