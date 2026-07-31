#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"
#include "GUI_Paint.h"

#define FRAME_WIDTH  640
#define FRAME_HEIGHT 480
#define VREG_VSEL    VREG_VOLTAGE_1_10
#define DVI_TIMING   dvi_timing_640x480p_60hz
#define PLANE_SIZE   (FRAME_WIDTH * FRAME_HEIGHT / 8)

#define C_BLACK   0x0
#define C_BLUE    0x1
#define C_GREEN   0x2
#define C_CYAN    0x3
#define C_RED     0x4
#define C_MAGENTA 0x5
#define C_YELLOW  0x6
#define C_WHITE   0x7

uint8_t framebuf[3 * PLANE_SIZE];
struct dvi_inst dvi0;

static void draw_box(int x1, int y1, int x2, int y2, uint8_t border, uint8_t title_color) {
    Paint_DrawRectangle(x1, y1, x2, y2, border, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawLine(x1, y1 + 20, x2, y1 + 20, border, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    (void)title_color;
}

static void draw_header(void) {
    for (int x = 0; x < FRAME_WIDTH; x++) {
        for (int y = 0; y < 32; y++) {
            Paint_SetPixel(x, y, C_BLUE);
        }
    }
    Paint_DrawString_EN(200, 6, "SYSTEM INFORMATION", &Font20, C_WHITE, C_BLUE);
}

static void draw_cpu_panel(void) {
    draw_box(10, 40, 310, 180, C_GREEN, C_GREEN);
    Paint_DrawString_EN(20, 43, "CPU & Clock", &Font16, C_GREEN, C_BLACK);

    uint32_t clk_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    char buf[48];
    snprintf(buf, sizeof(buf), "System Clock: %u MHz", (unsigned)(clk_khz / 1000));
    Paint_DrawString_EN(20, 68, buf, &Font12, C_WHITE, C_BLACK);

    snprintf(buf, sizeof(buf), "DVI Clock:    %u MHz", (unsigned)(DVI_TIMING.bit_clk_khz / 1000));
    Paint_DrawString_EN(20, 88, buf, &Font12, C_CYAN, C_BLACK);

    snprintf(buf, sizeof(buf), "DVI Timing:   640x480p60");
    Paint_DrawString_EN(20, 108, buf, &Font12, C_CYAN, C_BLACK);

    snprintf(buf, sizeof(buf), "Platform:     RP2350 ARM");
    Paint_DrawString_EN(20, 128, buf, &Font12, C_YELLOW, C_BLACK);

    snprintf(buf, sizeof(buf), "Cores:        2x Cortex-M33");
    Paint_DrawString_EN(20, 148, buf, &Font12, C_WHITE, C_BLACK);

    snprintf(buf, sizeof(buf), "Core1:        DVI rendering");
    Paint_DrawString_EN(20, 163, buf, &Font12, C_GREEN, C_BLACK);
}

static void draw_memory_panel(void) {
    draw_box(320, 40, 630, 180, C_RED, C_RED);
    Paint_DrawString_EN(330, 43, "Memory", &Font16, C_RED, C_BLACK);

    Paint_DrawString_EN(330, 68, "SRAM:   520 KB", &Font12, C_WHITE, C_BLACK);
    Paint_DrawString_EN(330, 88, "Flash:  2 MB (typical)", &Font12, C_WHITE, C_BLACK);

    char buf[48];
    snprintf(buf, sizeof(buf), "FB Size: %u bytes", (unsigned)(3 * PLANE_SIZE));
    Paint_DrawString_EN(330, 108, buf, &Font12, C_YELLOW, C_BLACK);

    snprintf(buf, sizeof(buf), "FB Mode: 1bpp x3 planes");
    Paint_DrawString_EN(330, 128, buf, &Font12, C_CYAN, C_BLACK);

    Paint_DrawString_EN(330, 148, "Colors:  8 (3-bit RGB)", &Font12, C_MAGENTA, C_BLACK);
    Paint_DrawString_EN(330, 163, "PICO_COPY_TO_RAM: ON", &Font12, C_GREEN, C_BLACK);
}

static void draw_display_panel(void) {
    draw_box(10, 190, 310, 330, C_CYAN, C_CYAN);
    Paint_DrawString_EN(20, 193, "Display (Waveshare)", &Font16, C_CYAN, C_BLACK);

    Paint_DrawString_EN(20, 218, "Model:  PICO-DVI-10.1\"", &Font12, C_WHITE, C_BLACK);
    Paint_DrawString_EN(20, 238, "Panel:  IPS 1024x600", &Font12, C_WHITE, C_BLACK);
    Paint_DrawString_EN(20, 258, "Output: DVI via PIO TMDS", &Font12, C_YELLOW, C_BLACK);
    Paint_DrawString_EN(20, 278, "TMDS D0: GPIO10", &Font12, C_RED, C_BLACK);
    Paint_DrawString_EN(20, 298, "TMDS D1: GPIO12", &Font12, C_GREEN, C_BLACK);
    Paint_DrawString_EN(20, 313, "TMDS D2: GPIO14  CLK: GPIO8", &Font12, C_BLUE, C_BLACK);
}

static void draw_power_panel(void) {
    draw_box(320, 190, 630, 330, C_YELLOW, C_YELLOW);
    Paint_DrawString_EN(330, 193, "Power Setup", &Font16, C_YELLOW, C_BLACK);

    Paint_DrawString_EN(330, 218, "Pico:  micro-USB (5V)", &Font12, C_WHITE, C_BLACK);
    Paint_DrawString_EN(330, 238, "Board: USB-C (5V/2A+)", &Font12, C_WHITE, C_BLACK);
    Paint_DrawString_EN(330, 258, "Vcore: 1.10V (DVDD)", &Font12, C_CYAN, C_BLACK);

    Paint_DrawString_EN(330, 288, "WiFi:  CYW43439 2.4GHz", &Font12, C_GREEN, C_BLACK);
    Paint_DrawString_EN(330, 308, "BT:    5.2 (BLE)", &Font12, C_GREEN, C_BLACK);
}

static void draw_footer(uint32_t uptime_sec) {
    for (int x = 0; x < FRAME_WIDTH; x++) {
        Paint_SetPixel(x, FRAME_HEIGHT - 25, C_BLUE);
        Paint_SetPixel(x, FRAME_HEIGHT - 1, C_BLUE);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), " Uptime: %02u:%02u:%02u ",
             (unsigned)(uptime_sec / 3600),
             (unsigned)((uptime_sec % 3600) / 60),
             (unsigned)(uptime_sec % 60));
    Paint_DrawString_EN(10, FRAME_HEIGHT - 20, buf, &Font12, C_WHITE, C_BLUE);
    Paint_DrawString_EN(450, FRAME_HEIGHT - 20, "Station Pico v1.0", &Font12, C_CYAN, C_BLUE);
}

void core1_main(void) {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    while (true) {
        for (uint y = 0; y < FRAME_HEIGHT; ++y) {
            uint32_t *tmdsbuf = 0;
            queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
            for (uint c = 0; c < 3; ++c) {
                tmds_encode_1bpp(
                    (const uint32_t *)&framebuf[y * FRAME_WIDTH / 8 + c * PLANE_SIZE],
                    tmdsbuf + c * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD,
                    FRAME_WIDTH);
            }
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
        }
    }
}

int main(void) {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    setup_default_uart();

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    Paint_NewImage(framebuf, FRAME_WIDTH, FRAME_HEIGHT, 0, C_BLACK);
    Paint_SetScale(3);

    multicore_launch_core1(core1_main);

    uint32_t start_time = to_ms_since_boot(get_absolute_time()) / 1000;
    while (true) {
        Paint_Clear(C_BLACK);
        draw_header();
        draw_cpu_panel();
        draw_memory_panel();
        draw_display_panel();
        draw_power_panel();

        uint32_t now = to_ms_since_boot(get_absolute_time()) / 1000;
        draw_footer(now - start_time);

        sleep_ms(500);
    }
}
