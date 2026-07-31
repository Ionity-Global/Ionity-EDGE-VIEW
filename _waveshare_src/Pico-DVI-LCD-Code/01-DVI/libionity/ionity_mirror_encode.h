#ifndef IONITY_MIRROR_ENCODE_H
#define IONITY_MIRROR_ENCODE_H

#include <stdint.h>
#include <stddef.h>

/*
 * Pure framebuffer -> wire encoding, with no lwIP or SDK dependency so it can
 * be compiled and tested on a host machine. ionity_mirror.c owns the socket;
 * this file owns the pixels.
 *
 * Source framebuffer is the GUI_Paint scale-3 layout: three 1-bit planes,
 * plane 0 blue, plane 1 green, plane 2 red, LSB-first within each byte,
 * plane stride = (width / 8) * height.
 */

#define IONITY_MIRROR_WIDTH   160
#define IONITY_MIRROR_HEIGHT  120
#define IONITY_MIRROR_PACKED  (IONITY_MIRROR_WIDTH * IONITY_MIRROR_HEIGHT / 2)

/* Sample down to IONITY_MIRROR_WIDTH x IONITY_MIRROR_HEIGHT, two pixels per
 * output byte, high nibble first. `out` must hold IONITY_MIRROR_PACKED bytes. */
void ionity_mirror_downscale(const uint8_t *fb, uint16_t fb_width, uint16_t fb_height,
                             uint8_t *out);

/* Run-length encode `src` (IONITY_MIRROR_PACKED bytes) as [count][byte] pairs.
 * Returns the encoded length, or 0 if it would not fit in `cap`. */
uint16_t ionity_mirror_rle(const uint8_t *src, uint8_t *dst, uint16_t cap);

/* Inverse of ionity_mirror_rle, for tests and any host-side decoder. */
uint16_t ionity_mirror_unrle(const uint8_t *src, uint16_t src_len,
                             uint8_t *dst, uint16_t cap);

/* Read one pixel (0-7) out of the packed buffer. */
static inline uint8_t ionity_mirror_pixel(const uint8_t *packed, int x, int y) {
    const int i = (y * IONITY_MIRROR_WIDTH + x) >> 1;
    return (x & 1) ? (uint8_t)(packed[i] & 0x0F) : (uint8_t)((packed[i] >> 4) & 0x0F);
}

#endif /* IONITY_MIRROR_ENCODE_H */
