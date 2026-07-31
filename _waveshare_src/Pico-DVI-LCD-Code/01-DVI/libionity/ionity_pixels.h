#ifndef IONITY_PIXELS_H
#define IONITY_PIXELS_H

#include "pico/stdlib.h"
#include "GUI_Paint.h"

#ifdef __cplusplus
extern "C" {
#endif

void ionity_fill_gradient_h(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                            uint8_t color_from, uint8_t color_to);

void ionity_fill_gradient_v(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                            uint8_t color_from, uint8_t color_to);

void ionity_draw_hline(uint16_t x0, uint16_t x1, uint16_t y, uint8_t color);

void ionity_draw_vline(uint16_t x, uint16_t y0, uint16_t y1, uint8_t color);

void ionity_draw_grid(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                      uint16_t spacing, uint8_t color);

void ionity_draw_checkerboard(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              uint16_t cell_size, uint8_t color_a, uint8_t color_b);

void ionity_draw_dot(uint16_t x, uint16_t y, uint8_t color, uint8_t radius);

void ionity_fill_rect_fast(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t color);

void ionity_draw_panel(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                       uint8_t border_color, uint8_t bg_color);

uint8_t ionity_color_blend(uint8_t c1, uint8_t c2, uint8_t alpha);

uint8_t ionity_color_from_rgb111(bool r, bool g, bool b);

#ifdef __cplusplus
}
#endif

#endif