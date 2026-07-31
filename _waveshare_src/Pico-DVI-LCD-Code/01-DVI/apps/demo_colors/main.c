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

static const char *color_names[] = {
    "BLACK", "BLUE", "GREEN", "CYAN", "RED", "MAGENTA", "YELLOW", "WHITE"
};

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

    while (true) {
        for (int color = 0; color < 8; color++) {
            Paint_Clear(color);
            uint8_t fg = (color == C_BLACK) ? C_WHITE : C_BLACK;
            Paint_DrawString_EN(240, 200, color_names[color], &Font24, fg, color);
            Paint_DrawString_EN(220, 240, "Station Pico", &Font20, fg, color);
            Paint_DrawString_EN(200, 280, "Color Test Screen", &Font16, fg, color);
            sleep_ms(2000);
        }
    }
}
