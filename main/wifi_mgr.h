// main/wifi_mgr.h — STA connection owned by the application.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    WIFI_MGR_DOWN = 0,      // idle, failed, or not configured
    WIFI_MGR_CONNECTING,
    WIFI_MGR_UP,            // associated and has an IP address
} wifi_mgr_state_t;

// Initialize NVS/netif/event loop once, then connect with the sdkconfig
// SSID/password. Empty SSID returns ESP_ERR_INVALID_ARG and stays DOWN.
esp_err_t wifi_mgr_start(void);
// Stop the STA. Safe to call when never started.
esp_err_t wifi_mgr_stop(void);

wifi_mgr_state_t wifi_mgr_state(void);
// RSSI in dBm; 0 when unknown.
int wifi_mgr_rssi(void);
