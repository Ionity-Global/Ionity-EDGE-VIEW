#include "ionity_brand.h"
#include "ionity_pixels.h"
#include <stdio.h>
#include <string.h>

#define SCALE3_RED    0x4
#define SCALE3_GREEN  0x2
#define SCALE3_BLUE   0x1
#define SCALE3_BLACK  0x0
#define SCALE3_WHITE  0x7
#define SCALE3_YELLOW (SCALE3_RED | SCALE3_GREEN)
#define SCALE3_CYAN   (SCALE3_GREEN | SCALE3_BLUE)
#define SCALE3_MAGENTA (SCALE3_RED | SCALE3_BLUE)

/* IO-nity brand mark, 24x24 design units: red rounded "power ring" with a
 * yellow energy bolt — drawn programmatically so it scales cleanly. */

/* Bolt silhouette as row spans {row, col_start, col_end}. */
static const uint8_t bolt_spans[][3] = {
    { 3, 13, 15}, { 4, 12, 15}, { 5, 12, 14}, { 6, 11, 14},
    { 7, 11, 13}, { 8, 10, 13}, { 9,  8, 16}, {10, 10, 15},
    {11, 10, 13}, {12,  9, 13}, {13,  9, 12}, {14,  8, 11},
    {15,  8, 10}, {16,  7, 10}, {17,  7,  9}, {18,  7,  8},
    {19,  7,  7},
};

static void logo_block(uint16_t x, uint16_t y, uint8_t scale,
                       int col, int row, int col2, int row2, uint8_t color) {
    for (int r = row; r <= row2; r++)
        for (int c = col; c <= col2; c++)
            for (uint8_t sy = 0; sy < scale; sy++)
                for (uint8_t sx = 0; sx < scale; sx++)
                    Paint_SetPixel(x + c * scale + sx, y + r * scale + sy, color);
}

void ionity_draw_logo(uint16_t x, uint16_t y, uint8_t scale) {
    if (scale == 0) scale = 1;
    /* Power ring (rounded-rect outline, 2 units thick). */
    logo_block(x, y, scale, 3, 0, 20, 1, SCALE3_RED);     /* top */
    logo_block(x, y, scale, 3, 22, 20, 23, SCALE3_RED);   /* bottom */
    logo_block(x, y, scale, 0, 3, 1, 20, SCALE3_RED);     /* left */
    logo_block(x, y, scale, 22, 3, 23, 20, SCALE3_RED);   /* right */
    logo_block(x, y, scale, 2, 2, 2, 2, SCALE3_RED);      /* corners */
    logo_block(x, y, scale, 21, 2, 21, 2, SCALE3_RED);
    logo_block(x, y, scale, 2, 21, 2, 21, SCALE3_RED);
    logo_block(x, y, scale, 21, 21, 21, 21, SCALE3_RED);
    /* Energy bolt. */
    for (size_t i = 0; i < sizeof(bolt_spans) / sizeof(bolt_spans[0]); i++)
        logo_block(x, y, scale, bolt_spans[i][1], bolt_spans[i][0],
                   bolt_spans[i][2], bolt_spans[i][0], SCALE3_YELLOW);
    /* Ion spark. */
    logo_block(x, y, scale, 17, 4, 18, 5, SCALE3_WHITE);
}

void ionity_draw_header(uint16_t screen_width, const char *subtitle) {
    ionity_fill_rect_fast(0, 0, screen_width - 1, IONITY_HEADER_HEIGHT - 1, SCALE3_BLACK);

    ionity_fill_rect_fast(0, 0, screen_width - 1, 2, SCALE3_RED);
    ionity_fill_rect_fast(0, IONITY_HEADER_HEIGHT - 3, screen_width - 1, IONITY_HEADER_HEIGHT - 1, SCALE3_RED);

    Paint_DrawString_EN(10, 8, "IO-NITY", &Font20, SCALE3_RED, SCALE3_BLACK);

    if (subtitle) {
        Paint_DrawString_EN(screen_width - 200, 12, subtitle, &Font12, SCALE3_WHITE, SCALE3_BLACK);
    }

    ionity_draw_hline(0, screen_width - 1, IONITY_HEADER_HEIGHT - 2, SCALE3_RED);
}

void ionity_draw_footer(uint16_t screen_width, uint16_t screen_height,
                        const char *status_left, const char *status_right) {
    uint16_t footer_y = screen_height - IONITY_FOOTER_HEIGHT;

    ionity_fill_rect_fast(0, footer_y, screen_width - 1, screen_height - 1, SCALE3_BLACK);
    ionity_draw_hline(0, screen_width - 1, footer_y, SCALE3_GREEN);
    ionity_draw_hline(0, screen_width - 1, screen_height - 1, SCALE3_GREEN);

    if (status_left) {
        Paint_DrawString_EN(5, footer_y + 5, status_left, &Font12, SCALE3_GREEN, SCALE3_BLACK);
    }
    if (status_right) {
        Paint_DrawString_EN(screen_width - 200, footer_y + 5, status_right, &Font12, SCALE3_GREEN, SCALE3_BLACK);
    }
}

void ionity_draw_weather_panel(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               int temp_c, int condition) {
    ionity_draw_panel(x, y, x + w, y + h, SCALE3_WHITE, SCALE3_BLACK);

    char buf[32];
    snprintf(buf, sizeof(buf), "WEATHER");
    Paint_DrawString_EN(x + 5, y + 3, buf, &Font16, SCALE3_WHITE, SCALE3_BLACK);
    ionity_draw_hline(x + 2, x + w - 2, y + 18, SCALE3_WHITE);

    snprintf(buf, sizeof(buf), "%d C", temp_c);
    Paint_DrawString_EN(x + 5, y + 22, buf, &Font20, SCALE3_YELLOW, SCALE3_BLACK);

    const char *cond_str = "Clear";
    switch (condition) {
        case 0: cond_str = "Sunny"; break;
        case 1: cond_str = "Cloudy"; break;
        case 2: cond_str = "Rain"; break;
        case 3: cond_str = "Storm"; break;
        case 4: cond_str = "Snow"; break;
        case 5: cond_str = "Fog"; break;
    }
    Paint_DrawString_EN(x + 5, y + 42, cond_str, &Font12, SCALE3_CYAN, SCALE3_BLACK);
}

void ionity_draw_ai_status(uint16_t x, uint16_t y, bool connected, const char *last_msg) {
    char buf[64];
    snprintf(buf, sizeof(buf), "AI: %s", connected ? "ONLINE" : "OFFLINE");
    Paint_DrawString_EN(x, y, buf, &Font12,
                        connected ? SCALE3_GREEN : SCALE3_RED, SCALE3_BLACK);

    if (last_msg) {
        snprintf(buf, sizeof(buf), "%.40s", last_msg);
        Paint_DrawString_EN(x, y + 15, buf, &Font12, SCALE3_WHITE, SCALE3_BLACK);
    }
}