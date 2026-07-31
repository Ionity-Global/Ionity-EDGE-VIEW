/*
 * Emits a mirror frame as base64 on stdout, exactly as the device would send
 * it, so the browser decoder can be checked against the real encoder output.
 *
 * Build: cl /nologo /I..\libionity tools\mirror_vector.c ..\libionity\ionity_mirror_encode.c
 */

#include <stdio.h>
#include <string.h>
#include "ionity_mirror_encode.h"

#define FB_W 640
#define FB_H 480
#define WIDTH_BYTES (FB_W / 8)
#define PLANE (WIDTH_BYTES * FB_H)

static unsigned char fb[3 * PLANE];
static unsigned char packed[IONITY_MIRROR_PACKED];
static unsigned char rle[IONITY_MIRROR_PACKED * 2];

static void set_pixel(int x, int y, unsigned char color) {
    unsigned char mask = (unsigned char)(1u << (x % 8));
    for (unsigned char c = 0; c < 3; ++c) {
        unsigned int addr = (unsigned)(x / 8) + (unsigned)y * WIDTH_BYTES + (unsigned)c * PLANE;
        if (color & (1u << c)) fb[addr] |= mask;
        else                   fb[addr] &= (unsigned char)~mask;
    }
}

static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void emit_b64(const unsigned char *d, int n) {
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned)d[i] << 16;
        if (i + 1 < n) v |= (unsigned)d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        putchar(B64[(v >> 18) & 63]);
        putchar(B64[(v >> 12) & 63]);
        putchar(i + 1 < n ? B64[(v >> 6) & 63] : '=');
        putchar(i + 2 < n ? B64[v & 63] : '=');
    }
}

int main(void) {
    /* A screen shaped like the real one: red header, blue stage, white footer,
       plus single pixels in the corners to pin down the geometry. */
    memset(fb, 0, sizeof(fb));
    for (int y = 0; y < 35; y++)   for (int x = 0; x < FB_W; x++) set_pixel(x, y, 4);
    for (int y = 134; y < 346; y++) for (int x = 4; x < 414; x++) set_pixel(x, y, 1);
    for (int y = 383; y < 425; y++) for (int x = 0; x < FB_W; x++) set_pixel(x, y, 6);
    for (int y = 453; y < FB_H; y++) for (int x = 0; x < FB_W; x++) set_pixel(x, y, 7);
    set_pixel(0, 40, 3);
    set_pixel(636, 40, 5);

    ionity_mirror_downscale(fb, FB_W, FB_H, packed);
    unsigned short n = ionity_mirror_rle(packed, rle, sizeof(rle));

    /* Line 1: base64 RLE payload. Line 2: expected pixels at probe points. */
    emit_b64(rle, n);
    putchar('\n');
    printf("%u %u\n", (unsigned)n, (unsigned)IONITY_MIRROR_PACKED);
    int probes[][2] = { {0,0}, {80,4}, {20,40}, {159,10}, {0,10}, {159,10}, {40,100}, {80,119} };
    for (int i = 0; i < 8; i++)
        printf("%d,%d,%u\n", probes[i][0], probes[i][1],
               (unsigned)ionity_mirror_pixel(packed, probes[i][0], probes[i][1]));
    return 0;
}
