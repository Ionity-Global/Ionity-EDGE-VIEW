#include "ionity_pixels.h"
#include <string.h>

#define SCALE3_RED    0x4
#define SCALE3_GREEN  0x2
#define SCALE3_BLUE   0x1
#define SCALE3_BLACK  0x0
#define SCALE3_WHITE  0x7

void ionity_fill_rect_fast(int x0, int y0, int x1, int y1, uint8_t color) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (Paint.Width == 0 || Paint.Height == 0 ||
        x1 < 0 || y1 < 0 || x0 >= Paint.Width || y0 >= Paint.Height) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= Paint.Width) x1 = Paint.Width - 1;
    if (y1 >= Paint.Height) y1 = Paint.Height - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            Paint_SetPixel(x, y, color);
        }
    }
}

void ionity_fill_gradient_h(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                            uint8_t color_from, uint8_t color_to) {
    if (x0 >= x1) return;
    uint16_t width = x1 - x0;
    for (uint16_t x = x0; x <= x1; x++) {
        uint8_t alpha = (uint8_t)((uint32_t)(x - x0) * 255 / width);
        uint8_t c = ionity_color_blend(color_from, color_to, alpha);
        for (uint16_t y = y0; y <= y1; y++) {
            Paint_SetPixel(x, y, c);
        }
    }
}

void ionity_fill_gradient_v(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                            uint8_t color_from, uint8_t color_to) {
    if (y0 >= y1) return;
    uint16_t height = y1 - y0;
    for (uint16_t y = y0; y <= y1; y++) {
        uint8_t alpha = (uint8_t)((uint32_t)(y - y0) * 255 / height);
        uint8_t c = ionity_color_blend(color_from, color_to, alpha);
        for (uint16_t x = x0; x <= x1; x++) {
            Paint_SetPixel(x, y, c);
        }
    }
}

void ionity_draw_hline(uint16_t x0, uint16_t x1, uint16_t y, uint8_t color) {
    for (uint16_t x = x0; x <= x1; x++) {
        Paint_SetPixel(x, y, color);
    }
}

void ionity_draw_vline(uint16_t x, uint16_t y0, uint16_t y1, uint8_t color) {
    for (uint16_t y = y0; y <= y1; y++) {
        Paint_SetPixel(x, y, color);
    }
}

void ionity_draw_grid(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                      uint16_t spacing, uint8_t color) {
    for (uint16_t x = x0; x <= x1; x += spacing) {
        ionity_draw_vline(x, y0, y1, color);
    }
    for (uint16_t y = y0; y <= y1; y += spacing) {
        ionity_draw_hline(x0, x1, y, color);
    }
}

void ionity_draw_checkerboard(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              uint16_t cell_size, uint8_t color_a, uint8_t color_b) {
    for (uint16_t y = y0; y <= y1; y++) {
        for (uint16_t x = x0; x <= x1; x++) {
            bool even = ((x / cell_size) + (y / cell_size)) & 1;
            Paint_SetPixel(x, y, even ? color_a : color_b);
        }
    }
}

void ionity_draw_dot(uint16_t x, uint16_t y, uint8_t color, uint8_t radius) {
    for (int16_t dy = -(int16_t)radius; dy <= (int16_t)radius; dy++) {
        for (int16_t dx = -(int16_t)radius; dx <= (int16_t)radius; dx++) {
            if (dx * dx + dy * dy <= (int16_t)radius * radius) {
                Paint_SetPixel(x + dx, y + dy, color);
            }
        }
    }
}

void ionity_draw_panel(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                       uint8_t border_color, uint8_t bg_color) {
    ionity_fill_rect_fast(x0 + 1, y0 + 1, x1 - 1, y1 - 1, bg_color);
    Paint_DrawRectangle(x0, y0, x1, y1, border_color, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
}

uint8_t ionity_color_blend(uint8_t c1, uint8_t c2, uint8_t alpha) {
    bool r1 = (c1 >> 2) & 1;
    bool g1 = (c1 >> 1) & 1;
    bool b1 = c1 & 1;
    bool r2 = (c2 >> 2) & 1;
    bool g2 = (c2 >> 1) & 1;
    bool b2 = c2 & 1;

    bool r = (alpha < 128) ? r1 : r2;
    bool g = (alpha < 128) ? g1 : g2;
    bool b = (alpha < 128) ? b1 : b2;

    return ionity_color_from_rgb111(r, g, b);
}

uint8_t ionity_color_from_rgb111(bool r, bool g, bool b) {
    return (r ? SCALE3_RED : 0) | (g ? SCALE3_GREEN : 0) | (b ? SCALE3_BLUE : 0);
}