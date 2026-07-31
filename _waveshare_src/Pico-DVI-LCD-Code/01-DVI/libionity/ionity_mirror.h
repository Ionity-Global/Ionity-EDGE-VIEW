#ifndef IONITY_MIRROR_H
#define IONITY_MIRROR_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Live screen mirror.
 *
 * The panel is 640x480 in three 1-bit planes (115 KB). Shipping that whole
 * buffer would saturate the radio and starve the renderer, so each frame is
 * sampled down 4x to 160x120, packed two pixels per byte and run-length
 * encoded. Flat UI compresses to a couple of KB; the worst case is bounded at
 * the packed size because we fall back to raw when RLE would be bigger.
 *
 * Frames are dropped, never queued: if the socket is busy the renderer keeps
 * going and the viewer just sees a lower frame rate.
 *
 * Wire format, little-endian:
 *   magic   u32  'IMF1' (0x31464D49)
 *   width   u16  160
 *   height  u16  120
 *   format  u8   0 = RLE pairs [count][byte], 1 = raw packed
 *   flags   u8   reserved
 *   length  u16  payload bytes that follow
 *   payload ...
 *
 * Packed byte = two pixels, high nibble first. Pixel value 0-7 is
 * bit0 blue | bit1 green | bit2 red.
 */

#define IONITY_MIRROR_PORT    4244
#define IONITY_MIRROR_WIDTH   160
#define IONITY_MIRROR_HEIGHT  120
#define IONITY_MIRROR_PACKED  (IONITY_MIRROR_WIDTH * IONITY_MIRROR_HEIGHT / 2)

bool ionity_mirror_init(uint16_t port);

/* Call once per rendered frame with the live framebuffer. Cheap and returns
 * immediately when nobody is watching. */
void ionity_mirror_tick(const uint8_t *framebuf, uint16_t fb_width, uint16_t fb_height);

/* Frames per second to send while a viewer is attached (1-15). */
void ionity_mirror_set_fps(uint8_t fps);

bool ionity_mirror_active(void);
uint32_t ionity_mirror_frames_sent(void);
uint32_t ionity_mirror_frames_dropped(void);

#endif /* IONITY_MIRROR_H */
