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

#define MARQUEE_Y      230
#define MARQUEE_HEIGHT 40
#define MARQUEE_SCROLL_SPEED 2

uint8_t framebuf[3 * PLANE_SIZE_BYTES];
struct dvi_inst dvi0;

/* ---- Sprites ---- */
typedef struct {
    int16_t x, y;
    int16_t dx, dy;
    int16_t w, h;
    uint8_t color;
} Sprite;

static Sprite sprites[] = {
    { 50,  60,  2,  3, 40, 30, SCALE3_RED },
    { 300, 200, -3,  2, 50, 25, SCALE3_GREEN },
    { 500, 100,  1, -2, 35, 35, SCALE3_BLUE },
    { 150, 350, -2, -1, 45, 20, SCALE3_YELLOW },
    { 400, 300,  3, -3, 30, 30, SCALE3_CYAN },
    { 250, 150, -1,  2, 55, 28, SCALE3_RED | SCALE3_BLUE },
};
#define NUM_SPRITES (sizeof(sprites) / sizeof(sprites[0]))

/* ---- State ---- */
static char ai_last_msg[128] = "Waiting for AI...";
static int weather_temp = 22;
static int weather_cond = 0;
static bool ai_connected = false;
static uint32_t fps_value = 0;
static uint32_t fps_last_time = 0;
static uint32_t fps_frame_count = 0;

/* ---- Marquee state ---- */
static char marquee_text[IONITY_HTTP_MAX_MSG_LEN] = "Welcome to IO-nity Station Pico! Open http://<ip> to send messages...";
static int marquee_offset = 0;
static int marquee_text_width = 0;

static void update_sprites(void) {
    for (int i = 0; i < NUM_SPRITES; i++) {
        sprites[i].x += sprites[i].dx;
        sprites[i].y += sprites[i].dy;
        if (sprites[i].x < 0 || sprites[i].x + sprites[i].w >= FRAME_WIDTH)
            sprites[i].dx = -sprites[i].dx;
        if (sprites[i].y < IONITY_HEADER_HEIGHT + 10 ||
            sprites[i].y + sprites[i].h >= FRAME_HEIGHT - IONITY_FOOTER_HEIGHT - 10)
            sprites[i].dy = -sprites[i].dy;
    }
}

static void draw_marquee(void) {
    /* Background bar */
    ionity_fill_rect_fast(0, MARQUEE_Y, FRAME_WIDTH - 1, MARQUEE_Y + MARQUEE_HEIGHT - 1, SCALE3_BLACK);
    ionity_draw_hline(0, FRAME_WIDTH - 1, MARQUEE_Y, SCALE3_RED);
    ionity_draw_hline(0, FRAME_WIDTH - 1, MARQUEE_Y + MARQUEE_HEIGHT - 1, SCALE3_RED);

    /* Label */
    Paint_DrawString_EN(5, MARQUEE_Y + 4, "MSG:", &Font12, SCALE3_RED, SCALE3_BLACK);

    /* Scroll the text */
    if (marquee_text[0]) {
        int text_len = strlen(marquee_text);
        marquee_text_width = text_len * 8;  /* ~8px per char for Font12 */

        int x = 40 - marquee_offset;
        int max_x = FRAME_WIDTH - 10;
        int clip_start = 45;

        /* Draw visible portion */
        for (int i = 0; i < text_len; i++) {
            int char_x = 40 + i * 8 - marquee_offset;
            if (char_x < clip_start) continue;
            if (char_x > max_x) break;

            char ch[2] = { marquee_text[i], '\0' };
            Paint_DrawString_EN(char_x, MARQUEE_Y + 4, ch, &Font12, SCALE3_YELLOW, SCALE3_BLACK);
        }

        marquee_offset += MARQUEE_SCROLL_SPEED;
        if (marquee_offset > marquee_text_width + 40) {
            marquee_offset = -FRAME_WIDTH;
        }
    }
}

