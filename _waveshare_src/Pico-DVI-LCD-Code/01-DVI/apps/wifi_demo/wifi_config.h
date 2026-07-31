#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// Edit these to match your WiFi network
#define WIFI_SSID     "YourNetworkSSID"
#define WIFI_PASSWORD "YourPassword"

// Auth type: CYW43_AUTH_WPA2_AES_PSK, CYW43_AUTH_WPA_TKIP_PSK, or CYW43_AUTH_OPEN
#define WIFI_AUTH     CYW43_AUTH_WPA2_AES_PSK

// HTTP server port
#define HTTP_PORT     80

// Country code (from cyw43_country.h)
#define WIFI_COUNTRY  CYW43_COUNTRY_WORLDWIDE

#endif
