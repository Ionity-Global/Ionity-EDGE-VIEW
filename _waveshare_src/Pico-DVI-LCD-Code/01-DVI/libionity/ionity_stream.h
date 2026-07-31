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
    IONITY_CMD_VERSE,      /* {"type":"verse","ref":"...","text":"..."} */
    IONITY_CMD_NEWS,       /* {"type":"news","text":"headline"} */
    IONITY_CMD_TIME,       /* {"type":"time","epoch":1234567890} (UTC) */
    IONITY_CMD_MODE,       /* {"type":"mode","name":"pacman|snake|bounce|off"} */
    IONITY_CMD_DATA,       /* {"type":"data","key":"...","value":"..."} */
    IONITY_CMD_LAYOUT,     /* {"type":"layout","slot":"stage","panel":"weather"} */
    IONITY_CMD_WIFI,       /* {"type":"wifi","ssid":"...","pass":"..."} */
    IONITY_CMD_CHAT,       /* {"type":"chat","who":"...","text":"..."}  */
    IONITY_CMD_CHATADD,    /* {"type":"chatadd","text":"..."} streamed   */
    IONITY_CMD_QUERY,      /* {"type":"query","what":"layout|status"} */
} ionity_cmd_type_t;

typedef struct {
    ionity_cmd_type_t type;
    char raw[IONITY_STREAM_MAX_CMD_LEN];
    int params[8];
    char text[320];
    char text2[64];        /* secondary string (e.g. verse reference) */
    char text3[64];        /* tertiary string (e.g. layout panel name) */
} ionity_command_t;

typedef void (*ionity_cmd_handler_t)(const ionity_command_t *cmd);

bool ionity_stream_init(uint16_t port);
void ionity_stream_set_handler(ionity_cmd_handler_t handler);
void ionity_stream_send(const char *data, uint16_t len);
bool ionity_stream_is_connected(void);
const char *ionity_stream_parse(const char *input, ionity_command_t *cmd);

/* UDP discovery beacon: broadcasts "IONITY-EDGE <name> <ip> <port>" every 5 s
 * on IONITY_BEACON_PORT so the Studio app can auto-discover the device. */
#define IONITY_BEACON_PORT 4243
bool ionity_beacon_init(const char *device_name, uint16_t stream_port);
void ionity_beacon_poll(void);

#ifdef __cplusplus
}
#endif

#endif