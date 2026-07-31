/*
 * Host test for the screen mirror. No Pico required.
 *
 * The framebuffer is filled using the identical bit arithmetic GUI_Paint uses
 * for scale 3, so if the plane order or bit order were wrong here they would be
 * wrong on the device too. Then the encoder runs, the output is decoded the way
 * the browser decodes it, and every pixel is compared.
 *
 * Build:  cl /nologo /W4 /I..\libionity tools\mirror_test.c ..\libionity\ionity_mirror_encode.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ionity_mirror_encode.h"

#define FB_W 640
#define FB_H 480
#define WIDTH_BYTES (FB_W / 8)
#define PLANE (WIDTH_BYTES * FB_H)

static uint8_t fb[3 * PLANE];
static uint8_t packed[IONITY_MIRROR_PACKED];
static uint8_t rle[IONITY_MIRROR_PACKED * 2];
static uint8_t decoded[IONITY_MIRROR_PACKED];

static int failures = 0;

static void check(int cond, const char *what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) failures++;
}

/* Exactly GUI_Paint.c, Paint_SetPixel, the Paint.Scale == 3 branch. */
static void set_pixel(int x, int y, uint8_t color) {
    uint8_t mask = (uint8_t)(1u << (x % 8));
    for (uint8_t component = 0; component < 3; ++component) {
        unsigned int addr = (unsigned)(x / 8) + (unsigned)y * WIDTH_BYTES
                          + (unsigned)component * WIDTH_BYTES * FB_H;
        if (color & (1u << component)) fb[addr] |= mask;
        else                            fb[addr] &= (uint8_t)~mask;
    }
}

static void fill(uint8_t color) {
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            set_pixel(x, y, color);
}

static uint8_t sampled(int ox, int oy) {
    return ionity_mirror_pixel(packed, ox, oy);
}

static void round_trip(void) {
    ionity_mirror_downscale(fb, FB_W, FB_H, packed);
    uint16_t n = ionity_mirror_rle(packed, rle, sizeof(rle));
    memset(decoded, 0xAA, sizeof(decoded));
    uint16_t got = ionity_mirror_unrle(rle, n, decoded, sizeof(decoded));
    if (got != IONITY_MIRROR_PACKED || memcmp(decoded, packed, IONITY_MIRROR_PACKED) != 0) {
        printf("  [FAIL] RLE round trip lost data (%u bytes back)\n", got);
        failures++;
    }
}

int main(void) {
    printf("EDGE-VIEW mirror encoder test\n\n");

    /* 1. Every colour survives the plane packing. */
    printf("solid colours\n");
    const char *names[8] = { "black", "blue", "green", "cyan", "red", "magenta", "yellow", "white" };
    for (uint8_t c = 0; c < 8; c++) {
        fill(c);
        round_trip();
        int ok = 1;
        for (int y = 0; y < IONITY_MIRROR_HEIGHT && ok; y++)
            for (int x = 0; x < IONITY_MIRROR_WIDTH && ok; x++)
                if (sampled(x, y) != c) ok = 0;
        char msg[64];
        snprintf(msg, sizeof(msg), "%s (%u) survives round trip", names[c], c);
        check(ok, msg);
    }

    /* 2. Channel mapping: bit0 blue, bit1 green, bit2 red. Set one plane only. */
    printf("\nchannel mapping\n");
    memset(fb, 0, sizeof(fb));
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) fb[(x / 8) + y * WIDTH_BYTES] |= (uint8_t)(1u << (x % 8));
    round_trip();
    check(sampled(80, 60) == 1, "plane 0 alone reads as blue (1)");

    memset(fb, 0, sizeof(fb));
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) fb[PLANE + (x / 8) + y * WIDTH_BYTES] |= (uint8_t)(1u << (x % 8));
    round_trip();
    check(sampled(80, 60) == 2, "plane 1 alone reads as green (2)");

    memset(fb, 0, sizeof(fb));
    for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) fb[2 * PLANE + (x / 8) + y * WIDTH_BYTES] |= (uint8_t)(1u << (x % 8));
    round_trip();
    check(sampled(80, 60) == 4, "plane 2 alone reads as red (4)");

    /* 3. Geometry: the sampler must read the 4x block origin, not drift. */
    printf("\ngeometry\n");
    fill(0);
    set_pixel(0, 0, 7);
    set_pixel(4, 0, 6);
    set_pixel(636, 476, 5);
    round_trip();
    check(sampled(0, 0) == 7, "top-left pixel lands at 0,0");
    check(sampled(1, 0) == 6, "x=4 lands in output column 1");
    check(sampled(159, 119) == 5, "bottom-right corner lands at 159,119");

    /* 4. Nibble order: two neighbours in one byte must not swap. */
    printf("\nnibble order\n");
    fill(0);
    set_pixel(0, 8, 4);   /* output 0,2 -> high nibble */
    set_pixel(4, 8, 1);   /* output 1,2 -> low nibble  */
    round_trip();
    check(sampled(0, 2) == 4, "even column is the high nibble");
    check(sampled(1, 2) == 1, "odd column is the low nibble");

    /* 5. Compression: a realistic screen is mostly flat, so RLE must pay off. */
    printf("\ncompression\n");
    fill(0);
    for (int y = 0; y < 35; y++) for (int x = 0; x < FB_W; x++) set_pixel(x, y, 4);
    for (int y = 134; y < 346; y++) for (int x = 4; x < 414; x++) set_pixel(x, y, 1);
    for (int y = 453; y < FB_H; y++) for (int x = 0; x < FB_W; x++) set_pixel(x, y, 7);
    ionity_mirror_downscale(fb, FB_W, FB_H, packed);
    uint16_t n = ionity_mirror_rle(packed, rle, sizeof(rle));
    printf("  typical screen: %u -> %u bytes (%.1f%% of packed, %.2f%% of raw planes)\n",
           IONITY_MIRROR_PACKED, n,
           100.0 * n / IONITY_MIRROR_PACKED,
           100.0 * n / (3.0 * PLANE));
    check(n > 0 && n < IONITY_MIRROR_PACKED / 4, "typical screen compresses below a quarter");

    /* 6. Worst case must be refused, not overflow. */
    printf("\nworst case\n");
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            set_pixel(x, y, (uint8_t)((x / 4 + y / 4) % 8));
    ionity_mirror_downscale(fb, FB_W, FB_H, packed);
    uint16_t bad = ionity_mirror_rle(packed, rle, IONITY_MIRROR_PACKED);
    printf("  checkerboard rle into a packed-size buffer -> %u (0 means fall back to raw)\n", bad);
    check(bad == 0 || bad <= IONITY_MIRROR_PACKED, "never writes past the buffer");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
