/* ionity_time.c — SNTP client + local wall clock for IO-nity EDGE-VIEW.
 * Raw-UDP NTP (RFC 4330 subset) against pool.ntp.org, resync every 6 h,
 * retry with backoff on failure. NO_SYS=1 lwIP, thread-safe-background arch.
 */
#include "ionity_time.h"
#include <stdio.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#define NTP_SERVER        "pool.ntp.org"
#define NTP_PORT          123
#define NTP_MSG_LEN       48
#define NTP_DELTA         2208988800u   /* 1900 -> 1970 */
#define NTP_RESYNC_MS     (6u * 60u * 60u * 1000u)
#define NTP_RETRY_MS      15000u
#define NTP_TIMEOUT_MS    5000u

typedef enum { TS_IDLE, TS_DNS, TS_WAIT_REPLY, TS_SYNCED, TS_BACKOFF } time_state_t;

static struct udp_pcb *ntp_pcb = NULL;
static ip_addr_t ntp_addr;
static time_state_t state = TS_IDLE;
static uint32_t state_ms = 0;
static uint32_t backoff_ms = NTP_RETRY_MS;

static bool     have_time = false;
static uint32_t epoch_utc_at_ref = 0;   /* UTC epoch captured at ref_boot_ms */
static uint32_t ref_boot_ms = 0;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

void ionity_time_set_epoch(uint32_t utc_epoch) {
    epoch_utc_at_ref = utc_epoch;
    ref_boot_ms = now_ms();
    have_time = true;
    if (state != TS_WAIT_REPLY) state = TS_SYNCED;
    state_ms = now_ms();
}

bool ionity_time_valid(void) { return have_time; }

uint32_t ionity_time_local_epoch(void) {
    if (!have_time) return 0;
    return epoch_utc_at_ref + (now_ms() - ref_boot_ms) / 1000u
           + (uint32_t)IONITY_TZ_OFFSET_SECONDS;
}

void ionity_time_get_hms(int *h, int *m, int *s) {
    uint32_t e = ionity_time_local_epoch();
    if (h) *h = (int)((e / 3600u) % 24u);
    if (m) *m = (int)((e / 60u) % 60u);
    if (s) *s = (int)(e % 60u);
}

void ionity_time_clock_str(char *out, size_t len) {
    if (!have_time) { snprintf(out, len, "--:--:--"); return; }
    int h, m, s;
    ionity_time_get_hms(&h, &m, &s);
    snprintf(out, len, "%02d:%02d:%02d", h, m, s);
}

/* Civil-date algorithm (Howard Hinnant) — days since 1970 to y/m/d. */
static void civil_from_days(int32_t z, int *y_out, int *m_out, int *d_out) {
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    *y_out = (int)(y + (m <= 2));
    *m_out = (int)m;
    *d_out = (int)d;
}

void ionity_time_date_str(char *out, size_t len) {
    if (!have_time) { if (len) out[0] = '\0'; return; }
    static const char *wd[] = {"THU","FRI","SAT","SUN","MON","TUE","WED"}; /* epoch day 0 = Thu */
    static const char *mo[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                               "JUL","AUG","SEP","OCT","NOV","DEC"};
    uint32_t e = ionity_time_local_epoch();
    int32_t days = (int32_t)(e / 86400u);
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    snprintf(out, len, "%s %02d %s", wd[days % 7], d, mo[m - 1]);
}

/* ---- NTP plumbing ---- */

static void ntp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr; (void)port;
    if (p->tot_len >= NTP_MSG_LEN) {
        uint8_t mode, stratum, b[4];
        pbuf_copy_partial(p, &mode, 1, 0);
        pbuf_copy_partial(p, &stratum, 1, 1);
        mode &= 0x7;
        if ((mode == 4 || mode == 5) && stratum != 0) {
            pbuf_copy_partial(p, b, 4, 40);   /* transmit timestamp, seconds */
            uint32_t secs = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                            ((uint32_t)b[2] << 8) | b[3];
            if (secs > NTP_DELTA) {
                ionity_time_set_epoch(secs - NTP_DELTA);
                state = TS_SYNCED;
                state_ms = now_ms();
                backoff_ms = NTP_RETRY_MS;
                printf("[time] NTP synced\n");
            }
        }
    }
    pbuf_free(p);
}

static void ntp_send_request(void) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    if (!p) { state = TS_BACKOFF; state_ms = now_ms(); return; }
    uint8_t *req = (uint8_t *)p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1B;  /* LI=0 VN=3 Mode=3 (client) */
    udp_sendto(ntp_pcb, p, &ntp_addr, NTP_PORT);
    pbuf_free(p);
    state = TS_WAIT_REPLY;
    state_ms = now_ms();
}

static void ntp_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name; (void)arg;
    if (ipaddr) {
        ntp_addr = *ipaddr;
        ntp_send_request();
    } else {
        state = TS_BACKOFF;
        state_ms = now_ms();
    }
}

static void ntp_start_query(void) {
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(NTP_SERVER, &ntp_addr, ntp_dns_cb, NULL);
    if (err == ERR_OK) {
        ntp_send_request();          /* cached */
    } else if (err == ERR_INPROGRESS) {
        state = TS_DNS;
        state_ms = now_ms();
    } else {
        state = TS_BACKOFF;
        state_ms = now_ms();
    }
    cyw43_arch_lwip_end();
}

void ionity_time_init(void) {
    if (!ntp_pcb) {
        cyw43_arch_lwip_begin();
        ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (ntp_pcb) udp_recv(ntp_pcb, ntp_recv_cb, NULL);
        cyw43_arch_lwip_end();
    }
    ntp_start_query();
}

void ionity_time_poll(void) {
    if (!ntp_pcb) return;
    uint32_t elapsed = now_ms() - state_ms;
    switch (state) {
    case TS_DNS:
        if (elapsed > NTP_TIMEOUT_MS) { state = TS_BACKOFF; state_ms = now_ms(); }
        break;
    case TS_WAIT_REPLY:
        if (elapsed > NTP_TIMEOUT_MS) {
            state = TS_BACKOFF;
            state_ms = now_ms();
            if (backoff_ms < 120000u) backoff_ms *= 2;
        }
        break;
    case TS_BACKOFF:
        if (elapsed > backoff_ms) ntp_start_query();
        break;
    case TS_SYNCED:
        if (elapsed > NTP_RESYNC_MS) ntp_start_query();
        break;
    default:
        break;
    }
}
