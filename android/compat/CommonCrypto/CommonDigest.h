/* CommonCrypto/CommonDigest.h → 自带 SHA-256。
 * 调用点只有 ss_shader_capture.c 的四段式用法（Init/Update×2/Final + 一次性）。 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint32_t CC_LONG;
typedef unsigned char *CC_SHA256_CTX_PTR;

typedef struct CC_SHA256_CTX {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t block[64];
    size_t used;
} CC_SHA256_CTX;

#define CC_SHA256_DIGEST_LENGTH 32

int CC_SHA256_Init(CC_SHA256_CTX *ctx);
int CC_SHA256_Update(CC_SHA256_CTX *ctx, const void *data, CC_LONG len);
int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *ctx);
unsigned char *CC_SHA256(const void *data, CC_LONG len, unsigned char *md);
