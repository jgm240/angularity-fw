#pragma once

// 1-bit BMP encoder for a Flipper framebuffer (128x64, u8g2 layout: one byte = 8 vertical
// pixels, LSB on top). Output is 4x scaled (512x256) with the Flipper's orange screen colour
// and black pixels. Pure C so it can be tested on a desktop.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SHOT_W      128
#define SHOT_H      64
#define SHOT_SCALE  4
#define SHOT_OUT_W  (SHOT_W * SHOT_SCALE)
#define SHOT_OUT_H  (SHOT_H * SHOT_SCALE)
#define SHOT_ROW_BYTES (SHOT_OUT_W / 8) /* 64, already a multiple of 4 */
#define SHOT_HDR_BYTES 62
#define SHOT_BMP_BYTES (SHOT_HDR_BYTES + SHOT_ROW_BYTES * SHOT_OUT_H)

typedef bool (*ShotWriteFn)(void* ctx, const void* data, size_t len);

static inline void shot_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline bool shot_pixel(const uint8_t* fb, int x, int y, bool rotate180) {
    if(rotate180) {
        x = SHOT_W - 1 - x;
        y = SHOT_H - 1 - y;
    }
    return (fb[(y / 8) * SHOT_W + x] >> (y % 8)) & 1;
}

/** Writes a complete BMP file through `write`. `fb` must hold 1024 bytes. */
static inline bool screenshot_bmp_write(const uint8_t* fb, bool rotate180, ShotWriteFn write, void* ctx) {
    uint8_t hdr[SHOT_HDR_BYTES];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    shot_put32(&hdr[2], SHOT_BMP_BYTES);
    shot_put32(&hdr[10], SHOT_HDR_BYTES); // pixel data offset
    shot_put32(&hdr[14], 40); // BITMAPINFOHEADER size
    shot_put32(&hdr[18], SHOT_OUT_W);
    shot_put32(&hdr[22], SHOT_OUT_H); // positive = rows stored bottom-up
    hdr[26] = 1; // planes
    hdr[28] = 1; // bits per pixel
    shot_put32(&hdr[34], SHOT_ROW_BYTES * SHOT_OUT_H);
    shot_put32(&hdr[46], 2); // palette entries
    // palette entry 0 = pixel off = Flipper orange (B, G, R, 0); entry 1 = pixel on = black
    hdr[54] = 0x00;
    hdr[55] = 0x82;
    hdr[56] = 0xFF;
    if(!write(ctx, hdr, sizeof(hdr))) return false;

    uint8_t row[SHOT_ROW_BYTES];
    for(int out_y = SHOT_OUT_H - 1; out_y >= 0; out_y--) {
        int y = out_y / SHOT_SCALE;
        // build the scaled row once per source line, but write one row per output line
        for(int bx = 0; bx < SHOT_ROW_BYTES; bx++) {
            int x0 = bx * 2; // two source pixels (4 output pixels each) per byte
            uint8_t hi = shot_pixel(fb, x0, y, rotate180) ? 0xF0 : 0x00;
            uint8_t lo = shot_pixel(fb, x0 + 1, y, rotate180) ? 0x0F : 0x00;
            row[bx] = hi | lo;
        }
        if(!write(ctx, row, sizeof(row))) return false;
    }
    return true;
}
