#include "ionity_stream.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "pico/cyw43_arch.h"

static struct tcp_pcb *server_pcb = NULL;
static struct tcp_pcb *client_pcbs[IONITY_STREAM_MAX_CLIENTS] = {NULL};
static ionity_cmd_handler_t cmd_handler = NULL;
static char rx_buffer[IONITY_STREAM_MAX_CMD_LEN];
static uint16_t rx_pos = 0;

static void close_client(struct tcp_pcb *pcb) {
    for (int i = 0; i < IONITY_STREAM_MAX_CLIENTS; i++) {
        if (client_pcbs[i] == pcb) {
            client_pcbs[i] = NULL;
            break;
        }
    }
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_close(pcb);
}

static void close_client_err(void *arg, err_t err) {
    (void)err;
    close_client((struct tcp_pcb *)arg);
}

static err_t stream_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        close_client(pcb);
        return ERR_OK;
    }

    uint16_t total_len = p->tot_len;
    if (total_len + rx_pos < IONITY_STREAM_MAX_CMD_LEN) {
        pbuf_copy_partial(p, rx_buffer + rx_pos, total_len, 0);
        rx_pos += total_len;
        rx_buffer[rx_pos] = '\0';

        char *nl = strchr(rx_buffer, '\n');
        if (nl) {
            *nl = '\0';
            if (cmd_handler) {
                ionity_command_t cmd;
                ionity_stream_parse(rx_buffer, &cmd);
                cmd_handler(&cmd);
            }
            uint16_t remaining = rx_pos - (uint16_t)(nl - rx_buffer) - 1;
            if (remaining > 0) {
                memmove(rx_buffer, nl + 1, remaining);
            }
            rx_pos = remaining;
        }
    } else {
        rx_pos = 0;
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t stream_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    for (int i = 0; i < IONITY_STREAM_MAX_CLIENTS; i++) {
        if (client_pcbs[i] == NULL) {
            client_pcbs[i] = new_pcb;
            tcp_recv(new_pcb, stream_recv);
            tcp_err(new_pcb, close_client_err);

            const char *banner = "{\"type\":\"hello\",\"device\":\"EDGE-VIEW\"}\n";
            tcp_write(new_pcb, banner, strlen(banner), TCP_WRITE_FLAG_COPY);
            tcp_output(new_pcb);
            return ERR_OK;
        }
    }
    tcp_close(new_pcb);
    return ERR_OK;
}

bool ionity_stream_init(uint16_t port) {
    server_pcb = tcp_new();
    if (!server_pcb) return false;

    err_t err = tcp_bind(server_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        tcp_close(server_pcb);
        server_pcb = NULL;
        return false;
    }

    server_pcb = tcp_listen(server_pcb);
    tcp_accept(server_pcb, stream_accept);

    rx_pos = 0;
    return true;
}

void ionity_stream_set_handler(ionity_cmd_handler_t handler) {
    cmd_handler = handler;
}

void ionity_stream_send(const char *data, uint16_t len) {
    for (int i = 0; i < IONITY_STREAM_MAX_CLIENTS; i++) {
        if (client_pcbs[i]) {
            tcp_write(client_pcbs[i], data, len, TCP_WRITE_FLAG_COPY);
            tcp_output(client_pcbs[i]);
        }
    }
}

bool ionity_stream_is_connected(void) {
    for (int i = 0; i < IONITY_STREAM_MAX_CLIENTS; i++) {
        if (client_pcbs[i]) return true;
    }
    return false;
}

/* Extract a JSON string value for `key` (handles \" \\ \n escapes). */
static bool json_get_str(const char *json, const char *key,
                         char *out, size_t out_len) {
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) { if (out_len) out[0] = '\0'; return false; }
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': out[i++] = ' '; break;
            case 't': out[i++] = ' '; break;
            default:  out[i++] = *p;  break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return true;
}

