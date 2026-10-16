#pragma once

// Minimal streaming SHA-256 (FIPS 180-4). The firmware does not export mbedtls
// hashing to apps, and this is small enough to carry. Header-only so it can be
// unit-tested on a desktop compiler.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t block[64];
    uint32_t block_len;
} OtaSha256;

static const uint32_t ota_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

#define OTA_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void ota_sha256_transform(OtaSha256* s, const uint8_t* p) {
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for(int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for(int i = 16; i < 64; i++) {
        uint32_t s0 = OTA_ROR(w[i - 15], 7) ^ OTA_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = OTA_ROR(w[i - 2], 17) ^ OTA_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->state[0];
    b = s->state[1];
    c = s->state[2];
    d = s->state[3];
    e = s->state[4];
    f = s->state[5];
    g = s->state[6];
    h = s->state[7];
    for(int i = 0; i < 64; i++) {
        uint32_t s1 = OTA_ROR(e, 6) ^ OTA_ROR(e, 11) ^ OTA_ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + ota_sha256_k[i] + w[i];
        uint32_t s0 = OTA_ROR(a, 2) ^ OTA_ROR(a, 13) ^ OTA_ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

static void ota_sha256_init(OtaSha256* s) {
    static const uint32_t init[8] = {
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19};
    memcpy(s->state, init, sizeof(init));
    s->bit_len = 0;
    s->block_len = 0;
}

static void ota_sha256_update(OtaSha256* s, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        s->block[s->block_len++] = data[i];
        if(s->block_len == 64) {
            ota_sha256_transform(s, s->block);
            s->bit_len += 512;
            s->block_len = 0;
        }
    }
}

/** Writes the digest as 64 lowercase hex characters plus a terminating NUL. */
static void ota_sha256_final_hex(OtaSha256* s, char out[65]) {
    s->bit_len += (uint64_t)s->block_len * 8;
    s->block[s->block_len++] = 0x80;
    if(s->block_len > 56) {
        while(s->block_len < 64)
            s->block[s->block_len++] = 0;
        ota_sha256_transform(s, s->block);
        s->block_len = 0;
    }
    while(s->block_len < 56)
        s->block[s->block_len++] = 0;
    for(int i = 0; i < 8; i++) {
        s->block[56 + i] = (uint8_t)(s->bit_len >> (56 - 8 * i));
    }
    ota_sha256_transform(s, s->block);

    static const char hex[] = "0123456789abcdef";
    for(int i = 0; i < 8; i++) {
        for(int j = 0; j < 4; j++) {
            uint8_t byte = (uint8_t)(s->state[i] >> (24 - 8 * j));
            out[i * 8 + j * 2] = hex[byte >> 4];
            out[i * 8 + j * 2 + 1] = hex[byte & 15];
        }
    }
    out[64] = '\0';
}
