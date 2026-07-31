#include "ionity_wifi.h"
#include <string.h>
#include <stdio.h>
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CRED_MAGIC 0x494F4E59  /* "IONY" */

/* Regulatory domain — default South Africa (Ionity Global HQ). */
#ifndef IONITY_WIFI_COUNTRY
#define IONITY_WIFI_COUNTRY CYW43_COUNTRY('Z', 'A', 0)
#endif

#define WIFI_CONNECT_ATTEMPTS   3
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RECHECK_MS         5000    /* link watchdog interval */
#define WIFI_RECONNECT_MS       10000   /* wait between reconnect tries */

typedef struct {
    uint32_t magic;
    char ssid[33];
    char pass[65];
    uint32_t checksum;
} wifi_creds_t;

static wifi_creds_t saved_creds;
static ionity_wifi_info_t wifi_info = {0};
static absolute_time_t connect_start;
static char active_ssid[33] = "";
static char active_pass[65] = "";
static uint32_t last_check_ms = 0;
static uint32_t last_reconnect_ms = 0;

static uint32_t creds_checksum(const wifi_creds_t *c) {
    uint32_t sum = c->magic;
    for (int i = 0; c->ssid[i]; i++) sum += (uint8_t)c->ssid[i];
    for (int i = 0; c->pass[i]; i++) sum += (uint8_t)c->pass[i];
    return sum;
}

