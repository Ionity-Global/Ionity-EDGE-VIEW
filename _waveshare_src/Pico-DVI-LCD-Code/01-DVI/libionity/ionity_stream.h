#ifndef IONITY_STREAM_H
#define IONITY_STREAM_H

#include "pico/stdlib.h"
#include "lwip/tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IONITY_STREAM_MAX_CMD_LEN 512
#define IONITY_STREAM_MAX_CLIENTS 4

typedef enum {
    IONITY_CMD_NONE = 0,
    IONITY_CMD_CLEAR,
    IONITY_CMD_RECT,
    IONITY_CMD_TEXT,
    IONITY_CMD_SPRITE,
    IONITY_CMD_WEATHER,
    IONITY_CMD_PING,
    IONITY_CMD_SET_BRIGHTNESS,
    IONITY_CMD_REBOOT,
} ionity_cmd_type_t;

typedef struct {
    ionity_cmd_type_t type;
    char raw[IONITY_STREAM_MAX_CMD_LEN];
    int params[8];
    char text[128];
} ionity_command_t;

typedef void (*ionity_cmd_handler_t)(const ionity_command_t *cmd);

bool ionity_stream_init(uint16_t port);
void ionity_stream_set_handler(ionity_cmd_handler_t handler);
void ionity_stream_send(const char *data, uint16_t len);
bool ionity_stream_is_connected(void);
const char *ionity_stream_parse(const char *input, ionity_command_t *cmd);

#ifdef __cplusplus
}
#endif

#endif