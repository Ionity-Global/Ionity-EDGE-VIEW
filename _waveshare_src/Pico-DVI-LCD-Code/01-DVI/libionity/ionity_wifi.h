#ifndef IONITY_WIFI_H
#define IONITY_WIFI_H

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IONITY_WIFI_DISCONNECTED = 0,
    IONITY_WIFI_CONNECTING,
    IONITY_WIFI_CONNECTED,
    IONITY_WIFI_ERROR
} ionity_wifi_state_t;

typedef struct {
    char ssid[33];
    char ip_addr[16];
    ionity_wifi_state_t state;
    uint32_t connect_time_ms;
    int8_t rssi;
} ionity_wifi_info_t;

bool ionity_wifi_init(void);
bool ionity_wifi_connect(const char *ssid, const char *password);
bool ionity_wifi_connect_default(void);
void ionity_wifi_poll(void);
ionity_wifi_info_t ionity_wifi_get_info(void);
bool ionity_wifi_is_connected(void);
void ionity_wifi_led_status(void);

#ifdef __cplusplus
}
#endif

#endif