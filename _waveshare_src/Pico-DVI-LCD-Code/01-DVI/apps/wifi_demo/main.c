#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"
#include "GUI_Paint.h"
#include "wifi_config.h"
#include "web_ui.h"

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

static volatile bool wifi_connected = false;
static volatile uint32_t frame_count = 0;
static uint32_t start_time_ms = 0;

// ── URL parameter parser ────────────────────────────────────────────────────
static int get_param_int(const char *url, const char *key, int default_val) {
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(url, search);
    if (!p) return default_val;
    p += strlen(search);
    return atoi(p);
}

static void get_param_str(const char *url, const char *key, char *out, int max_len) {
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(url, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    int i = 0;
    while (*p && *p != '&' && *p != ' ' && i < max_len - 1) {
        if (*p == '%' && *(p+1) == '2' && *(p+2) == '0') { out[i++] = ' '; p += 3; }
        else if (*p == '+') { out[i++] = ' '; p++; }
        else { out[i++] = *p++; }
    }
    out[i] = '\0';
}

// ── HTTP response helpers ───────────────────────────────────────────────────
static const char HTTP_OK[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
    "Connection: close\r\n\r\n";

static const char HTTP_OK_HTML[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n";

static void send_response(struct tcp_pcb *pcb, const char *header, const char *body) {
    tcp_write(pcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
    if (body) tcp_write(pcb, body, strlen(body), TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

// ── API handlers ────────────────────────────────────────────────────────────
static void handle_status(struct tcp_pcb *pcb) {
    ip4_addr_t *ip = (ip4_addr_t *)&(cyw43_state.netif[0].ip_addr);
    uint32_t uptime = (to_ms_since_boot(get_absolute_time()) - start_time_ms) / 1000;
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"status\":\"ok\",\"ip\":\"%d.%d.%d.%d\",\"uptime\":%u,\"frames\":%u,"
        "\"resolution\":\"%dx%d\",\"colors\":8,\"board\":\"Pico 2W RP2350\"}",
        ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip),
        (unsigned)uptime, (unsigned)frame_count,
        FRAME_WIDTH, FRAME_HEIGHT);
    send_response(pcb, HTTP_OK, buf);
}

static void handle_clear(struct tcp_pcb *pcb, const char *url) {
    int color = get_param_int(url, "color", C_BLACK);
    if (color < 0) color = 0;
    if (color > 7) color = 7;
    Paint_Clear(color);
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"action\":\"clear\",\"color\":%d}", color);
    send_response(pcb, HTTP_OK, buf);
}

static void handle_text(struct tcp_pcb *pcb, const char *url) {
    int x = get_param_int(url, "x", 0);
    int y = get_param_int(url, "y", 0);
    int size = get_param_int(url, "size", 16);
    int fg = get_param_int(url, "fg", C_WHITE);
    int bg = get_param_int(url, "bg", C_BLACK);
    char text[128];
    get_param_str(url, "text", text, sizeof(text));

    sFONT *font = &Font16;
    if (size <= 8) font = &Font8;
    else if (size <= 12) font = &Font12;
    else if (size <= 16) font = &Font16;
    else if (size <= 20) font = &Font20;
    else font = &Font24;

    if (text[0]) Paint_DrawString_EN(x, y, text, font, fg, bg);

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"action\":\"text\",\"x\":%d,\"y\":%d}", x, y);
    send_response(pcb, HTTP_OK, buf);
}

static void handle_rect(struct tcp_pcb *pcb, const char *url) {
    int x1 = get_param_int(url, "x1", 0);
    int y1 = get_param_int(url, "y1", 0);
    int x2 = get_param_int(url, "x2", 10);
    int y2 = get_param_int(url, "y2", 10);
    int color = get_param_int(url, "color", C_WHITE);
    int fill = get_param_int(url, "fill", 0);
    int thick = get_param_int(url, "thick", 2);

    Paint_DrawRectangle(x1, y1, x2, y2, color,
        (DOT_PIXEL)thick, fill ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"action\":\"rect\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}", x1, y1, x2, y2);
    send_response(pcb, HTTP_OK, buf);
}

static void handle_circle(struct tcp_pcb *pcb, const char *url) {
    int x = get_param_int(url, "x", 100);
    int y = get_param_int(url, "y", 100);
    int r = get_param_int(url, "r", 20);
    int color = get_param_int(url, "color", C_WHITE);
    int fill = get_param_int(url, "fill", 0);

    Paint_DrawCircle(x, y, r, color, DOT_PIXEL_1X1, fill ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"action\":\"circle\",\"x\":%d,\"y\":%d,\"r\":%d}", x, y, r);
    send_response(pcb, HTTP_OK, buf);
}

static void handle_line(struct tcp_pcb *pcb, const char *url) {
    int x1 = get_param_int(url, "x1", 0);
    int y1 = get_param_int(url, "y1", 0);
    int x2 = get_param_int(url, "x2", 100);
    int y2 = get_param_int(url, "y2", 100);
    int color = get_param_int(url, "color", C_WHITE);

    Paint_DrawLine(x1, y1, x2, y2, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"action\":\"line\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}", x1, y1, x2, y2);
    send_response(pcb, HTTP_OK, buf);
}

static void handle_pixel(struct tcp_pcb *pcb, const char *url) {
    int x = get_param_int(url, "x", 0);
    int y = get_param_int(url, "y", 0);
    int color = get_param_int(url, "color", C_WHITE);
    Paint_SetPixel(x, y, color);

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"action\":\"pixel\",\"x\":%d,\"y\":%d}", x, y);
    send_response(pcb, HTTP_OK, buf);
}

// ── HTTP request handler ────────────────────────────────────────────────────
static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (err != ERR_OK || p == NULL) {
        if (p) pbuf_free(p);
        tcp_close(pcb);
        return ERR_OK;
    }

    char *data = (char *)p->payload;
    int data_len = p->len;

    // Extract URL from "GET /path HTTP/1.1"
    char url[512] = {0};
    if (data_len > 4 && data[0] == 'G' && data[1] == 'E' && data[2] == 'T') {
        char *start = data + 4;
        int i = 0;
        while (*start && *start != ' ' && i < (int)sizeof(url) - 1) {
            url[i++] = *start++;
        }
        url[i] = '\0';
    }

    pbuf_free(p);

    // Route requests
    if (strstr(url, "/api/status")) {
        handle_status(pcb);
    } else if (strstr(url, "/api/clear")) {
        handle_clear(pcb, url);
    } else if (strstr(url, "/api/text")) {
        handle_text(pcb, url);
    } else if (strstr(url, "/api/rect")) {
        handle_rect(pcb, url);
    } else if (strstr(url, "/api/circle")) {
        handle_circle(pcb, url);
    } else if (strstr(url, "/api/line")) {
        handle_line(pcb, url);
    } else if (strstr(url, "/api/pixel")) {
        handle_pixel(pcb, url);
    } else if (strstr(url, "/api/colors")) {
        const char *json = "{\"colors\":[\"BLACK\",\"BLUE\",\"GREEN\",\"CYAN\",\"RED\",\"MAGENTA\",\"YELLOW\",\"WHITE\"]}";
        send_response(pcb, HTTP_OK, json);
    } else {
        // Serve web UI
        send_response(pcb, HTTP_OK_HTML, web_ui_html);
    }

    tcp_close(pcb);
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    tcp_recv(pcb, http_recv);
    return ERR_OK;
}

