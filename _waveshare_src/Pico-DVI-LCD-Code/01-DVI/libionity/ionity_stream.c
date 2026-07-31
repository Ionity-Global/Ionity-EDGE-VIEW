#include "ionity_stream.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"

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

            const char *banner = "{\"type\":\"hello\",\"device\":\"Station Pico\"}\n";
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

const char *ionity_stream_parse(const char *input, ionity_command_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    strncpy(cmd->raw, input, sizeof(cmd->raw) - 1);

    if (strstr(input, "\"ping\"")) {
        cmd->type = IONITY_CMD_PING;
    } else if (strstr(input, "\"clear\"")) {
        cmd->type = IONITY_CMD_CLEAR;
    } else if (strstr(input, "\"rect\"")) {
        cmd->type = IONITY_CMD_RECT;
        sscanf(input, "%*[^0-9]%d,%d,%d,%d,%d",
               &cmd->params[0], &cmd->params[1], &cmd->params[2],
               &cmd->params[3], &cmd->params[4]);
    } else if (strstr(input, "\"text\"")) {
        cmd->type = IONITY_CMD_TEXT;
        char *t = strstr(input, "\"text\":\"");
        if (t) {
            t += 8;
            int i = 0;
            while (*t && *t != '"' && i < 127) cmd->text[i++] = *t++;
            cmd->text[i] = '\0';
        }
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