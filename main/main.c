// main/main.c — OpenCode Go usage monitor: quota dashboard with Wi-Fi,
// NTP clock, 3-minute auto refresh (OK = manual refresh), UP/DOWN page
// switch, threshold beeps, and deep sleep after 5 idle minutes (full
// backlight while a charge signal keeps the device awake). Fresh
// screens; the baseline demo now lives in examples/baseline-demo/ and is
// not part of this build.
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
#include "alert.h"
#include "alert_beep.h"
#include "key_guard.h"
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
// Backlight levels (%): dim normally; full while a charge signal inhibits
// automatic sleep, so the state keeping the device awake is visible.
#define BACKLIGHT_IDLE_PCT 10
#define BACKLIGHT_FULL_PCT 100

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
static bool s_backlight_full;  // worker's last applied backlight state

// Waking from deep sleep by key reboots the chip while that key is still
// held: the button component starts a few hundred milliseconds later,
// registers a press that is already down, and grades it a long press once
// it has been down for its threshold, so an unintended action fires almost
// immediately. Drop key actions until that press comes up; the deadline is
// only a safety net so a stuck key cannot leave the pad dead. The state
// machine lives in key_guard.h so it can be host-tested without the app.
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// Button callbacks run on the shared esp_timer task: enqueue only.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    // Consume the guard before the readiness check: a release can arrive after the
    // post-init voltage sample but before input dispatch is ready, and dropping it
    // here would leave the guard armed to swallow the next real press.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        // The release and the deadline both arrive on this task; log the disarm
        // transition once it happens.
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static refresh_result_t do_refresh(void)
{
    // Cheap pre-flight: don't burn a 12 s HTTP timeout while the STA is
    // still associating.
    if (wifi_mgr_state() != WIFI_MGR_UP) {
        usage_store_set_failed("WAITING FOR WIFI");
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
    usage_store_set_failed(err == ESP_ERR_INVALID_ARG ? "API KEY NOT SET"
                                                      : "FETCH FAILED");
    return REFRESH_SLOW;
}

// Worker: periodic refresh, on-demand refresh, threshold beeps, and idle sleep.
static void worker_task(void *arg)
{
    (void)arg;
    const int64_t interval_us = (int64_t)CONFIG_APP_USAGE_REFRESH_S * 1000000;
    const int64_t fast_retry_us = (int64_t)REFRESH_FAST_RETRY_S * 1000000;
    const int64_t slow_retry_us = (int64_t)REFRESH_SLOW_RETRY_S * 1000000;
    int64_t next_refresh = esp_timer_get_time() + 5 * 1000000;  // first fetch soon
    // Edge-triggered beeps: only a rising threshold plays. Starts at
    // NORMAL so a reboot into an already-high quota still warns once.
    alert_level_t last_beep_level = ALERT_NORMAL;
    for (;;) {
        // Sample charge state once per wake (minutes scale, so the
        // voltage-slope history is warm by the time the idle deadline
        // expires). Only the idle path is gated; OK long-press in
        // input_task still sleeps unconditionally.
        bool charge_inhibited = power_idle_charge_inhibited();
        // Mirror the charge-inhibit state on the panel: full brightness
        // while a charge signal keeps the device awake, dim otherwise.
        // Edge-triggered; worker wakes are minutes apart.
        if (charge_inhibited != s_backlight_full) {
            bsp_display_backlight(charge_inhibited ? BACKLIGHT_FULL_PCT
                                                   : BACKLIGHT_IDLE_PCT);
            s_backlight_full = charge_inhibited;
        }
        // Block until the nearer of the next refresh and the idle
        // deadline. While a charge signal inhibits sleep the idle deadline
        // cannot end the wait — clamping to it would collapse the wait to
        // 0 and spin this task (and its two gauge reads) at full speed — so
        // the refresh deadline paces the re-check instead. A button push
        // only moves the deadline later, so a stale early wake harmlessly
        // recomputes and re-blocks: no fixed wake cap, and sleep starts
        // within a tick of expiry whenever nothing inhibits it.
        int64_t now = esp_timer_get_time();
        int64_t wait_us = next_refresh - now;
        if (!charge_inhibited) {
            const int64_t idle_left_us =
                ((int64_t)CONFIG_APP_IDLE_SLEEP_S - power_idle_idle_s()) * 1000000;
            if (idle_left_us < wait_us) wait_us = idle_left_us;
        }
        if (wait_us < 0) wait_us = 0;

        uint32_t cmd = 0;
        BaseType_t got = xTaskNotifyWait(0, UINT32_MAX, &cmd,
                                         pdMS_TO_TICKS((uint32_t)(wait_us / 1000)));
        if (power_idle_expired() && !charge_inhibited) {
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
            // Beeps run here in the worker: audio writes block for
            // hundreds of ms and must stay off the LVGL timer and the
            // button callback. A falling level re-arms silently.
            if (r == REFRESH_OK) {
                usage_snapshot_t snap;
                usage_store_get(&snap);
                alert_level_t level = alert_level_for_windows(&snap.info);
                if (level > last_beep_level) {
                    alert_beep_play(level);
                }
                last_beep_level = level;
            }
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
        } else if (input.event == BSP_BTN_CLICK &&
                   (input.btn == BSP_BTN_UP || input.btn == BSP_BTN_DOWN)) {
            // Page switch needs the LVGL lock; the input task may block
            // briefly, unlike the esp_timer button callback that fed it.
            if (bsp_lvgl_lock(500)) {
                if (input.btn == BSP_BTN_UP) {
                    ui_home_page_prev();
                } else {
                    ui_home_page_next();
                }
                bsp_lvgl_unlock();
            }
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
    // A key wake reboots with that key still held; ignore its actions until
    // it comes up (see on_key). power_idle.c arms only the GPIO-low button
    // wakeup, so a GPIO wake here is a button wake.
    if (wakeup == ESP_SLEEP_WAKEUP_GPIO) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "key wake: suppressing key actions until release");
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
    bsp_display_backlight(BACKLIGHT_IDLE_PCT);

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
    } else if (s_key_guard.armed) {
        // If the wake key was already released before the button driver
        // started, the component never saw a press and will never emit a
        // release; sync from the real voltage so the guard disarms now.
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key already released; guard disarmed early");
        }
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
