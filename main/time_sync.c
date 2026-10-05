// main/time_sync.c — see time_sync.h.
#include "time_sync.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "wifi_mgr.h"

static const char *TAG = "time_sync";
static bool s_started;

// Re-poll every 15 min so XTAL/RTC drift stays in the ~1 s range instead of
// accumulating over the lwIP default 1 h window (CONFIG_LWIP_SNTP_UPDATE_DELAY).
// Minimum allowed by RFC 4330 is 15 s; 15 min balances accuracy vs. power.
#define TIME_SYNC_INTERVAL_MS (15u * 60u * 1000u)

// 2020-01-01T00:00:00Z: anything earlier means "not synced yet".
#define MIN_PLAUSIBLE_UTC 1577836800LL

static void on_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "time synced, utc=%lld", (long long)time(NULL));
}

void time_sync_start(void)
{
    if (s_started) return;
    setenv("TZ", CONFIG_APP_TIMEZONE, 1);
    tzset();
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
    // IMMED jumps straight to the server time. SMOOTH would slew via
    // adjtime() and can look "tens of seconds slow" for a long while.
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_sync_interval(TIME_SYNC_INTERVAL_MS);
    esp_sntp_set_time_sync_notification_cb(on_sync);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "SNTP started (%s, %ums)", server,
             (unsigned)TIME_SYNC_INTERVAL_MS);
}

bool time_sync_done(void)
{
    return (long long)time(NULL) >= MIN_PLAUSIBLE_UTC;
}
