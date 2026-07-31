#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/* Credentials come from the central wifi_config.cmake at the repo root
 * (passed in as IONITY_WIFI_SSID / IONITY_WIFI_PASS compile definitions).
 * The literals below are only a fallback for standalone builds. */
#if defined(IONITY_WIFI_SSID) && defined(IONITY_WIFI_PASS)
#define WIFI_SSID     IONITY_WIFI_SSID
#define WIFI_PASSWORD IONITY_WIFI_PASS
#else
#define WIFI_SSID     "Antwerp Ionity"
#define WIFI_PASSWORD "Antwerp1990!"
#endif

// Use mixed PSK for broadest compatibility (WPA/WPA2)
#define WIFI_AUTH     CYW43_AUTH_WPA2_MIXED_PSK
#define HTTP_PORT     80
#define WIFI_COUNTRY  CYW43_COUNTRY('Z', 'A', 0)   /* South Africa */

#endif
