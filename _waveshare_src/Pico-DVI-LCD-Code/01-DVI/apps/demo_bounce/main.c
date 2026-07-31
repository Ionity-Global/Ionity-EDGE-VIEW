#include <stdio.h>
#include <stdlib.h>
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

#define C_BLACK  0x0
#define C_BLUE   0x1
#define C_GREEN  0x2
#define C_CYAN   0x3
#define C_RED    0x4
#define C_MAGENTA 0x5
#define C_YELLOW 0x6
#define C_WHITE  0x7

uint8_t framebuf[3 * PLANE_SIZE];
struct dvi_inst dvi0;

#define NUM_BALLS 12

typedef struct {
    int x, y;
    int dx, dy;
    int radius;
    uint8_t color;
} Ball;

static Ball balls[NUM_BALLS];

static void init_balls(void) {
    int colors[] = {C_RED, C_GREEN, C_BLUE, C_CYAN, C_MAGENTA, C_YELLOW, C_WHITE};
    int radii[]  = {8, 10, 12, 14, 16, 18, 20, 22, 25, 28, 30, 35};
    int speeds[] = {1, 2, 3, -1, -2, -3};
    for (int i = 0; i < NUM_BALLS; i++) {
        balls[i].x = 50 + (i * 47) % (FRAME_WIDTH - 100);
        balls[i].y = 50 + (i * 73) % (FRAME_HEIGHT - 100);
        balls[i].dx = speeds[i % 6];
        balls[i].dy = speeds[(i + 3) % 6];
        balls[i].radius = radii[i];
        balls[i].color = colors[i % 7];
    }
}

static void update_balls(void) {
    for (int i = 0; i < NUM_BALLS; i++) {
        balls[i].x += balls[i].dx;
        balls[i].y += balls[i].dy;
        if (balls[i].x - balls[i].radius < 0 || balls[i].x + balls[i].radius >= FRAME_WIDTH)
            balls[i].dx = -balls[i].dx;
        if (balls[i].y - balls[i].radius < 0 || balls[i].y + balls[i].radius >= FRAME_HEIGHT)
            balls[i].dy = -balls[i].dy;
    }
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

    init_balls();
    multicore_launch_core1(core1_main);

    uint32_t frame = 0;
    while (true) {
        Paint_Clear(C_BLACK);

        Paint_DrawRectangle(0, 0, FRAME_WIDTH - 1, FRAME_HEIGHT - 1, C_WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

        for (int i = 0; i < NUM_BALLS; i++) {
            Paint_DrawCircle(balls[i].x, balls[i].y, balls[i].radius, balls[i].color, DOT_PIXEL_2X2, DRAW_FILL_FULL);
        }

        Paint_DrawString_EN(10, 5, "BOUNCE", &Font16, C_WHITE, C_BLACK);
        Paint_DrawString_EN(550, 5, "640x480", &Font12, C_CYAN, C_BLACK);

        update_balls();
        frame++;
        sleep_ms(16);
    }
}
