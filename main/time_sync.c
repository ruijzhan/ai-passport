// main/time_sync.c — see time_sync.h.
#include "time_sync.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "wifi_mgr.h"

static const char *TAG = "time_sync";
static bool s_started;

// 2020-01-01T00:00:00Z: anything earlier means "not synced yet".
#define MIN_PLAUSIBLE_UTC 1577836800LL

void time_sync_start(void)
{
    setenv("TZ", CONFIG_APP_TIMEZONE, 1);
    tzset();
    if (s_started) return;
    // SNTP posts to the LWIP tcpip mbox, which only exists after the
    // network stack is up. Starting it with Wi-Fi down (empty SSID,
    // offline) aborts in tcpip_callback and boot-loops the device.
    if (wifi_mgr_state() != WIFI_MGR_UP) {
        return;
    }
    const char *server = CONFIG_APP_NTP_SERVER;
    if (!server || server[0] == '\0') {
        ESP_LOGW(TAG, "NTP server not configured");
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "SNTP started (%s)", server);
}

bool time_sync_done(void)
{
    return (long long)time(NULL) >= MIN_PLAUSIBLE_UTC;
}
