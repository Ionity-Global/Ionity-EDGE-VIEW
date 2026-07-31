#include "ionity_wifi.h"
#include <string.h>
#include <stdio.h>

static ionity_wifi_info_t wifi_info = {0};
static absolute_time_t connect_start;

bool ionity_wifi_init(void) {
    memset(&wifi_info, 0, sizeof(wifi_info));
    wifi_info.state = IONITY_WIFI_DISCONNECTED;

    if (cyw43_arch_init()) {
        wifi_info.state = IONITY_WIFI_ERROR;
        return false;
    }

    cyw43_arch_enable_sta_mode();
    return true;
}

bool ionity_wifi_connect(const char *ssid, const char *password) {
    if (wifi_info.state == IONITY_WIFI_ERROR) return false;

    wifi_info.state = IONITY_WIFI_CONNECTING;
    strncpy(wifi_info.ssid, ssid, sizeof(wifi_info.ssid) - 1);
    connect_start = get_absolute_time();

    int result = cyw43_arch_wifi_connect_timeout_ms(
        ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000);

    if (result == 0) {
        wifi_info.state = IONITY_WIFI_CONNECTED;
        wifi_info.connect_time_ms = absolute_time_diff_us(connect_start, get_absolute_time()) / 1000;

        uint8_t *ip = (uint8_t *)&cyw43_state.netif[0].ip_addr.addr;
        snprintf(wifi_info.ip_addr, sizeof(wifi_info.ip_addr),
                 "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

        wifi_info.rssi = 0;
        return true;
    }

    wifi_info.state = IONITY_WIFI_ERROR;
    return false;
}

bool ionity_wifi_connect_default(void) {
    return ionity_wifi_connect(IONITY_WIFI_SSID, IONITY_WIFI_PASS);
}

void ionity_wifi_poll(void) {
    cyw43_arch_poll();

    if (wifi_info.state == IONITY_WIFI_CONNECTED) {
        int rssi_val;
        if (cyw43_wifi_get_rssi(&cyw43_state, &rssi_val) == 0) {
            wifi_info.rssi = (int8_t)rssi_val;
        }
    }
}

ionity_wifi_info_t ionity_wifi_get_info(void) {
    return wifi_info;
}

bool ionity_wifi_is_connected(void) {
    return wifi_info.state == IONITY_WIFI_CONNECTED &&
           cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN;
}

void ionity_wifi_led_status(void) {
    if (wifi_info.state == IONITY_WIFI_CONNECTED) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    } else if (wifi_info.state == IONITY_WIFI_CONNECTING) {
        static absolute_time_t last_toggle;
        static bool blink_on = false;
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(last_toggle, now) > 500000) {  /* 500 ms */
            blink_on = !blink_on;
            last_toggle = now;
        }
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_on);
    } else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }
}