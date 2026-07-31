/* IO-nity EDGE-VIEW — server-driven display client.
 *
 * The Pico does not own the content. EDGE-VIEW Studio (the server) computes
 * everything and streams it here over WiFi; this firmware is a renderer with
 * an interchangeable layout:
 *
 *   - SLOTS are fixed rectangles on the 640x480 screen.
 *   - PANELS are named renderers that can be bound to any slot at runtime:
 *         {"type":"layout","slot":"stage","panel":"clock"}
 *   - CONTENT arrives as key/value pairs and panels read it by key:
 *         {"type":"data","key":"wx.temp","value":"21"}
 *   - WiFi itself is reprovisionable remotely:
 *         {"type":"wifi","ssid":"...","pass":"..."}
 *
 * Everything degrades gracefully: when the server is quiet the panels fall
 * back to on-device sources (NTP, Open-Meteo, the local game AI) so the screen
 * is never blank.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"

#include "GUI_Paint.h"

#include "ionity_wifi.h"
#include "ionity_stream.h"
#include "ionity_pixels.h"
#include "ionity_brand.h"
#include "ionity_http.h"
#include "ionity_time.h"
#include "ionity_weather.h"
#include "ionity_game.h"
#include "ionity_qr.h"
#include "ionity_data.h"
#include "ionity_scene.h"
#include "ionity_mirror.h"

#define FRAME_WIDTH  640
#define FRAME_HEIGHT 480
#define VREG_VSEL    VREG_VOLTAGE_1_10
#define DVI_TIMING   dvi_timing_640x480p_60hz

#define PLANE_SIZE_BYTES (FRAME_WIDTH * FRAME_HEIGHT / 8)
#define SCALE3_RED    0x4
#define SCALE3_GREEN  0x2
#define SCALE3_BLUE   0x1
#define SCALE3_BLACK  0x0
#define SCALE3_WHITE  0x7
#define SCALE3_YELLOW (SCALE3_RED | SCALE3_GREEN)
#define SCALE3_CYAN   (SCALE3_GREEN | SCALE3_BLUE)
#define SCALE3_MAGENTA (SCALE3_RED | SCALE3_BLUE)

/* Character cell widths for the bundled fonts. */
#define CW8   5
#define CW12  7
#define CW16  11
#define CW20  14
#define CW24  17

uint8_t framebuf[3 * PLANE_SIZE_BYTES];
static uint8_t backbuf[3 * PLANE_SIZE_BYTES];

/* Double buffered. Core 1 scans `scan_buf` while core 0 paints `draw_buf`;
 * without this the panel shows the buffer mid-clear and flickers black every
 * frame. `next_buf` is published by core 0 and picked up by core 1 at the top
 * of a frame, so a swap never tears. */
static uint8_t *volatile scan_buf = framebuf;
static uint8_t *volatile next_buf = NULL;
static uint8_t *draw_buf = backbuf;
static volatile bool core1_running = false;
struct dvi_inst dvi0;

static uint32_t g_frame = 0;
static uint32_t fps_value = 0, fps_last = 0, fps_cnt = 0;
static bool     net_services_up = false;
static char     ai_action[128] = "Waiting for EDGE-VIEW Studio...";

/* ---- Local fallbacks, used only until the server streams its own ---- */
static char marquee_text[384] =
    "IO-nity EDGE-VIEW - post a message from any device on this WiFi";
static int  marquee_offset = 0;

static char verse_text[360] =
    "You are the light of the world. Let your light shine before men, "
    "that they may see your good deeds and praise your Father in heaven.";
static char verse_ref[64] = "Matthew 5:14-16";

#define NEWS_RING 12
#define NEWS_LEN  168
static char news_ring[NEWS_RING][NEWS_LEN];
static int  news_count = 0, news_write = 0;
static int  news_idx = 0, news_scroll = 0;
static const char *news_fallback[] = {
    "IO-nity EDGE-VIEW standing by - waiting for the Studio server",
    "Run EDGE-VIEW Studio on Windows to stream live AI headlines here",
    "Ionity Global (Pty) Ltd - Native-AI, AIoT, Cloud and Edge - ionity.co.za",
};
#define NUM_FALLBACK (sizeof(news_fallback) / sizeof(news_fallback[0]))

static void news_push(const char *headline) {
    if (!headline || !headline[0]) return;
    snprintf(news_ring[news_write], NEWS_LEN, "%.*s", NEWS_LEN - 1, headline);
    news_write = (news_write + 1) % NEWS_RING;
    if (news_count < NEWS_RING) news_count++;
}

static const char *news_current(void) {
    if (news_count == 0) return news_fallback[news_idx % NUM_FALLBACK];
    return news_ring[news_idx % news_count];
}

/* ---- Chat ring: what the AI and people are saying, on the glass ---- */

#define CHAT_RING 10
#define CHAT_WHO  14
#define CHAT_LEN  120
static char chat_who[CHAT_RING][CHAT_WHO];
static char chat_msg[CHAT_RING][CHAT_LEN];
static int  chat_count = 0, chat_write = 0;
static uint32_t chat_seq = 0;

