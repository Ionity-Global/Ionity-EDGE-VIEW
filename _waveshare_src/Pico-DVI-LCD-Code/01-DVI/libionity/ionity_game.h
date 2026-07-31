#ifndef IONITY_GAME_H
#define IONITY_GAME_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Attract-mode games with on-device autopilot AI (BFS pathfinding).
 * Rendered inside a caller-supplied rectangle of the 3-bit framebuffer. */

typedef enum {
    IONITY_GAME_OFF = 0,     /* nothing drawn (caller may use area) */
    IONITY_GAME_PACMAN,      /* self-playing Pac-Man, 4 ghost personalities */
    IONITY_GAME_SNAKE,       /* self-playing Snake, BFS + tail-chase safety */
    IONITY_GAME_BOUNCE,      /* classic bouncing sprites */
} ionity_game_mode_t;

/* Set the play area (pixels) once at startup. */
void ionity_game_init(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* Switch game (resets state). Accepts names: pacman|snake|bounce|off. */
void ionity_game_set_mode(ionity_game_mode_t mode);
bool ionity_game_set_mode_name(const char *name);
ionity_game_mode_t ionity_game_get_mode(void);
const char *ionity_game_mode_name(void);

/* Update + draw one frame (call every frame ~60 Hz). */
void ionity_game_tick(uint32_t frame);

/* Current score line for HUD/status use. */
uint32_t ionity_game_score(void);

#ifdef __cplusplus
}
#endif

#endif
