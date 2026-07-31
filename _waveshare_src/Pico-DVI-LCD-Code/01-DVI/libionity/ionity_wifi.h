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
    IONITY_WIFI_AP_MODE,
    IONITY_WIFI_ERROR
} ionity_wifi_state_t;

typedef struct {
    char ssid[33];
    char ip_addr[16];
    ionity_wifi_state_t state;
    uint32_t connect_time_ms;
    int8_t rssi;
} ionity_wifi_info_t;

/* Init WiFi. If no saved credentials, starts in AP mode for provisioning. */
bool ionity_wifi_init(void);

/* Connect to a specific network. Returns true if connected. */
bool ionity_wifi_connect(const char *ssid, const char *password);

/* Connect using compile-time defaults. */
bool ionity_wifi_connect_default(void);

/* Start AP mode for provisioning. */
void ionity_wifi_start_ap(void);

/* Save credentials to flash. */
bool ionity_wifi_save_creds(const char *ssid, const char *password);

/* Erases saved credentials. On the next boot the device falls back to
 * build-time credentials, or to the provisioning AP if there are none. */
bool ionity_wifi_clear_creds(void);

/* Load saved credentials. Returns true if found. */
bool ionity_wifi_load_creds(char *ssid_out, size_t ssid_len,
                            char *pass_out, size_t pass_len);

/* Check if credentials are saved. */
bool ionity_wifi_has_saved_creds(void);

/* Poll (call in main loop). */
void ionity_wifi_poll(void);

/* Get current info. */
ionity_wifi_info_t ionity_wifi_get_info(void);

/* Check if connected. */
bool ionity_wifi_is_connected(void);

/* LED status. */
void ionity_wifi_led_status(void);

#ifdef __cplusplus
}
#endif

#endif