static void chat_push(const char *who, const char *text) {
    if (!text || !text[0]) return;
    snprintf(chat_who[chat_write], CHAT_WHO, "%.*s", CHAT_WHO - 1,
             (who && who[0]) ? who : "web");
    snprintf(chat_msg[chat_write], CHAT_LEN, "%.*s", CHAT_LEN - 1, text);
    chat_write = (chat_write + 1) % CHAT_RING;
    if (chat_count < CHAT_RING) chat_count++;
    chat_seq++;
}

/* Appends to the newest line instead of starting a new one, so a reply can be
 * streamed in a token at a time. */
static void chat_append(const char *text) {
    if (!text || !text[0]) return;
    if (chat_count == 0) { chat_push("ai", text); return; }
    int last = (chat_write - 1 + CHAT_RING) % CHAT_RING;
    size_t len = strlen(chat_msg[last]);
    if (len < CHAT_LEN - 1)
        snprintf(chat_msg[last] + len, CHAT_LEN - len, "%.*s",
                 (int)(CHAT_LEN - len - 1), text);
    chat_seq++;
}

/* ---- Shared drawing helpers ---- */

static void panel_frame(const ionity_rect_t *r, const char *title,
                        uint8_t title_color, uint8_t border) {
    ionity_draw_panel(r->x, r->y, r->x + r->w, r->y + r->h, border, SCALE3_BLACK);
    if (title && title[0]) {
        Paint_DrawString_EN(r->x + 6, r->y + 3, (char *)title, &Font16,
                            title_color, SCALE3_BLACK);
        ionity_draw_hline(r->x + 2, r->x + r->w - 2, r->y + 20, border);
    }
}

/* Greedy word wrap. Returns the y just past the last line drawn. */
static int draw_wrapped(int x, int y, int max_y, int width_px,
                        const char *text, uint8_t color) {
    int max_chars = width_px / CW12;
    if (max_chars < 4) return y;
    char line[80];
    const char *p = text;
    while (*p && y < max_y) {
        int n = (int)strlen(p);
        if (n > max_chars) {
            n = max_chars;
            while (n > 0 && p[n] != ' ') n--;
            if (n == 0) n = max_chars;
        }
        if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
        snprintf(line, sizeof(line), "%.*s", n, p);
        Paint_DrawString_EN(x, y, line, &Font12, color, SCALE3_BLACK);
        y += 14;
        p += n;
        while (*p == ' ') p++;
    }
    return y;
}

/* Horizontal scroller shared by the marquee and news banner. */
static void draw_scroller(int x0, int x1, int y, const char *s, int offset,
                          sFONT *font, int cw, uint8_t color) {
    int len = (int)strlen(s);
    for (int i = 0; i < len; i++) {
        int cx = x0 + i * cw - offset;
        if (cx < x0 - cw) continue;
        if (cx > x1 - cw) break;
        char ch[2] = {s[i], 0};
        Paint_DrawString_EN(cx, y, ch, font, color, SCALE3_BLACK);
    }
}

/* =================== Panels =================== */

static void panel_header(const ionity_rect_t *r) {
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, SCALE3_BLACK);
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + 1, SCALE3_RED);
    ionity_fill_rect_fast(r->x, r->y + r->h - 2, r->x + r->w - 1, r->y + r->h - 1, SCALE3_RED);

    ionity_draw_logo(r->x + 4, r->y + 5, 1);
    Paint_DrawString_EN(r->x + 34, r->y + 4,
                        (char *)ionity_data_get("hdr.title", "IO-NITY EDGE-VIEW"),
                        &Font16, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(r->x + 34, r->y + 22,
                        (char *)ionity_data_get("hdr.sub", "Ionity Global (Pty) Ltd"),
                        &Font8, SCALE3_RED, SCALE3_BLACK);

    /* Clock, top right. Server time wins; NTP is the fallback. */
    char clk[16];
    const char *pushed = ionity_data_get_fresh("clock");
    if (pushed && pushed[0]) {
        snprintf(clk, sizeof(clk), "%.11s", pushed);
    } else {
        ionity_time_clock_str(clk, sizeof(clk));
    }
    Paint_DrawString_EN(r->x + r->w - 4 - (int)strlen(clk) * CW20, r->y + 8, clk,
                        &Font20,
                        (pushed || ionity_time_valid()) ? SCALE3_YELLOW : SCALE3_BLUE,
                        SCALE3_BLACK);

    /* Small server-link dot beside the clock. */
    uint8_t dot = ionity_data_have_server() ? SCALE3_GREEN : SCALE3_RED;
    ionity_fill_rect_fast(r->x + r->w - 10, r->y + 2, r->x + r->w - 5, r->y + 7, dot);
}

static void panel_clock(const ionity_rect_t *r) {
    panel_frame(r, "TIME", SCALE3_CYAN, SCALE3_WHITE);
    char clk[16], date[32];
    const char *pushed = ionity_data_get_fresh("clock");
    if (pushed && pushed[0]) snprintf(clk, sizeof(clk), "%.11s", pushed);
    else ionity_time_clock_str(clk, sizeof(clk));
    ionity_time_date_str(date, sizeof(date));

    int cx = r->x + (r->w - (int)strlen(clk) * CW24) / 2;
    Paint_DrawString_EN(cx < r->x + 4 ? r->x + 4 : cx, r->y + r->h / 2 - 20, clk,
                        &Font24, SCALE3_YELLOW, SCALE3_BLACK);
    const char *d = ionity_data_get("date", date);
    if (d[0]) {
        int dx = r->x + (r->w - (int)strlen(d) * CW12) / 2;
        Paint_DrawString_EN(dx < r->x + 4 ? r->x + 4 : dx, r->y + r->h / 2 + 12,
                            (char *)d, &Font12, SCALE3_WHITE, SCALE3_BLACK);
    }
}