static void draw_info_panel(void) {
    ionity_draw_panel(10, 50, 300, 170, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(20, 55, "Hardware Info", &Font16, SCALE3_WHITE, SCALE3_BLACK);
    ionity_draw_hline(20, 290, 73, SCALE3_WHITE);

    Paint_DrawString_EN(20, 80,  "Board: Pico 2W", &Font12, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(20, 95,  "Chip:  RP2350", &Font12, SCALE3_GREEN, SCALE3_BLACK);
    Paint_DrawString_EN(20, 110, "Display: 10.1\" IPS", &Font12, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(20, 125, "Interface: DVI (PIO)", &Font12, SCALE3_BLUE, SCALE3_BLACK);
    Paint_DrawString_EN(20, 140, "Panel: 1024x600 IPS", &Font12, SCALE3_YELLOW, SCALE3_BLACK);
    Paint_DrawString_EN(20, 155, "DVI Mode: 640x480p60", &Font12, SCALE3_WHITE, SCALE3_BLACK);

    ionity_draw_panel(310, 50, 630, 170, SCALE3_WHITE, SCALE3_BLACK);
    Paint_DrawString_EN(320, 55, "Network", &Font16, SCALE3_WHITE, SCALE3_BLACK);
    ionity_draw_hline(320, 620, 73, SCALE3_WHITE);

    ionity_wifi_info_t wifi = ionity_wifi_get_info();
    char buf[64];
    snprintf(buf, sizeof(buf), "WiFi: %s", wifi.ssid);
    Paint_DrawString_EN(320, 80, buf, &Font12, SCALE3_GREEN, SCALE3_BLACK);

    snprintf(buf, sizeof(buf), "IP: %s", wifi.ip_addr);
    Paint_DrawString_EN(320, 95, buf, &Font12, SCALE3_WHITE, SCALE3_BLACK);

    snprintf(buf, sizeof(buf), "State: %s",
             wifi.state == IONITY_WIFI_CONNECTED ? "Connected" :
             wifi.state == IONITY_WIFI_CONNECTING ? "Connecting..." :
             wifi.state == IONITY_WIFI_ERROR ? "Error" : "Disconnected");
    Paint_DrawString_EN(320, 110, buf, &Font12,
                        wifi.state == IONITY_WIFI_CONNECTED ? SCALE3_GREEN : SCALE3_RED, SCALE3_BLACK);

    snprintf(buf, sizeof(buf), "RSSI: %d dBm", wifi.rssi);
    Paint_DrawString_EN(320, 125, buf, &Font12, SCALE3_CYAN, SCALE3_BLACK);

    snprintf(buf, sizeof(buf), "HTTP: %s",
             ionity_http_msg_count() > 0 ? "Active" : "Ready");
    Paint_DrawString_EN(320, 140, buf, &Font12,
                        ionity_http_msg_count() > 0 ? SCALE3_GREEN : SCALE3_YELLOW, SCALE3_BLACK);

    snprintf(buf, sizeof(buf), "Msgs: %d", ionity_http_msg_count());
    Paint_DrawString_EN(320, 155, buf, &Font12, SCALE3_WHITE, SCALE3_BLACK);
}

static void handle_http_message(const char *text, const char *author) {
    snprintf(marquee_text, sizeof(marquee_text), "%s: %s", author, text);
    marquee_offset = -FRAME_WIDTH;
    snprintf(ai_last_msg, sizeof(ai_last_msg), "%s: %.60s", author, text);
}

static void handle_stream_command(const ionity_command_t *cmd) {
    switch (cmd->type) {
    case IONITY_CMD_PING:
        ionity_stream_send("{\"type\":\"pong\"}\n", 17);
        break;
    case IONITY_CMD_WEATHER:
        weather_temp = cmd->params[0];
        weather_cond = cmd->params[1];
        snprintf(ai_last_msg, sizeof(ai_last_msg), "Weather: %dC cond=%d", weather_temp, weather_cond);
        break;
    case IONITY_CMD_TEXT:
        handle_http_message(cmd->text, "AI");
        break;
    case IONITY_CMD_REBOOT:
        watchdog_enable(100, 1);
        while (1) tight_loop_contents();
        break;
    case IONITY_CMD_SPRITE:
        if (cmd->params[0] >= 0 && cmd->params[0] < (int)NUM_SPRITES) {
            sprites[cmd->params[0]].x = cmd->params[1];
            sprites[cmd->params[0]].y = cmd->params[2];
            sprites[cmd->params[0]].dx = cmd->params[3];
            sprites[cmd->params[0]].dy = cmd->params[4];
        }
        snprintf(ai_last_msg, sizeof(ai_last_msg), "Sprite %d moved", cmd->params[0]);
        break;
    case IONITY_CMD_SET_BRIGHTNESS:
        snprintf(ai_last_msg, sizeof(ai_last_msg), "Brightness: %d", cmd->params[0]);
        break;
    default:
        break;
    }
    ai_connected = ionity_stream_is_connected();
}

/* ---- Core 1: DVI TMDS output ---- */
void core1_main(void) {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    while (true) {
        for (uint y = 0; y < FRAME_HEIGHT; ++y) {
            uint32_t *tmdsbuf = 0;
            queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
            for (uint component = 0; component < 3; ++component) {
                tmds_encode_1bpp(
                    (const uint32_t *)&framebuf[y * FRAME_WIDTH / 8 + component * PLANE_SIZE_BYTES],
                    tmdsbuf + component * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD,
                    FRAME_WIDTH);
            }
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
        }
    }
}

/* ---- Core 0: Main ---- */
int main(void) {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    setup_default_uart();

    printf("Station Pico - IO-nity SDK starting...\n");

    /* ---- Set DVI clock FIRST (252 MHz) ---- *
     * Must happen before WiFi init so the CYW43 PIO
     * clock divider is calculated for the final
     * system clock frequency.                   */
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    printf("System clock: %d MHz\n", (int)(DVI_TIMING.bit_clk_khz / 1000));

    /* ---- WiFi init at 252 MHz ---- */
    printf("Initializing WiFi...\n");
    if (ionity_wifi_init()) {
        printf("WiFi driver OK. Connecting to %s...\n", IONITY_WIFI_SSID);
        if (ionity_wifi_connect_default()) {
            ionity_wifi_info_t info = ionity_wifi_get_info();
            printf("WiFi connected! IP: %s (took %lu ms)\n",
                   info.ip_addr, info.connect_time_ms);
        } else {
            printf("WiFi connection FAILED. Check SSID/password.\n");
        }
    } else {
        printf("WiFi init FAILED.\n");
    }

    /* ---- TCP stream server (port 4242) ---- */
    if (ionity_stream_init(IONITY_STREAM_PORT)) {
        printf("Stream server on port %d\n", IONITY_STREAM_PORT);
        ionity_stream_set_handler(handle_stream_command);
    }

    /* ---- HTTP server (port 80) ---- */
    if (ionity_http_init(IONITY_HTTP_PORT)) {
        printf("HTTP server on port %d\n", IONITY_HTTP_PORT);
        ionity_http_set_msg_handler(handle_http_message);
    }

    /* ---- DVI init ---- */
    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    Paint_NewImage(framebuf, FRAME_WIDTH, FRAME_HEIGHT, 0, SCALE3_BLACK);
    Paint_SetScale(3);

    /* ---- Launch DVI on core 1 ---- */
    multicore_launch_core1(core1_main);

    uint32_t frame = 0;
    while (true) {
        /* Service WiFi + lwIP */
        ionity_wifi_poll();
        ionity_wifi_led_status();

        /* Render frame */
        Paint_Clear(SCALE3_BLACK);

        /* IO-nity header */
        char header_sub[64];
        snprintf(header_sub, sizeof(header_sub), "v1.0 | SDK-Ionity");
        ionity_draw_header(FRAME_WIDTH, header_sub);

        /* Info panel */
        draw_info_panel();

        /* Weather panel */
        ionity_draw_weather_panel(10, 180, 150, 90, weather_temp, weather_cond);

        /* AI status */
        ai_connected = ionity_stream_is_connected();
        ionity_draw_ai_status(10, 280, ai_connected, ai_last_msg);

        /* Scrolling message marquee */
        draw_marquee();

        /* Sprites */
        for (int i = 0; i < NUM_SPRITES; i++) {
            Paint_DrawRectangle(
                sprites[i].x, sprites[i].y,
                sprites[i].x + sprites[i].w, sprites[i].y + sprites[i].h,
                sprites[i].color, DOT_PIXEL_2X2, DRAW_FILL_FULL);
        }

        /* FPS counter */
        char fps_buf[32];
        fps_frame_count++;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - fps_last_time >= 1000) {
            fps_value = fps_frame_count;
            fps_frame_count = 0;
            fps_last_time = now_ms;
        }
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %lu", (unsigned long)fps_value);
        Paint_DrawString_EN(500, 55, fps_buf, &Font12, SCALE3_WHITE, SCALE3_BLACK);

        /* IO-nity footer */
        char footer_left[64], footer_right[64];
        snprintf(footer_left, sizeof(footer_left), "Pico 2W | %d MHz",
                 (int)(DVI_TIMING.bit_clk_khz / 1000));
        snprintf(footer_right, sizeof(footer_right), "Uptime: %02d:%02d",
                 (int)(frame / 3600), (int)((frame % 3600) / 60));
        ionity_draw_footer(FRAME_WIDTH, FRAME_HEIGHT, footer_left, footer_right);

        update_sprites();
        frame++;

        sleep_ms(16);
    }
}