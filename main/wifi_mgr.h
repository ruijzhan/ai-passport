// main/wifi_mgr.h — STA connection owned by the application.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    WIFI_MGR_DOWN = 0,      // idle, failed, or not configured
    WIFI_MGR_CONNECTING,
    WIFI_MGR_UP,            // associated and has an IP address
} wifi_mgr_state_t;

// Bring up netif and the default event loop, then connect with the
// sdkconfig SSID/password. NVS must already be initialized (app_main
// does it once, before any subsystem). Empty SSID returns
// ESP_ERR_INVALID_ARG and stays DOWN.
esp_err_t wifi_mgr_start(void);
// Quiesce the radio. Called on the terminal deep-sleep path; safe when
// never started.
esp_err_t wifi_mgr_stop(void);

wifi_mgr_state_t wifi_mgr_state(void);
// RSSI in dBm; WIFI_MGR_RSSI_UNKNOWN (below any real reading) when the
// STA is not up or the read fails.
#define WIFI_MGR_RSSI_UNKNOWN (-128)
int wifi_mgr_rssi(void);
