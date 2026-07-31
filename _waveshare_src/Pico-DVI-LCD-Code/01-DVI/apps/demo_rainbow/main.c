#include <stdio.h>
#include <string.h>
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

static const uint8_t rainbow[] = {
    C_RED, C_RED, C_YELLOW, C_YELLOW, C_GREEN, C_GREEN,
    C_CYAN, C_CYAN, C_BLUE, C_BLUE, C_MAGENTA, C_MAGENTA
};
#define RAINBOW_LEN 12

static void draw_rainbow_bars(int offset) {
    int band_h = FRAME_HEIGHT / RAINBOW_LEN;
    for (int i = 0; i < RAINBOW_LEN; i++) {
        int idx = (i + offset) % RAINBOW_LEN;
        int y_start = i * band_h;
        int y_end = (i == RAINBOW_LEN - 1) ? FRAME_HEIGHT : (i + 1) * band_h;
        for (int y = y_start; y < y_end; y++) {
            for (int x = 0; x < 120; x++) {
                Paint_SetPixel(x, y, rainbow[idx]);
            }
        }
    }
}

static void draw_animated_stripes(int offset) {
    int stripe_w = 40;
    uint8_t colors[] = {C_RED, C_YELLOW, C_GREEN, C_CYAN, C_BLUE, C_MAGENTA};
    int num_colors = 6;
    for (int x = 120; x < FRAME_WIDTH; x++) {
        int col_idx = ((x - 120 + offset) / stripe_w) % num_colors;
        for (int y = 0; y < FRAME_HEIGHT; y++) {
            Paint_SetPixel(x, y, colors[col_idx]);
        }
    }
}

static void draw_center_overlay(void) {
    Paint_DrawRectangle(180, 170, 460, 310, C_WHITE, DOT_PIXEL_2X2, DRAW_FILL_FULL);
    Paint_DrawRectangle(182, 172, 458, 308, C_BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    Paint_DrawString_EN(210, 185, "RAINBOW DEMO", &Font20, C_BLACK, C_WHITE);
    Paint_DrawString_EN(210, 215, "Station Pico", &Font16, C_RED, C_WHITE);
    Paint_DrawString_EN(210, 240, "Pico 2W RP2350", &Font12, C_BLUE, C_WHITE);
    Paint_DrawString_EN(210, 260, "Waveshare 10.1\" DVI", &Font12, C_GREEN, C_WHITE);
    Paint_DrawString_EN(210, 280, "640 x 480 @ 60Hz", &Font12, C_MAGENTA, C_WHITE);
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

    int offset = 0;
    int rainbow_offset = 0;
    while (true) {
        Paint_Clear(C_BLACK);

        draw_rainbow_bars(rainbow_offset);
        draw_animated_stripes(offset);
        draw_center_overlay();

        offset = (offset + 2) % 240;
        if (offset % 40 == 0) rainbow_offset = (rainbow_offset + 1) % RAINBOW_LEN;

        sleep_ms(30);
    }
}
