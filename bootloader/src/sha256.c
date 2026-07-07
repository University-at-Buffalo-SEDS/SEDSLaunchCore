#include "launchcore/image.h"
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t block[64];
    size_t used;
} sha256_ctx_t;

static const uint32_t k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }
static uint32_t load_be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void store_be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void transform(sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    for (uint32_t i = 0; i < 16u; ++i) w[i] = load_be(&block[i * 4u]);
    for (uint32_t i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    uint32_t e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];
    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void init(sha256_ctx_t *ctx)
{
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u; ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu; ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->len = 0u;
    ctx->used = 0u;
}

static void update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    ctx->len += (uint64_t)len * 8u;
    while (len > 0u) {
        size_t n = 64u - ctx->used;
        if (n > len) n = len;
        memcpy(&ctx->block[ctx->used], data, n);
        ctx->used += n;
        data += n;
        len -= n;
        if (ctx->used == 64u) {
            transform(ctx, ctx->block);
            ctx->used = 0u;
        }
    }
}

static void final(sha256_ctx_t *ctx, uint8_t out[32])
{
    ctx->block[ctx->used++] = 0x80u;
    if (ctx->used > 56u) {
        while (ctx->used < 64u) ctx->block[ctx->used++] = 0u;
        transform(ctx, ctx->block);
        ctx->used = 0u;
    }
    while (ctx->used < 56u) ctx->block[ctx->used++] = 0u;
    for (uint32_t i = 0; i < 8u; ++i) ctx->block[56u + i] = (uint8_t)(ctx->len >> (56u - i * 8u));
    transform(ctx, ctx->block);
    for (uint32_t i = 0; i < 8u; ++i) store_be(&out[i * 4u], ctx->h[i]);
}

void launchcore_sha256(const void *data, size_t len, uint8_t out[32])
{
    sha256_ctx_t ctx;
    init(&ctx);
    update(&ctx, (const uint8_t *)data, len);
    final(&ctx, out);
}
