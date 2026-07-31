#ifndef IONITY_BRAND_H
#define IONITY_BRAND_H

#include "pico/stdlib.h"
#include "GUI_Paint.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IONITY_HEADER_HEIGHT 40
#define IONITY_FOOTER_HEIGHT 30
#define IONITY_BRAND_COLOR_PRIMARY   (0x4)  /* RED */
#define IONITY_BRAND_COLOR_ACCENT    (0x2)  /* GREEN */
#define IONITY_BRAND_COLOR_HIGHLIGHT (0x6)  /* YELLOW (RED+GREEN) */
#define IONITY_BRAND_COLOR_DIM       (0x1)  /* BLUE */

void ionity_draw_header(uint16_t screen_width, const char *subtitle);
void ionity_draw_footer(uint16_t screen_width, uint16_t screen_height,
                        const char *status_left, const char *status_right);
void ionity_draw_logo(uint16_t x, uint16_t y, uint8_t scale);
void ionity_draw_weather_panel(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               int temp_c, int condition);
void ionity_draw_ai_status(uint16_t x, uint16_t y, bool connected, const char *last_msg);

#ifdef __cplusplus
}
#endif

#endif