bool ionity_wifi_has_saved_creds(void) {
    const wifi_creds_t *flash_creds = (const wifi_creds_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (flash_creds->magic != CRED_MAGIC) return false;
    return flash_creds->checksum == creds_checksum(flash_creds);
}

bool ionity_wifi_load_creds(char *ssid_out, size_t ssid_len,
                            char *pass_out, size_t pass_len) {
    const wifi_creds_t *flash_creds = (const wifi_creds_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (flash_creds->magic != CRED_MAGIC) return false;
    if (flash_creds->checksum != creds_checksum(flash_creds)) return false;
    strncpy(ssid_out, flash_creds->ssid, ssid_len - 1);
    ssid_out[ssid_len - 1] = '\0';
    strncpy(pass_out, flash_creds->pass, pass_len - 1);
    pass_out[pass_len - 1] = '\0';
    return true;
}

bool ionity_wifi_save_creds(const char *ssid, const char *password) {
    memset(&saved_creds, 0, sizeof(saved_creds));
    saved_creds.magic = CRED_MAGIC;
    strncpy(saved_creds.ssid, ssid, 32);
    strncpy(saved_creds.pass, password, 64);
    saved_creds.checksum = creds_checksum(&saved_creds);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&saved_creds, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    const wifi_creds_t *verify = (const wifi_creds_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    return verify->magic == CRED_MAGIC && verify->checksum == creds_checksum(verify);
}

bool ionity_wifi_clear_creds(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    memset(&saved_creds, 0, sizeof(saved_creds));
    return !ionity_wifi_has_saved_creds();
}

void ionity_wifi_start_ap(void) {
    cyw43_arch_enable_ap_mode("IO-nity-Setup", "ionity123", CYW43_AUTH_WPA2_AES_PSK);
    wifi_info.state = IONITY_WIFI_AP_MODE;
    strncpy(wifi_info.ssid, "IO-nity-Setup", sizeof(wifi_info.ssid) - 1);
    snprintf(wifi_info.ip_addr, sizeof(wifi_info.ip_addr), "192.168.4.1");
}

bool ionity_wifi_init(void) {
    memset(&wifi_info, 0, sizeof(wifi_info));
    wifi_info.state = IONITY_WIFI_DISCONNECTED;

    if (cyw43_arch_init_with_country(IONITY_WIFI_COUNTRY)) {
        wifi_info.state = IONITY_WIFI_ERROR;
        return false;
    }

    /* 1) Saved credentials from provisioning take priority. */
    if (ionity_wifi_has_saved_creds()) {
        char ssid[33], pass[65];
        if (ionity_wifi_load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
            cyw43_arch_enable_sta_mode();
            if (ionity_wifi_connect(ssid, pass)) return true;
            printf("[wifi] saved creds failed, trying build-time creds\n");
        }
    }

    /* 2) Build-time credentials (wifi_config.cmake). */
#if defined(IONITY_WIFI_SSID)
    if (IONITY_WIFI_SSID[0] != '\0') {
        cyw43_arch_enable_sta_mode();
        if (ionity_wifi_connect_default()) return true;
        /* Keep STA mode: dashboard runs offline while the background
         * watchdog in ionity_wifi_poll() keeps retrying. */
        printf("[wifi] build-time creds failed — will keep retrying\n");
        wifi_info.state = IONITY_WIFI_DISCONNECTED;
        last_reconnect_ms = to_ms_since_boot(get_absolute_time());
        return true;
    }
#endif

    /* 3) No way to join — start AP mode for provisioning. */
    ionity_wifi_start_ap();
    return true;
}

bool ionity_wifi_connect(const char *ssid, const char *password) {
    if (wifi_info.state == IONITY_WIFI_ERROR) return false;

    wifi_info.state = IONITY_WIFI_CONNECTING;
    strncpy(wifi_info.ssid, ssid, sizeof(wifi_info.ssid) - 1);
    strncpy(active_ssid, ssid, sizeof(active_ssid) - 1);
    strncpy(active_pass, password, sizeof(active_pass) - 1);
    connect_start = get_absolute_time();

    /* Retry with escalating patience — WPA2-mixed for broad AP compatibility. */
    int result = -1;
    for (int attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS; attempt++) {
        printf("[wifi] connect '%s' attempt %d/%d\n", ssid, attempt, WIFI_CONNECT_ATTEMPTS);
        result = cyw43_arch_wifi_connect_timeout_ms(
            ssid, password, CYW43_AUTH_WPA2_MIXED_PSK, WIFI_CONNECT_TIMEOUT_MS);
        if (result == 0) break;
        sleep_ms(1000 * attempt);   /* linear backoff between attempts */
    }

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

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_check_ms < WIFI_RECHECK_MS) return;
    last_check_ms = now;

    if (wifi_info.state == IONITY_WIFI_CONNECTED) {
        int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link != CYW43_LINK_JOIN) {
            printf("[wifi] link lost (%d) — reconnecting\n", link);
            wifi_info.state = IONITY_WIFI_DISCONNECTED;
            last_reconnect_ms = now;
        } else {
            int32_t rssi_val;
            if (cyw43_wifi_get_rssi(&cyw43_state, &rssi_val) == 0) {
                wifi_info.rssi = (int8_t)rssi_val;
            }
        }
    } else if ((wifi_info.state == IONITY_WIFI_DISCONNECTED ||
                wifi_info.state == IONITY_WIFI_ERROR) &&
               active_ssid[0] != '\0') {
        /* Background reconnect: non-blocking async join, checked next poll. */
        if (now - last_reconnect_ms >= WIFI_RECONNECT_MS) {
            last_reconnect_ms = now;
            wifi_info.state = IONITY_WIFI_CONNECTING;
            cyw43_arch_wifi_connect_async(active_ssid, active_pass,
                                          CYW43_AUTH_WPA2_MIXED_PSK);
        }
    } else if (wifi_info.state == IONITY_WIFI_CONNECTING && active_ssid[0] != '\0') {
        int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link == CYW43_LINK_JOIN) {
            uint8_t *ip = (uint8_t *)&cyw43_state.netif[0].ip_addr.addr;
            if (ip[0] != 0) {
                snprintf(wifi_info.ip_addr, sizeof(wifi_info.ip_addr),
                         "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                wifi_info.state = IONITY_WIFI_CONNECTED;
                printf("[wifi] reconnected: %s\n", wifi_info.ip_addr);
            }
        } else if (link < 0 || now - last_reconnect_ms > 30000) {
            wifi_info.state = IONITY_WIFI_DISCONNECTED;   /* try again next window */
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
        static bool blink = false;
        blink = !blink;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink);
    } else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }
}