static void panel_network(const ionity_rect_t *r) {
    panel_frame(r, "NETWORK", SCALE3_CYAN, SCALE3_WHITE);

    ionity_wifi_info_t wi = ionity_wifi_get_info();
    char b[64];
    int ly = r->y + 25;
    const char *st = "OFFLINE";
    uint8_t stc = SCALE3_RED;
    switch (wi.state) {
    case IONITY_WIFI_CONNECTED:  st = "CONNECTED";    stc = SCALE3_GREEN;  break;
    case IONITY_WIFI_CONNECTING: st = "RECONNECTING"; stc = SCALE3_YELLOW; break;
    case IONITY_WIFI_AP_MODE:    st = "SETUP MODE";   stc = SCALE3_YELLOW; break;
    default: break;
    }
    Paint_DrawString_EN(r->x + 6, ly, (char *)st, &Font12, stc, SCALE3_BLACK); ly += 15;
    snprintf(b, sizeof(b), "SSID %.22s", wi.ssid[0] ? wi.ssid : "-");
    Paint_DrawString_EN(r->x + 6, ly, b, &Font12, SCALE3_WHITE, SCALE3_BLACK); ly += 15;
    snprintf(b, sizeof(b), "IP   %s", wi.ip_addr[0] ? wi.ip_addr : "-");
    Paint_DrawString_EN(r->x + 6, ly, b, &Font12, SCALE3_WHITE, SCALE3_BLACK); ly += 15;
    Paint_DrawString_EN(r->x + 6, ly, "TCP 4242  HTTP 80", &Font12,
                        SCALE3_YELLOW, SCALE3_BLACK);

    int bars = 0;
    if (wi.state == IONITY_WIFI_CONNECTED) {
        if (wi.rssi >= -55) bars = 4;
        else if (wi.rssi >= -65) bars = 3;
        else if (wi.rssi >= -75) bars = 2;
        else bars = 1;
    }
    for (int i = 0; i < 4; i++) {
        int bx = r->x + r->w - 34 + i * 8, bh = 4 + i * 4;
        ionity_fill_rect_fast(bx, r->y + 18 - bh, bx + 5, r->y + 18,
                              i < bars ? SCALE3_GREEN : SCALE3_BLUE);
    }
}

static void panel_feed(const ionity_rect_t *r) {
    panel_frame(r, "SERVER LINK", SCALE3_MAGENTA, SCALE3_WHITE);

    char b[64];
    int ly = r->y + 25;
    bool link = ionity_stream_is_connected();
    bool srv  = ionity_data_have_server();

    snprintf(b, sizeof(b), "STUDIO %s", link ? "LINKED" : "WAITING");
    Paint_DrawString_EN(r->x + 6, ly, b, &Font12,
                        link ? SCALE3_GREEN : SCALE3_YELLOW, SCALE3_BLACK); ly += 15;

    if (srv) {
        uint32_t age = ionity_data_server_age_ms() / 1000;
        snprintf(b, sizeof(b), "STREAM %d keys  %lus", ionity_data_count(),
                 (unsigned long)age);
    } else {
        snprintf(b, sizeof(b), "LOCAL FALLBACK MODE");
    }
    Paint_DrawString_EN(r->x + 6, ly, b, &Font12,
                        srv ? SCALE3_CYAN : SCALE3_YELLOW, SCALE3_BLACK); ly += 15;

    snprintf(b, sizeof(b), "NEWS %d  MSGS %d  NTP %s", news_count,
             ionity_http_msg_count(), ionity_time_valid() ? "OK" : "--");
    Paint_DrawString_EN(r->x + 6, ly, b, &Font12, SCALE3_CYAN, SCALE3_BLACK); ly += 15;
    snprintf(b, sizeof(b), "%.32s", ai_action);
    Paint_DrawString_EN(r->x + 6, ly, b, &Font12, SCALE3_WHITE, SCALE3_BLACK);
}

