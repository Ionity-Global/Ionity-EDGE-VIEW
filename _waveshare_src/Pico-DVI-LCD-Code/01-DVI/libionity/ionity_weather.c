/* ionity_weather.c — current weather for the EDGE-VIEW header panel.
 * Plain-HTTP GET to api.open-meteo.com (no TLS available on Pico lwIP here);
 * tiny strstr JSON scan — no JSON library. Studio app can override via the
 * WEATHER stream command at any time.
 */
#include "ionity_weather.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#define WX_HOST        "api.open-meteo.com"
#define WX_PATH        "/v1/forecast?latitude=" IONITY_WEATHER_LAT \
                       "&longitude=" IONITY_WEATHER_LON "&current_weather=true"
#define WX_REFRESH_MS  (15u * 60u * 1000u)
#define WX_RETRY_MS    60000u
#define WX_TIMEOUT_MS  10000u
#define WX_BUF_LEN     1024

typedef enum { WX_IDLE, WX_DNS, WX_CONNECTING, WX_RECEIVING, WX_DONE, WX_BACKOFF } wx_state_t;

static wx_state_t state = WX_IDLE;
static uint32_t state_ms = 0;
static struct tcp_pcb *wx_pcb = NULL;
static ip_addr_t wx_addr;
static char wx_buf[WX_BUF_LEN];
static uint16_t wx_len = 0;

static bool have_wx = false;
static int cur_temp = 0;
static int cur_cond = IONITY_WX_SUNNY;
static bool pushed_override = false;   /* app-pushed value wins until next fetch OK */

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool ionity_weather_valid(void) { return have_wx; }
int ionity_weather_temp_c(void) { return cur_temp; }
int ionity_weather_condition(void) { return cur_cond; }

void ionity_weather_set(int temp_c, int condition) {
    cur_temp = temp_c;
    if (condition >= 0 && condition <= IONITY_WX_FOG) cur_cond = condition;
    have_wx = true;
    pushed_override = true;
}

/* Open-Meteo WMO weather code -> 6-icon condition. */
static int wmo_to_condition(int code) {
    if (code == 0) return IONITY_WX_SUNNY;
    if (code <= 3) return IONITY_WX_CLOUDY;
    if (code == 45 || code == 48) return IONITY_WX_FOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return IONITY_WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return IONITY_WX_SNOW;
    if (code >= 95) return IONITY_WX_STORM;
    return IONITY_WX_CLOUDY;
}

static void wx_parse_response(void) {
    wx_buf[wx_len] = '\0';
    const char *t = strstr(wx_buf, "\"temperature\":");
    const char *w = strstr(wx_buf, "\"weathercode\":");
    if (t) {
        cur_temp = (int)strtol(t + 14, NULL, 10);
        if (w) cur_cond = wmo_to_condition((int)strtol(w + 14, NULL, 10));
        have_wx = true;
        pushed_override = false;
        printf("[wx] %dC cond=%d\n", cur_temp, cur_cond);
    }
}

static void wx_close(bool ok) {
    if (wx_pcb) {
        tcp_arg(wx_pcb, NULL);
        tcp_recv(wx_pcb, NULL);
        tcp_err(wx_pcb, NULL);
        if (tcp_close(wx_pcb) != ERR_OK) tcp_abort(wx_pcb);
        wx_pcb = NULL;
    }
    state = ok ? WX_DONE : WX_BACKOFF;
    state_ms = now_ms();
}

static err_t wx_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) {                       /* remote closed — parse what we have */
        wx_parse_response();
        wx_close(have_wx && !pushed_override);
        return ERR_OK;
    }
    uint16_t copy = p->tot_len;
    if (copy > WX_BUF_LEN - 1 - wx_len) copy = WX_BUF_LEN - 1 - wx_len;
    if (copy) {
        pbuf_copy_partial(p, wx_buf + wx_len, copy, 0);
        wx_len += copy;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void wx_err_cb(void *arg, err_t err) {
    (void)arg; (void)err;
    wx_pcb = NULL;                  /* pcb already freed by lwIP */
    state = WX_BACKOFF;
    state_ms = now_ms();
}

static err_t wx_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) { wx_close(false); return err; }
    static const char req[] =
        "GET " WX_PATH " HTTP/1.1\r\n"
        "Host: " WX_HOST "\r\n"
        "User-Agent: ionity-edgeview/2.0\r\n"
        "Connection: close\r\n\r\n";
    wx_len = 0;
    tcp_write(pcb, req, sizeof(req) - 1, 0);
    tcp_output(pcb);
    state = WX_RECEIVING;
    state_ms = now_ms();
    return ERR_OK;
}

static void wx_connect(void) {
    wx_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!wx_pcb) { state = WX_BACKOFF; state_ms = now_ms(); return; }
    tcp_recv(wx_pcb, wx_recv_cb);
    tcp_err(wx_pcb, wx_err_cb);
    if (tcp_connect(wx_pcb, &wx_addr, 80, wx_connected_cb) != ERR_OK) {
        wx_close(false);
        return;
    }
    state = WX_CONNECTING;
    state_ms = now_ms();
}

static void wx_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name; (void)arg;
    if (ipaddr) { wx_addr = *ipaddr; wx_connect(); }
    else { state = WX_BACKOFF; state_ms = now_ms(); }
}

static void wx_start_fetch(void) {
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(WX_HOST, &wx_addr, wx_dns_cb, NULL);
    if (err == ERR_OK) wx_connect();
    else if (err == ERR_INPROGRESS) { state = WX_DNS; state_ms = now_ms(); }
    else { state = WX_BACKOFF; state_ms = now_ms(); }
    cyw43_arch_lwip_end();
}

void ionity_weather_init(void) {
    wx_start_fetch();
}

void ionity_weather_poll(void) {
    uint32_t elapsed = now_ms() - state_ms;
    switch (state) {
    case WX_DNS:
    case WX_CONNECTING:
    case WX_RECEIVING:
        if (elapsed > WX_TIMEOUT_MS) { cyw43_arch_lwip_begin(); wx_close(false); cyw43_arch_lwip_end(); }
        break;
    case WX_DONE:
        if (elapsed > WX_REFRESH_MS) wx_start_fetch();
        break;
    case WX_BACKOFF:
        if (elapsed > WX_RETRY_MS) wx_start_fetch();
        break;
    default:
        break;
    }
}
