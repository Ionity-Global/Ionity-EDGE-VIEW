#include "ionity_mirror_encode.h"

void ionity_mirror_downscale(const uint8_t *fb, uint16_t fb_width, uint16_t fb_height,
                             uint8_t *out) {
    const uint32_t width_bytes = (uint32_t)fb_width / 8u;
    const uint32_t plane = width_bytes * fb_height;
    const uint16_t step_x = (uint16_t)(fb_width / IONITY_MIRROR_WIDTH);
    const uint16_t step_y = (uint16_t)(fb_height / IONITY_MIRROR_HEIGHT);

    for (uint16_t oy = 0; oy < IONITY_MIRROR_HEIGHT; oy++) {
        const uint32_t row = (uint32_t)(oy * step_y) * width_bytes;
        for (uint16_t ox = 0; ox < IONITY_MIRROR_WIDTH; ox += 2) {
            uint8_t pair = 0;
            for (uint8_t half = 0; half < 2; half++) {
                const uint16_t sx = (uint16_t)((ox + half) * step_x);
                const uint32_t addr = row + (sx >> 3);
                const uint8_t mask = (uint8_t)(1u << (sx & 7u));
                uint8_t v = 0;
                if (fb[addr] & mask)               v |= 1u;  /* blue  */
                if (fb[addr + plane] & mask)       v |= 2u;  /* green */
                if (fb[addr + 2u * plane] & mask)  v |= 4u;  /* red   */
                pair = (uint8_t)((pair << 4) | v);
            }
            *out++ = pair;
        }
    }
}

uint16_t ionity_mirror_rle(const uint8_t *src, uint8_t *dst, uint16_t cap) {
    uint16_t o = 0;
    uint32_t i = 0;
    while (i < IONITY_MIRROR_PACKED) {
        const uint8_t v = src[i];
        uint32_t run = 1;
        while (run < 255 && i + run < IONITY_MIRROR_PACKED && src[i + run] == v) run++;
        if (o + 2 > cap) return 0;
        dst[o++] = (uint8_t)run;
        dst[o++] = v;
        i += run;
    }
    return o;
}

uint16_t ionity_mirror_unrle(const uint8_t *src, uint16_t src_len,
                             uint8_t *dst, uint16_t cap) {
    uint16_t o = 0;
    for (uint16_t i = 0; i + 1 < src_len; i += 2) {
        const uint8_t run = src[i];
        const uint8_t v = src[i + 1];
        for (uint8_t k = 0; k < run; k++) {
            if (o >= cap) return o;
            dst[o++] = v;
        }
    }
    return o;
}
