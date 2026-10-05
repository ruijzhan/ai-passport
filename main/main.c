// main/main.c — OpenCode Go usage monitor: quota dashboard with Wi-Fi,
// NTP clock, 3-minute auto refresh (OK = manual refresh), and deep sleep
// after 5 idle minutes. Fresh screens; the baseline demo now lives in
// examples/baseline-demo/ and is not part of this build.
#include <string.h>
#include <time.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "power_idle.h"
#include "time_sync.h"
#include "usage_client.h"
#include "usage_store.h"
#include "ui_home.h"
#include "wifi_mgr.h"

static const char *TAG = "opencode_go";

#define INPUT_QUEUE_DEPTH 8
#define REFRESH_CMD 1
// Retry cadence after a failed refresh. Pre-flight failures (Wi-Fi still
// associating) did no radio work, so they retry fast; a real fetch
// failure pays DNS+TCP+TLS and backs off longer. Success uses
// CONFIG_APP_USAGE_REFRESH_S.
#define REFRESH_FAST_RETRY_S 5
#define REFRESH_SLOW_RETRY_S 60

typedef enum {
    REFRESH_OK,     // new snapshot stored
    REFRESH_FAST,   // pre-flight failed; retry soon
    REFRESH_SLOW,   // fetch or config failed; back off
} refresh_result_t;

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_worker_task;
static volatile bool s_input_ready;

// Button callbacks run on the shared esp_timer task: enqueue only.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static refresh_result_t do_refresh(void)
{
    // Cheap pre-flight: don't burn a 12 s HTTP timeout while the STA is
    // still associating.
    if (wifi_mgr_state() != WIFI_MGR_UP) {
        usage_store_set_failed("Waiting for WiFi");
        return REFRESH_FAST;
    }
    usage_info_t info;
    esp_err_t err = usage_fetch(&info);
    if (err == ESP_OK) {
        usage_store_set_ok(&info, time(NULL));
        return REFRESH_OK;
    }
    // ESP_ERR_INVALID_ARG means the key is unset (usage_client.h); every
    // other error is an HTTP/TLS failure. Both need config or the network
    // to change, so they back off.
    usage_store_set_failed(err == ESP_ERR_INVALID_ARG ? "API key not set"
                                                      : "Fetch failed");
    return REFRESH_SLOW;
}

// Worker: periodic refresh, on-demand refresh, and idle sleep.
static void worker_task(void *arg)
{
    (void)arg;
    const int64_t interval_us = (int64_t)CONFIG_APP_USAGE_REFRESH_S * 1000000;
    const int64_t fast_retry_us = (int64_t)REFRESH_FAST_RETRY_S * 1000000;
    const int64_t slow_retry_us = (int64_t)REFRESH_SLOW_RETRY_S * 1000000;
    int64_t next_refresh = esp_timer_get_time() + 5 * 1000000;  // first fetch soon
    for (;;) {
        // Block until the nearer of the next refresh and the idle
        // deadline. A button push only moves the deadline later, so a
        // stale early wake harmlessly recomputes and re-blocks — no
        // fixed wake cap, and sleep starts within a tick of expiry.
        int64_t now = esp_timer_get_time();
        int64_t wait_us = next_refresh - now;
        const int64_t idle_left_us =
            ((int64_t)CONFIG_APP_IDLE_SLEEP_S - power_idle_idle_s()) * 1000000;
        if (idle_left_us < wait_us) wait_us = idle_left_us;
        if (wait_us < 0) wait_us = 0;

        uint32_t cmd = 0;
        BaseType_t got = xTaskNotifyWait(0, UINT32_MAX, &cmd,
                                         pdMS_TO_TICKS((uint32_t)(wait_us / 1000)));
        if (power_idle_expired()) {
            power_idle_enter_deep_sleep();
        }
        // Refresh on OK request or when the periodic deadline passed.
        if ((got == pdTRUE && cmd == REFRESH_CMD) ||
            esp_timer_get_time() >= next_refresh) {
            refresh_result_t r = do_refresh();
            int64_t retry_us = r == REFRESH_OK ? interval_us :
                                 r == REFRESH_FAST ? fast_retry_us :
                                                     slow_retry_us;
            next_refresh = esp_timer_get_time() + retry_us;
        }
    }
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        power_idle_mark_activity();
        if (input.btn == BSP_BTN_OK && input.event == BSP_BTN_CLICK &&
            s_worker_task) {
            xTaskNotify(s_worker_task, REFRESH_CMD, eSetValueWithOverwrite);
        } else if (input.btn == BSP_BTN_OK && input.event == BSP_BTN_LONG) {
            power_idle_enter_deep_sleep();
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "OpenCode Go usage monitor starting");
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "wakeup cause: %d", wakeup);
    }

    // NVS once, before any subsystem: usage_store reads its cache and
    // wifi_mgr needs the calibration data. Best effort, never erase — a
    // broken NVS only drops the usage cache and Wi-Fi credentials.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s (not erasing user data)",
                 esp_err_to_name(err));
    }

    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(10);

    usage_store_init();
    power_idle_init();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue creation failed");
        return;
    }
    if (xTaskCreate(input_task, "go_input", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task creation failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed; idle sleep still applies");
    }
    if (xTaskCreate(worker_task, "go_worker", 6144, NULL, 4,
                    &s_worker_task) != pdPASS) {
        ESP_LOGE(TAG, "worker task creation failed");
        return;
    }

    if (CONFIG_APP_WIFI_AUTOCONNECT) {
        if (wifi_mgr_start() != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi not started (SSID/key missing?)");
        }
    } else {
        ESP_LOGI(TAG, "Wi-Fi autoconnect disabled");
    }
    time_sync_start();

    if (bsp_lvgl_lock(1000)) {
        ui_home_create();
        bsp_lvgl_unlock();
        s_input_ready = true;
    } else {
        ESP_LOGE(TAG, "LVGL lock failed, UI not created");
        return;
    }

    // Gauge init last: it spends ~100 ms of I2C handshakes (worst case
    // seconds rechecking the battery profile) and the UI renders "--"
    // until it answers, so it must not sit between power-on and the
    // first frame or the first refresh.
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge unavailable, continuing without SOC");
    }

    ESP_LOGI(TAG, "ready");
}
