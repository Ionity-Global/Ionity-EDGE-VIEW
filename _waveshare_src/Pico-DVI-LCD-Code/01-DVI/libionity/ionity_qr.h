#ifndef IONITY_QR_H
#define IONITY_QR_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal QR encoder: Version 2 (25x25), ECC level L, byte mode, mask 0.
 * Capacity: 32 bytes — enough for "http://<ip>/" broadcast links. */

#define IONITY_QR_SIZE 25
#define IONITY_QR_MAX_TEXT 32

/* Encode text. Returns false if too long. Modules in out[y][x] (1 = dark). */
bool ionity_qr_encode(const char *text, uint8_t out[IONITY_QR_SIZE][IONITY_QR_SIZE]);

/* Encode + draw at pixel (x,y), each module `scale` px, with quiet zone. */
bool ionity_qr_draw(const char *text, uint16_t x, uint16_t y, uint8_t scale);

#ifdef __cplusplus
}
#endif

#endif
