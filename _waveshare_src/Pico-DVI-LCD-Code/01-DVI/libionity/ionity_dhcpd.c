/* ionity_dhcpd — minimal DHCP + DNS server for the provisioning AP.
 *
 * cyw43_arch_enable_ap_mode() brings the AP netif up on 192.168.4.1 but hands
 * out no addresses, so clients join and then hang. This gives them a lease and
 * answers every DNS query with ourselves, which also trips the phones'
 * captive-portal probe and pops the setup page automatically. */
#include "ionity_dhcpd.h"
#include <string.h>
#include <stdio.h>
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DNS_SERVER_PORT  53
#define LEASE_COUNT      8
#define LEASE_FIRST_HOST 16          /* hand out .16 … .23 */
#define DHCP_MIN_LEN     240         /* header + magic cookie */

static struct udp_pcb *dhcp_pcb = NULL;
static struct udp_pcb *dns_pcb  = NULL;
static uint8_t leases[LEASE_COUNT][6];   /* MAC per slot; zero = free */

static const uint8_t SRV_IP[4] = {192, 168, 4, 1};

/* Find or allocate a lease slot for a MAC. Returns host byte, 0 on full. */
static uint8_t lease_for(const uint8_t *mac) {
    int free_slot = -1;
    for (int i = 0; i < LEASE_COUNT; i++) {
        if (memcmp(leases[i], mac, 6) == 0) return LEASE_FIRST_HOST + i;
        static const uint8_t zero[6] = {0};
        if (free_slot < 0 && memcmp(leases[i], zero, 6) == 0) free_slot = i;
    }
    if (free_slot < 0) free_slot = 0;            /* full: recycle the oldest slot */
    memcpy(leases[free_slot], mac, 6);
    return LEASE_FIRST_HOST + free_slot;
}

static void put_opt(uint8_t **p, uint8_t code, uint8_t len, const void *data) {
    (*p)[0] = code; (*p)[1] = len;
    memcpy(*p + 2, data, len);
    *p += 2 + len;
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)addr; (void)port;
    if (p->tot_len < DHCP_MIN_LEN) { pbuf_free(p); return; }

    uint8_t req[320];
    uint16_t len = p->tot_len > sizeof(req) ? sizeof(req) : p->tot_len;
    pbuf_copy_partial(p, req, len, 0);
    pbuf_free(p);
    if (req[0] != 1) return;                     /* not BOOTREQUEST */

    /* Find option 53 (message type). */
    uint8_t msg_type = 0;
    for (uint16_t i = DHCP_MIN_LEN; i + 2 < len && req[i] != 255;) {
        if (req[i] == 0) { i++; continue; }
        if (req[i] == 53) msg_type = req[i + 2];
        i += 2 + req[i + 1];
    }
    if (msg_type != 1 && msg_type != 3) return;  /* DISCOVER or REQUEST only */

    uint8_t reply[300];
    memset(reply, 0, sizeof(reply));
    reply[0] = 2; reply[1] = 1; reply[2] = 6;    /* BOOTREPLY, ethernet */
    memcpy(reply + 4, req + 4, 4);               /* xid */
    memcpy(reply + 10, req + 10, 2);             /* flags */
    uint8_t host = lease_for(req + 28);
    reply[16] = SRV_IP[0]; reply[17] = SRV_IP[1]; reply[18] = SRV_IP[2]; reply[19] = host; /* yiaddr */
    memcpy(reply + 20, SRV_IP, 4);               /* siaddr */
    memcpy(reply + 24, req + 24, 4);             /* giaddr */
    memcpy(reply + 28, req + 28, 16);            /* chaddr */
    reply[236] = 99; reply[237] = 130; reply[238] = 83; reply[239] = 99; /* magic */

    uint8_t *opt = reply + 240;
    uint8_t t = (msg_type == 1) ? 2 : 5;         /* OFFER or ACK */
    static const uint8_t mask[4]  = {255, 255, 255, 0};
    static const uint8_t lease[4] = {0, 1, 81, 128};   /* 24 h */
    put_opt(&opt, 53, 1, &t);
    put_opt(&opt, 54, 4, SRV_IP);
    put_opt(&opt, 51, 4, lease);
    put_opt(&opt, 1, 4, mask);
    put_opt(&opt, 3, 4, SRV_IP);
    put_opt(&opt, 6, 4, SRV_IP);
    *opt++ = 255;

    struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, sizeof(reply), PBUF_RAM);
    if (!out) return;
    memcpy(out->payload, reply, sizeof(reply));
    udp_sendto(pcb, out, IP_ADDR_BROADCAST, DHCP_CLIENT_PORT);
    pbuf_free(out);
    printf("[dhcpd] %s -> 192.168.4.%d\n", t == 2 ? "OFFER" : "ACK", host);
}

/* Answer every A query with 192.168.4.1 (captive-portal style). */
static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                     const ip_addr_t *addr, u16_t port) {
    (void)arg;
    if (p->tot_len < 12 || p->tot_len > 480) { pbuf_free(p); return; }

    uint8_t q[512];
    uint16_t len = p->tot_len;
    pbuf_copy_partial(p, q, len, 0);
    pbuf_free(p);
    if (q[2] & 0x80) return;                     /* already a response */

    q[2] = 0x81; q[3] = 0x80;                    /* response, recursion available */
    q[6] = 0; q[7] = 1;                          /* one answer */
    q[8] = q[9] = q[10] = q[11] = 0;

    uint8_t ans[16] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
                       SRV_IP[0], SRV_IP[1], SRV_IP[2], SRV_IP[3]};
    if (len + sizeof(ans) > sizeof(q)) return;
    memcpy(q + len, ans, sizeof(ans));

    struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, len + sizeof(ans), PBUF_RAM);
    if (!out) return;
    memcpy(out->payload, q, len + sizeof(ans));
    udp_sendto(pcb, out, addr, port);
    pbuf_free(out);
}

bool ionity_dhcpd_start(void) {
    if (dhcp_pcb) return true;
    memset(leases, 0, sizeof(leases));

    /* lwIP calls from the main loop need the arch lock (background variant). */
    cyw43_arch_lwip_begin();

    dhcp_pcb = udp_new();
    if (!dhcp_pcb) { cyw43_arch_lwip_end(); return false; }
    ip_set_option(dhcp_pcb, SOF_BROADCAST);
    if (udp_bind(dhcp_pcb, IP_ANY_TYPE, DHCP_SERVER_PORT) != ERR_OK) {
        udp_remove(dhcp_pcb); dhcp_pcb = NULL;
        cyw43_arch_lwip_end();
        return false;
    }
    udp_recv(dhcp_pcb, dhcp_recv, NULL);

    dns_pcb = udp_new();
    if (dns_pcb) {
        if (udp_bind(dns_pcb, IP_ANY_TYPE, DNS_SERVER_PORT) == ERR_OK)
            udp_recv(dns_pcb, dns_recv, NULL);
        else { udp_remove(dns_pcb); dns_pcb = NULL; }
    }
    cyw43_arch_lwip_end();
    printf("[dhcpd] serving 192.168.4.16-23, dns wildcard -> 192.168.4.1\n");
    return true;
}

void ionity_dhcpd_stop(void) {
    cyw43_arch_lwip_begin();
    if (dhcp_pcb) { udp_remove(dhcp_pcb); dhcp_pcb = NULL; }
    if (dns_pcb)  { udp_remove(dns_pcb);  dns_pcb  = NULL; }
    cyw43_arch_lwip_end();
}