static void draw_weather_icon(int x, int y, int cond, uint32_t frame) {
    switch (cond) {
    case IONITY_WX_SUNNY:
        Paint_DrawCircle(x + 12, y + 12, 8, SCALE3_YELLOW, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        for (int i = 0; i < 2; i++) {
            Paint_SetPixel(x + 12 + (i ? 11 : -11), y + 12, SCALE3_YELLOW);
            Paint_SetPixel(x + 12, y + 12 + (i ? 11 : -11), SCALE3_YELLOW);
        }
        break;
    case IONITY_WX_CLOUDY:
        Paint_DrawCircle(x + 9, y + 14, 6, SCALE3_WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawCircle(x + 16, y + 12, 7, SCALE3_WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        break;
    case IONITY_WX_RAIN:
        Paint_DrawCircle(x + 12, y + 9, 7, SCALE3_WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        for (int i = 0; i < 3; i++) {
            int dy = (int)((frame / 4 + i * 3) % 8);
            ionity_draw_vline(x + 6 + i * 6, y + 16 + dy, y + 18 + dy, SCALE3_CYAN);
        }
        break;
    case IONITY_WX_STORM:
        Paint_DrawCircle(x + 12, y + 8, 7, SCALE3_WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        if ((frame / 12) & 1) {
            ionity_draw_vline(x + 12, y + 14, y + 20, SCALE3_YELLOW);
            ionity_draw_vline(x + 11, y + 17, y + 22, SCALE3_YELLOW);
        }
        break;
    case IONITY_WX_SNOW:
        Paint_DrawCircle(x + 12, y + 9, 7, SCALE3_WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        for (int i = 0; i < 3; i++)
            Paint_SetPixel(x + 7 + i * 5, y + 18 + (int)((frame / 8 + i) % 4), SCALE3_WHITE);
        break;
    default:
        for (int i = 0; i < 4; i++)
            ionity_draw_hline(x + 2, x + 22, y + 6 + i * 5, SCALE3_WHITE);
        break;
    }
}

static void panel_weather(const ionity_rect_t *r) {
    panel_frame(r, "WEATHER", SCALE3_CYAN, SCALE3_WHITE);

    char b[32];
    ionity_time_date_str(b, sizeof(b));
    const char *date = ionity_data_get("date", b);
    if (date[0])
        Paint_DrawString_EN(r->x + 6, r->y + 24, (char *)date, &Font12,
                            SCALE3_WHITE, SCALE3_BLACK);

    /* Server value first, on-device Open-Meteo second. */
    const char *stemp = ionity_data_get_fresh("wx.temp");
    bool have = stemp || ionity_weather_valid();
    int temp = stemp ? atoi(stemp) : ionity_weather_temp_c();
    int cond = ionity_data_get_int("wx.cond", ionity_weather_condition());

    if (have) {
        snprintf(b, sizeof(b), "%dC", temp);
        Paint_DrawString_EN(r->x + 6, r->y + 40, b, &Font24, SCALE3_YELLOW, SCALE3_BLACK);
        static const char *wx_names[] = {"SUNNY", "CLOUDY", "RAIN", "STORM", "SNOW", "FOG"};
        if (cond < 0 || cond > 5) cond = 1;
        Paint_DrawString_EN(r->x + 6, r->y + 68, (char *)wx_names[cond], &Font12,
                            SCALE3_CYAN, SCALE3_BLACK);
        draw_weather_icon(r->x + r->w - 32, r->y + 38, cond, g_frame);
    } else {
        Paint_DrawString_EN(r->x + 6, r->y + 44, "FETCHING", &Font12,
                            SCALE3_BLUE, SCALE3_BLACK);
    }
    Paint_DrawString_EN(r->x + 6, r->y + r->h - 12,
                        (char *)ionity_data_get("wx.place", "CENTURION ZA"),
                        &Font8, SCALE3_GREEN, SCALE3_BLACK);
}

static void panel_game(const ionity_rect_t *r) {
    static int16_t last_x = -1, last_y = -1, last_w = -1, last_h = -1;
    if (r->x != last_x || r->y != last_y || r->w != last_w || r->h != last_h) {
        ionity_game_init(r->x, r->y, r->w, r->h);
        last_x = r->x; last_y = r->y; last_w = r->w; last_h = r->h;
    }
    ionity_game_tick(g_frame);
}

static void panel_verse(const ionity_rect_t *r) {
    panel_frame(r, "TODAY'S VERSE", SCALE3_YELLOW, SCALE3_RED);
    const char *t = ionity_data_get("verse.text", verse_text);
    const char *ref = ionity_data_get("verse.ref", verse_ref);
    draw_wrapped(r->x + 7, r->y + 28, r->y + r->h - 22, r->w - 14, t, SCALE3_WHITE);
    Paint_DrawString_EN(r->x + 7, r->y + r->h - 18, (char *)ref, &Font12,
                        SCALE3_YELLOW, SCALE3_BLACK);
}

static void panel_qr(const ionity_rect_t *r) {
    panel_frame(r, "BROADCAST", SCALE3_GREEN, SCALE3_RED);
    ionity_wifi_info_t wi = ionity_wifi_get_info();
    if (wi.state == IONITY_WIFI_CONNECTED && wi.ip_addr[0]) {
        char url[40];
        snprintf(url, sizeof(url), "http://%s/", wi.ip_addr);
        int span = 25 * 4 + 2 * 8;               /* 25 modules x4 px + quiet zone */
        ionity_qr_draw(url, r->x + (r->w - span) / 2, r->y + 28, 4);
        int tx = r->x + (r->w - (int)strlen(url) * CW12) / 2;
        Paint_DrawString_EN(tx < r->x + 4 ? r->x + 4 : tx, r->y + 28 + span + 6, url,
                            &Font12, SCALE3_CYAN, SCALE3_BLACK);
        Paint_DrawString_EN(r->x + 7, r->y + r->h - 14, "Scan to post to this screen",
                            &Font8, SCALE3_WHITE, SCALE3_BLACK);
    } else {
        Paint_DrawString_EN(r->x + 7, r->y + 60, "Waiting for WiFi...", &Font12,
                            SCALE3_YELLOW, SCALE3_BLACK);
    }
}

/* Alternates verse and QR every ~12 s. */
static void panel_rotate(const ionity_rect_t *r) {
    if (((g_frame / 720) % 2) == 0) panel_verse(r);
    else                            panel_qr(r);
}

/* Fully server-driven panel: whatever the server puts in text.title/text.body. */
static void panel_text(const ionity_rect_t *r) {
    const char *title = ionity_data_get("text.title", "EDGE-VIEW");
    panel_frame(r, title, SCALE3_YELLOW, SCALE3_WHITE);
    const char *body = ionity_data_get("text.body", "Waiting for server content...");
    draw_wrapped(r->x + 7, r->y + 28, r->y + r->h - 6, r->w - 14, body, SCALE3_WHITE);
}

static void panel_logo(const ionity_rect_t *r) {
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, SCALE3_BLACK);
    int scale = r->h / 32;
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;
    ionity_draw_logo(r->x + (r->w - 24 * scale) / 2, r->y + (r->h - 24 * scale) / 2, scale);
}

static void panel_marquee(const ionity_rect_t *r) {
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, SCALE3_BLACK);
    ionity_draw_hline(r->x, r->x + r->w - 1, r->y, SCALE3_GREEN);
    ionity_draw_hline(r->x, r->x + r->w - 1, r->y + r->h - 1, SCALE3_GREEN);
    Paint_DrawString_EN(r->x + 5, r->y + 9, "MSG", &Font12, SCALE3_GREEN, SCALE3_BLACK);

    const char *s = ionity_data_get("marquee", marquee_text);
    if (!s[0]) return;
    draw_scroller(r->x + 40, r->x + r->w - 6, r->y + 9, s, marquee_offset,
                  &Font12, CW12, SCALE3_YELLOW);
    marquee_offset += 2;
    if (marquee_offset > (int)strlen(s) * CW12 + 80)
        marquee_offset = -(r->w - 40);
}

static void panel_news(const ionity_rect_t *r) {
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, SCALE3_BLACK);
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + 1, SCALE3_RED);
    ionity_fill_rect_fast(r->x, r->y + r->h - 2, r->x + r->w - 1, r->y + r->h - 1, SCALE3_RED);
    ionity_fill_rect_fast(r->x, r->y + 2, r->x + 76, r->y + r->h - 3, SCALE3_RED);
    Paint_DrawString_EN(r->x + 6, r->y + 6, "AI", &Font16, SCALE3_WHITE, SCALE3_RED);
    Paint_DrawString_EN(r->x + 6, r->y + 23, "NEWS", &Font12, SCALE3_YELLOW, SCALE3_RED);

    const char *n = news_current();
    draw_scroller(r->x + 86, r->x + r->w - 8, r->y + 13, n, news_scroll,
                  &Font16, CW16, SCALE3_YELLOW);
    news_scroll += 2;
    if (news_scroll > (int)strlen(n) * CW16 + 200) {
        news_scroll = 0;
        news_idx++;
        news_idx %= (news_count == 0) ? (int)NUM_FALLBACK : news_count;
    }
}

/* Newest at the bottom, older lines scrolling up out of the frame. */
static void panel_chat(const ionity_rect_t *r) {
    panel_frame(r, "CHAT", SCALE3_GREEN, SCALE3_GREEN);

    if (chat_count == 0) {
        Paint_DrawString_EN(r->x + 7, r->y + 28, "Say something from the console",
                            &Font12, SCALE3_CYAN, SCALE3_BLACK);
        return;
    }

    int bottom = r->y + r->h - 6;
    int y = r->y + 26;
    int max_lines = (bottom - y) / 14;
    if (max_lines < 1) return;

    int show = chat_count < max_lines ? chat_count : max_lines;
    int first = (chat_write - show + CHAT_RING) % CHAT_RING;

    for (int i = 0; i < show && y < bottom; i++) {
        int idx = (first + i) % CHAT_RING;
        bool mine = (strcmp(chat_who[idx], "ionity-ai") == 0 || strcmp(chat_who[idx], "ai") == 0);

        char who[CHAT_WHO + 2];
        snprintf(who, sizeof(who), "%s:", chat_who[idx]);
        Paint_DrawString_EN(r->x + 7, y, who, &Font12,
                            mine ? SCALE3_MAGENTA : SCALE3_CYAN, SCALE3_BLACK);

        int indent = (int)strlen(who) * CW12 + 6;
        y = draw_wrapped(r->x + 7 + indent, y, bottom, r->w - 14 - indent,
                         chat_msg[idx], mine ? SCALE3_WHITE : SCALE3_YELLOW);
    }
}

static void panel_stats(const ionity_rect_t *r) {
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, SCALE3_BLACK);
    ionity_wifi_info_t wi = ionity_wifi_get_info();
    char b[96];
    snprintf(b, sizeof(b), "POST http://%s/  |  FPS %lu  |  BEACON UDP %d  |  %s",
             wi.ip_addr[0] ? wi.ip_addr : "---.---.---.---",
             (unsigned long)fps_value, IONITY_BEACON_PORT,
             ionity_data_have_server() ? "SERVER-DRIVEN" : "LOCAL");
    Paint_DrawString_EN(r->x + 5, r->y + 4, b, &Font12, SCALE3_CYAN, SCALE3_BLACK);
}

static void panel_footer(const ionity_rect_t *r) {
    ionity_fill_rect_fast(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, SCALE3_BLACK);
    ionity_draw_hline(r->x, r->x + r->w - 1, r->y, SCALE3_GREEN);
    ionity_draw_hline(r->x, r->x + r->w - 1, r->y + r->h - 1, SCALE3_GREEN);
    Paint_DrawString_EN(r->x + 5, r->y + 8,
                        (char *)ionity_data_get("foot.left",
                            "IONITY GLOBAL (PTY) LTD - ionity.co.za"),
                        &Font12, SCALE3_GREEN, SCALE3_BLACK);
    char b[48];
    uint32_t s = to_ms_since_boot(get_absolute_time()) / 1000;
    snprintf(b, sizeof(b), "EDGE-VIEW v2.1 | UP %02lu:%02lu:%02lu",
             (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60),
             (unsigned long)(s % 60));
    Paint_DrawString_EN(r->x + r->w - 5 - (int)strlen(b) * CW12, r->y + 8, b,
                        &Font12, SCALE3_WHITE, SCALE3_BLACK);
}

/* =================== Layout =================== */

static void register_panels(void) {
    ionity_scene_register("header",  panel_header);
    ionity_scene_register("clock",   panel_clock);
    ionity_scene_register("network", panel_network);
    ionity_scene_register("feed",    panel_feed);
    ionity_scene_register("weather", panel_weather);
    ionity_scene_register("game",    panel_game);
    ionity_scene_register("verse",   panel_verse);
    ionity_scene_register("qr",      panel_qr);
    ionity_scene_register("rotate",  panel_rotate);
    ionity_scene_register("text",    panel_text);
    ionity_scene_register("logo",    panel_logo);
    ionity_scene_register("marquee", panel_marquee);
    ionity_scene_register("news",    panel_news);
    ionity_scene_register("chat",    panel_chat);
    ionity_scene_register("stats",   panel_stats);
    ionity_scene_register("footer",  panel_footer);
}

static void apply_default_layout(void) {
    ionity_scene_set_rect(IONITY_SLOT_HEADER,    0,   0, FRAME_WIDTH,      35);
    ionity_scene_set_rect(IONITY_SLOT_INFO_A,    4,  38, 240,              92);
    ionity_scene_set_rect(IONITY_SLOT_INFO_B,  248,  38, 246,              92);
    ionity_scene_set_rect(IONITY_SLOT_INFO_C,  498,  38, FRAME_WIDTH - 502, 92);
    ionity_scene_set_rect(IONITY_SLOT_STAGE,     4, 134, 410,             212);
    ionity_scene_set_rect(IONITY_SLOT_SIDE,    418, 134, FRAME_WIDTH - 422, 212);
    ionity_scene_set_rect(IONITY_SLOT_MARQUEE,   0, 350, FRAME_WIDTH,      30);
    ionity_scene_set_rect(IONITY_SLOT_TICKER,    0, 383, FRAME_WIDTH,      42);
    ionity_scene_set_rect(IONITY_SLOT_STATS,     0, 429, FRAME_WIDTH,      20);
    ionity_scene_set_rect(IONITY_SLOT_FOOTER,    0, 453, FRAME_WIDTH,      27);

    ionity_scene_bind(IONITY_SLOT_HEADER,  "header");
    ionity_scene_bind(IONITY_SLOT_INFO_A,  "network");
    ionity_scene_bind(IONITY_SLOT_INFO_B,  "feed");
    ionity_scene_bind(IONITY_SLOT_INFO_C,  "weather");
    ionity_scene_bind(IONITY_SLOT_STAGE,   "game");
    ionity_scene_bind(IONITY_SLOT_SIDE,    "rotate");
    ionity_scene_bind(IONITY_SLOT_MARQUEE, "marquee");
    ionity_scene_bind(IONITY_SLOT_TICKER,  "news");
    ionity_scene_bind(IONITY_SLOT_STATS,   "stats");
    ionity_scene_bind(IONITY_SLOT_FOOTER,  "footer");
}

/* =================== Command handling =================== */

static void handle_http_message(const char *text, const char *author) {
    snprintf(marquee_text, sizeof(marquee_text), "%s: %s", author, text);
    marquee_offset = -(FRAME_WIDTH - 40);
    snprintf(ai_action, sizeof(ai_action), "MSG %.24s", author);
}

static void handle_stream_command(const ionity_command_t *cmd) {
    char reply[512];

    switch (cmd->type) {
    case IONITY_CMD_PING:
        ionity_stream_send("{\"type\":\"pong\"}\n", 16);
        break;

    case IONITY_CMD_DATA:
        if (cmd->text2[0]) {
            ionity_data_set(cmd->text2, cmd->text);
            snprintf(ai_action, sizeof(ai_action), "%.20s = %.10s", cmd->text2, cmd->text);
        }
        break;

    case IONITY_CMD_LAYOUT:
        if (ionity_scene_bind_by_name(cmd->text2, cmd->text3))
            snprintf(ai_action, sizeof(ai_action), "%.12s -> %.12s", cmd->text2, cmd->text3);
        else
            snprintf(ai_action, sizeof(ai_action), "Bad layout %.16s", cmd->text2);
        break;

    case IONITY_CMD_QUERY: {
        int n = ionity_scene_describe(reply, sizeof(reply));
        if (n > 0) ionity_stream_send(reply, (uint16_t)n);
        break;
    }

    case IONITY_CMD_CHAT:
        chat_push(cmd->text2, cmd->text);
        snprintf(ai_action, sizeof(ai_action), "Chat %.20s", cmd->text2);
        break;

    case IONITY_CMD_CHATADD:
        chat_append(cmd->text);
        break;

    case IONITY_CMD_WIFI:
        if (cmd->params[0]) {
            bool ok = ionity_wifi_clear_creds();
            snprintf(ai_action, sizeof(ai_action), "WiFi forgotten, rebooting");
            if (ok) ionity_stream_send("{\"type\":\"wifi\",\"cleared\":true}\n", 30);
            else    ionity_stream_send("{\"type\":\"wifi\",\"cleared\":false}\n", 31);
            sleep_ms(300);
            watchdog_enable(100, 1);
            while (1) { tight_loop_contents(); }
        }
        if (cmd->text2[0]) {
            ionity_wifi_save_creds(cmd->text2, cmd->text);
            snprintf(ai_action, sizeof(ai_action), "WiFi -> %.20s, rebooting", cmd->text2);
            ionity_stream_send("{\"type\":\"wifi\",\"saved\":true}\n", 28);
            sleep_ms(300);
            watchdog_enable(100, 1);
            while (1) { tight_loop_contents(); }
        }
        break;

    case IONITY_CMD_WEATHER:
        ionity_weather_set(cmd->params[0], cmd->params[1]);
        snprintf(ai_action, sizeof(ai_action), "Weather %dC", cmd->params[0]);
        break;

    case IONITY_CMD_TEXT:
        handle_http_message(cmd->text, "STUDIO");
        break;

    case IONITY_CMD_VERSE:
        if (cmd->text[0])  snprintf(verse_text, sizeof(verse_text), "%s", cmd->text);
        if (cmd->text2[0]) snprintf(verse_ref, sizeof(verse_ref), "%s", cmd->text2);
        snprintf(ai_action, sizeof(ai_action), "Verse: %.24s", verse_ref);
        break;

    case IONITY_CMD_NEWS:
        news_push(cmd->text);
        snprintf(ai_action, sizeof(ai_action), "News +1 (%d)", news_count);
        break;

    case IONITY_CMD_TIME:
        ionity_time_set_epoch((uint32_t)cmd->params[0]);
        snprintf(ai_action, sizeof(ai_action), "Time set");
        break;

    case IONITY_CMD_MODE:
        if (ionity_game_set_mode_name(cmd->text))
            snprintf(ai_action, sizeof(ai_action), "Game: %s", ionity_game_mode_name());
        break;

    case IONITY_CMD_REBOOT:
        watchdog_enable(100, 1);
        while (1) { tight_loop_contents(); }
        break;

    default:
        break;
    }
}

/* =================== Boot screens =================== */

static void draw_splash(void) {
    Paint_Clear(SCALE3_BLACK);
    ionity_fill_rect_fast(0, 0, FRAME_WIDTH - 1, 3, SCALE3_RED);
    ionity_fill_rect_fast(0, FRAME_HEIGHT - 4, FRAME_WIDTH - 1, FRAME_HEIGHT - 1, SCALE3_RED);
    ionity_draw_logo((FRAME_WIDTH - 96) / 2, 90, 4);
    Paint_DrawString_EN((FRAME_WIDTH - 17 * CW24) / 2, 210, "IO-NITY EDGE-VIEW",
                        &Font24, SCALE3_RED, SCALE3_BLACK);
    Paint_DrawString_EN((FRAME_WIDTH - 38 * CW16) / 2, 244,
                        "Ionity Global (Pty) Ltd - Centurion ZA",
                        &Font16, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN((FRAME_WIDTH - 32 * CW12) / 2, 276,
                        "Server-driven display - v2.1", &Font12,
                        SCALE3_GREEN, SCALE3_BLACK);
    Paint_DrawString_EN((FRAME_WIDTH - 21 * CW12) / 2, 300,
                        "Starting services...", &Font12, SCALE3_CYAN, SCALE3_BLACK);
}

static void draw_provisioning_screen(void) {
    Paint_Clear(SCALE3_BLACK);
    ionity_fill_rect_fast(0, 0, FRAME_WIDTH - 1, 3, SCALE3_RED);
    ionity_fill_rect_fast(0, FRAME_HEIGHT - 3, FRAME_WIDTH - 1, FRAME_HEIGHT - 1, SCALE3_RED);
    ionity_draw_logo((FRAME_WIDTH - 72) / 2, 40, 3);
    Paint_DrawString_EN(140, 130, "WIFI SETUP REQUIRED", &Font20, SCALE3_YELLOW, SCALE3_BLACK);
    Paint_DrawString_EN(80, 170, "1. Connect to Wi-Fi:", &Font16, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(100, 195, "IO-nity-Setup", &Font24, SCALE3_GREEN, SCALE3_BLACK);
    Paint_DrawString_EN(100, 225, "Password: ionity123", &Font16, SCALE3_CYAN, SCALE3_BLACK);
    Paint_DrawString_EN(80, 260, "2. Open browser:", &Font16, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(100, 285, "http://192.168.4.1", &Font24, SCALE3_YELLOW, SCALE3_BLACK);
    Paint_DrawString_EN(80, 320, "3. Enter your WiFi details", &Font16, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(80, 350, "4. Pico will reboot & connect", &Font16, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(70, 400, "Or reprovision remotely from EDGE-VIEW Studio",
                        &Font12, SCALE3_GREEN, SCALE3_BLACK);
}

void core1_main(void) {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    while (1) {
        /* Top of frame is the only safe moment to change buffers. */
        if (next_buf) { scan_buf = next_buf; next_buf = NULL; }
        const uint8_t *fb = scan_buf;
        for (uint y = 0; y < FRAME_HEIGHT; ++y) {
            uint32_t *t = 0;
            queue_remove_blocking_u32(&dvi0.q_tmds_free, &t);
            for (uint c = 0; c < 3; ++c)
                tmds_encode_1bpp((const uint32_t *)&fb[y * FRAME_WIDTH / 8 + c * PLANE_SIZE_BYTES],
                                 t + c * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD, FRAME_WIDTH);
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &t);
        }
    }
}

/* Publish the finished frame and take the buffer core 1 just released. */
static void present(void) {
    if (core1_running) {
        next_buf = draw_buf;
        while (next_buf != NULL) tight_loop_contents();
    } else {
        scan_buf = draw_buf;   /* nothing is scanning yet, swap outright */
    }
    draw_buf = (draw_buf == framebuf) ? backbuf : framebuf;
    Paint_SelectImage(draw_buf);
}

int main(void) {
    vreg_set_voltage(VREG_VSEL); sleep_ms(10);
    setup_default_uart();
    printf("IO-nity EDGE-VIEW v2.1 (server-driven)\n");

    ionity_data_init();
    ionity_scene_init();
    register_panels();
    apply_default_layout();

    printf("WiFi...\n");
    if (!ionity_wifi_init()) printf("WiFi init FAILED\n");

    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    printf("Clock: %d MHz\n", (int)(DVI_TIMING.bit_clk_khz / 1000));

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    Paint_NewImage(draw_buf, FRAME_WIDTH, FRAME_HEIGHT, 0, SCALE3_BLACK);
    Paint_SetScale(3);

    ionity_wifi_info_t w = ionity_wifi_get_info();

    if (w.state == IONITY_WIFI_AP_MODE) {
        draw_provisioning_screen();
        present();
        multicore_launch_core1(core1_main);
        core1_running = true;
        if (ionity_http_init(IONITY_HTTP_PORT))
            printf("HTTP on :%d (provisioning)\n", IONITY_HTTP_PORT);
        while (w.state == IONITY_WIFI_AP_MODE) {
            ionity_wifi_poll();
            w = ionity_wifi_get_info();
            sleep_ms(100);
        }
        watchdog_enable(100, 1);
        while (1) { tight_loop_contents(); }
    }

    draw_splash();
    present();
    multicore_launch_core1(core1_main);
    core1_running = true;
    sleep_ms(1200);

    if (ionity_stream_init(IONITY_STREAM_PORT)) {
        printf("TCP :%d\n", IONITY_STREAM_PORT);
        ionity_stream_set_handler(handle_stream_command);
    }
    if (ionity_http_init(IONITY_HTTP_PORT)) {
        printf("HTTP :%d\n", IONITY_HTTP_PORT);
        ionity_http_set_msg_handler(handle_http_message);
    }
    ionity_beacon_init("EDGE-VIEW", IONITY_STREAM_PORT);
    if (ionity_mirror_init(IONITY_MIRROR_PORT)) {
        printf("MIRROR :%d\n", IONITY_MIRROR_PORT);
    }

    if (w.state == IONITY_WIFI_CONNECTED) {
        ionity_time_init();
        ionity_weather_init();
        net_services_up = true;
    }

    ionity_game_set_mode(IONITY_GAME_PACMAN);

    printf("IP: %s\nReady. Open http://%s\n", w.ip_addr, w.ip_addr);

    while (1) {
        ionity_wifi_poll();
        ionity_wifi_led_status();
        ionity_time_poll();
        ionity_weather_poll();
        ionity_beacon_poll();

        if (!net_services_up && ionity_wifi_is_connected()) {
            ionity_time_init();
            ionity_weather_init();
            net_services_up = true;
        }

        Paint_Clear(SCALE3_BLACK);
        ionity_scene_render();
        present();
        ionity_mirror_tick(scan_buf, FRAME_WIDTH, FRAME_HEIGHT);

        fps_cnt++;
        uint32_t n = to_ms_since_boot(get_absolute_time());
        if (n - fps_last >= 1000) { fps_value = fps_cnt; fps_cnt = 0; fps_last = n; }
        g_frame++;
    }
}
