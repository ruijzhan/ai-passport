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

#include "power_idle.h"
#include "time_sync.h"
#include "usage_client.h"
#include "usage_store.h"
#include "ui_home.h"
#include "wifi_mgr.h"

static const char *TAG = "opencode_go";

#define INPUT_QUEUE_DEPTH 8
#define REFRESH_CMD 1
// Seconds before retrying a failed usage fetch (success uses
// CONFIG_APP_USAGE_REFRESH_S).
#define REFRESH_RETRY_S 30

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
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

static bool do_refresh(void)
{
    time_t now = time(NULL);
    if (CONFIG_APP_OPENCODE_API_KEY[0] == '\0') {
        usage_store_set_failed("API key not set", now);
        return false;
    }
    if (wifi_mgr_state() != WIFI_MGR_UP) {
        usage_store_set_failed("Waiting for WiFi", now);
        return false;
    }
    time_sync_start();
    usage_info_t info;
    if (usage_fetch(&info) == ESP_OK) {
        usage_store_set_ok(&info, time(NULL));
        return true;
    }
    usage_store_set_failed("Fetch failed", time(NULL));
    return false;
}

// Worker: periodic refresh, on-demand refresh, and idle sleep.
static void worker_task(void *arg)
{
    (void)arg;
    const int64_t interval_us = (int64_t)CONFIG_APP_USAGE_REFRESH_S * 1000000;
    // A failed fetch (usually Wi-Fi not ready yet on early boot) retries
    // soon so the first good snapshot — and the pre-sleep NVS cache that
    // depends on it — is not delayed by a full refresh interval.
    const int64_t retry_us = (int64_t)REFRESH_RETRY_S * 1000000;
    // Wake at least once a minute so the idle deadline is checked even
    // when the next refresh is far away.
    const TickType_t tick_cap = pdMS_TO_TICKS(60000);
    int64_t next_refresh = esp_timer_get_time() + 5 * 1000000;  // first fetch soon
    for (;;) {
        int64_t now = esp_timer_get_time();
        int64_t wait_us = next_refresh - now;
        if (wait_us < 0) wait_us = 0;
        TickType_t wait_ticks = pdMS_TO_TICKS((uint32_t)(wait_us / 1000));
        if (wait_ticks > tick_cap) wait_ticks = tick_cap;

        uint32_t cmd = 0;
        BaseType_t got = xTaskNotifyWait(0, UINT32_MAX, &cmd, wait_ticks);
        if (power_idle_expired()) {
            power_idle_enter_deep_sleep();
        }
        if (got == pdTRUE && cmd == REFRESH_CMD) {
            bool ok = do_refresh();
            next_refresh = esp_timer_get_time() + (ok ? interval_us : retry_us);
        } else if (esp_timer_get_time() >= next_refresh) {
            bool ok = do_refresh();
            next_refresh = esp_timer_get_time() + (ok ? interval_us : retry_us);
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

    bsp_i2c_init();
    bsp_i2c_scan();
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge unavailable, continuing without SOC");
    }

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
    if (xTaskCreate(input_task, "go_input", 3072, NULL, 5, &s_input_task) != pdPASS) {
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

    ESP_LOGI(TAG, "ready");
}