const char *ionity_stream_parse(const char *input, ionity_command_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    snprintf(cmd->raw, sizeof(cmd->raw), "%s", input);

    if (strstr(input, "\"ping\"")) {
        cmd->type = IONITY_CMD_PING;
    } else if (strstr(input, "\"clear\"")) {
        cmd->type = IONITY_CMD_CLEAR;
    } else if (strstr(input, "\"rect\"")) {
        cmd->type = IONITY_CMD_RECT;
        sscanf(input, "%*[^0-9]%d,%d,%d,%d,%d",
               &cmd->params[0], &cmd->params[1], &cmd->params[2],
               &cmd->params[3], &cmd->params[4]);
    } else if (strstr(input, "\"verse\"")) {
        cmd->type = IONITY_CMD_VERSE;
        json_get_str(input, "text", cmd->text, sizeof(cmd->text));
        json_get_str(input, "ref", cmd->text2, sizeof(cmd->text2));
    } else if (strstr(input, "\"news\"")) {
        cmd->type = IONITY_CMD_NEWS;
        json_get_str(input, "text", cmd->text, sizeof(cmd->text));
    } else if (strstr(input, "\"time\"")) {
        cmd->type = IONITY_CMD_TIME;
        const char *e = strstr(input, "\"epoch\":");
        if (e) {
            unsigned long long v = strtoull(e + 8, NULL, 10);
            cmd->params[0] = (int)(v & 0xffffffffu);        /* low 32 */
            cmd->params[1] = (int)((v >> 32) & 0xffffffffu);
        }
    } else if (strstr(input, "\"mode\"")) {
        cmd->type = IONITY_CMD_MODE;
        json_get_str(input, "name", cmd->text, sizeof(cmd->text));
    } else if (strstr(input, "\"data\"")) {
        cmd->type = IONITY_CMD_DATA;
        json_get_str(input, "key", cmd->text2, sizeof(cmd->text2));
        json_get_str(input, "value", cmd->text, sizeof(cmd->text));
    } else if (strstr(input, "\"layout\"")) {
        cmd->type = IONITY_CMD_LAYOUT;
        json_get_str(input, "slot", cmd->text2, sizeof(cmd->text2));
        json_get_str(input, "panel", cmd->text3, sizeof(cmd->text3));
    } else if (strstr(input, "\"wifi\"")) {
        cmd->type = IONITY_CMD_WIFI;
        json_get_str(input, "ssid", cmd->text2, sizeof(cmd->text2));
        json_get_str(input, "pass", cmd->text, sizeof(cmd->text));
        cmd->params[0] = strstr(input, "\"reset\"") != NULL ? 1 : 0;
    } else if (strstr(input, "\"chatadd\"")) {
        cmd->type = IONITY_CMD_CHATADD;
        json_get_str(input, "text", cmd->text, sizeof(cmd->text));
    } else if (strstr(input, "\"chat\"")) {
        cmd->type = IONITY_CMD_CHAT;
        json_get_str(input, "who", cmd->text2, sizeof(cmd->text2));
        json_get_str(input, "text", cmd->text, sizeof(cmd->text));
    } else if (strstr(input, "\"query\"")) {
        cmd->type = IONITY_CMD_QUERY;
        json_get_str(input, "what", cmd->text2, sizeof(cmd->text2));
    } else if (strstr(input, "\"text\"")) {
        cmd->type = IONITY_CMD_TEXT;
        json_get_str(input, "text", cmd->text, sizeof(cmd->text));
        sscanf(input, "%*[^0-9]%d,%d", &cmd->params[0], &cmd->params[1]);
    } else if (strstr(input, "\"weather\"")) {
        cmd->type = IONITY_CMD_WEATHER;
        char *t = strstr(input, "\"temp\":");
        if (t) cmd->params[0] = atoi(t + 7);
        t = strstr(input, "\"cond\":");
        if (t) cmd->params[1] = atoi(t + 7);
    } else if (strstr(input, "\"sprite\"")) {
        cmd->type = IONITY_CMD_SPRITE;
        sscanf(input, "%*[^0-9]%d,%d,%d,%d,%d",
               &cmd->params[0], &cmd->params[1], &cmd->params[2],
               &cmd->params[3], &cmd->params[4]);
    } else if (strstr(input, "\"reboot\"")) {
        cmd->type = IONITY_CMD_REBOOT;
    } else if (strstr(input, "\"brightness\"")) {
        cmd->type = IONITY_CMD_SET_BRIGHTNESS;
        sscanf(input, "%*[^0-9]%d", &cmd->params[0]);
    }

    return input;
}

/* ---- UDP discovery beacon ---- */

static struct udp_pcb *beacon_pcb = NULL;
static char beacon_msg[96];
static uint32_t beacon_last_ms = 0;
static uint16_t beacon_stream_port = 4242;
static char beacon_name[32] = "EDGE-VIEW";

bool ionity_beacon_init(const char *device_name, uint16_t stream_port) {
    if (device_name) {
        strncpy(beacon_name, device_name, sizeof(beacon_name) - 1);
        beacon_name[sizeof(beacon_name) - 1] = '\0';
    }
    beacon_stream_port = stream_port;
    cyw43_arch_lwip_begin();
    beacon_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (beacon_pcb) ip_set_option(beacon_pcb, SOF_BROADCAST);
    cyw43_arch_lwip_end();
    return beacon_pcb != NULL;
}

void ionity_beacon_poll(void) {
    if (!beacon_pcb) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - beacon_last_ms < 5000) return;
    beacon_last_ms = now;

    const ip4_addr_t *ip = netif_ip4_addr(&cyw43_state.netif[0]);
    if (ip4_addr_isany_val(*ip)) return;
    char ipstr[16];
    ip4addr_ntoa_r(ip, ipstr, sizeof(ipstr));
    int len = snprintf(beacon_msg, sizeof(beacon_msg), "IONITY-EDGE %s %s %u",
                       beacon_name, ipstr, (unsigned)beacon_stream_port);

    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p) {
        memcpy(p->payload, beacon_msg, (size_t)len);
        ip_addr_t bcast;
        IP4_ADDR(ip_2_ip4(&bcast), 255, 255, 255, 255);
        udp_sendto(beacon_pcb, p, &bcast, IONITY_BEACON_PORT);
        pbuf_free(p);
    }
    cyw43_arch_lwip_end();
}