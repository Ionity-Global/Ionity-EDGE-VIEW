#ifndef IONITY_HTTP_H
#define IONITY_HTTP_H

#include "pico/stdlib.h"
#include "lwip/tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IONITY_HTTP_PORT 80
#define IONITY_HTTP_MAX_MSG_LEN 256
#define IONITY_HTTP_MAX_MSGS 20

typedef struct {
    char text[IONITY_HTTP_MAX_MSG_LEN];
    char author[32];
    uint32_t timestamp;
} ionity_message_t;

typedef void (*ionity_http_msg_handler_t)(const char *text, const char *author);

bool ionity_http_init(uint16_t port);
void ionity_http_set_msg_handler(ionity_http_msg_handler_t handler);
void ionity_http_send_json(const char *json);

/* Message queue for scrolling display */
void ionity_http_msg_push(const char *text, const char *author);
int  ionity_http_msg_count(void);
const ionity_message_t *ionity_http_msg_get(int index);
void ionity_http_msg_clear(void);

#ifdef __cplusplus
}
#endif

#endif