static void http_server_init(void) {
    struct tcp_pcb *pcb = tcp_new();
    tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, http_accept);
}

// ── DVI Core (Core1) ───────────────────────────────────────────────────────
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

// ── Main (Core0) ───────────────────────────────────────────────────────────
int main(void) {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    setup_default_uart();

    start_time_ms = to_ms_since_boot(get_absolute_time());

    // Init DVI
    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    Paint_NewImage(framebuf, FRAME_WIDTH, FRAME_HEIGHT, 0, C_BLACK);
    Paint_SetScale(3);

    // Show connecting screen
    Paint_Clear(C_BLACK);
    Paint_DrawString_EN(180, 180, "STATION PICO", &Font24, C_WHITE, C_BLACK);
    Paint_DrawString_EN(200, 220, "Connecting to WiFi...", &Font16, C_YELLOW, C_BLACK);

    // Launch DVI on Core1
    multicore_launch_core1(core1_main);

    // Init WiFi
    printf("Station Pico: Initializing WiFi...\n");
    if (cyw43_arch_init_with_country(WIFI_COUNTRY)) {
        printf("WiFi init failed!\n");
        Paint_DrawString_EN(200, 260, "WiFi INIT FAILED", &Font16, C_RED, C_BLACK);
        while (true) { sleep_ms(1000); }
    }

    cyw43_arch_enable_sta_mode();
    printf("Connecting to %s...\n", WIFI_SSID);
    Paint_DrawString_EN(200, 260, WIFI_SSID, &Font16, C_CYAN, C_BLACK);

    int result = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, WIFI_AUTH, 15000);
    if (result) {
        printf("WiFi connect failed (%d)!\n", result);
        Paint_Clear(C_BLACK);
        Paint_DrawString_EN(180, 180, "STATION PICO", &Font24, C_WHITE, C_BLACK);
        Paint_DrawString_EN(200, 220, "WiFi Connection Failed", &Font16, C_RED, C_BLACK);
        Paint_DrawString_EN(200, 260, "Check wifi_config.h", &Font12, C_YELLOW, C_BLACK);
        cyw43_arch_deinit();
        while (true) { sleep_ms(1000); }
    }

    wifi_connected = true;
    ip4_addr_t *ip = (ip4_addr_t *)&(cyw43_state.netif[0].ip_addr);
    printf("Connected! IP: %d.%d.%d.%d\n",
        ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));

    // Show connected screen briefly
    Paint_Clear(C_BLACK);
    Paint_DrawString_EN(180, 160, "STATION PICO", &Font24, C_WHITE, C_BLACK);
    Paint_DrawString_EN(200, 200, "WiFi Connected!", &Font16, C_GREEN, C_BLACK);
    char ip_str[64];
    snprintf(ip_str, sizeof(ip_str), "IP: %d.%d.%d.%d",
        ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));
    Paint_DrawString_EN(200, 240, ip_str, &Font16, C_CYAN, C_BLACK);
    Paint_DrawString_EN(200, 280, "Open web UI to draw", &Font12, C_YELLOW, C_BLACK);
    sleep_ms(3000);

    // Clear to canvas
    Paint_Clear(C_BLACK);

    // Start HTTP server
    http_server_init();
    printf("HTTP server on port %d\n", HTTP_PORT);

    // Main loop: poll WiFi + update frame counter
    while (true) {
        cyw43_arch_poll();
        frame_count++;
        sleep_ms(10);
    }
}
