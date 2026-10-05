// main/power_idle.c — see power_idle.h.
//
// Terminal shutdown order: wait for all buttons to be released (a LONG
// press is still held when it fires), then UI teardown plus LCD suspend
// under a single LVGL lock, then Wi-Fi stop, battery gauge sleep, and
// shared-I2C pin release. Audio is never initialized by this app, so
// there is nothing to suspend.
#include "power_idle.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "ui_home.h"
#include "wifi_mgr.h"

static const char *TAG = "power_idle";
static int64_t s_last_activity_us;
static volatile bool s_sleeping;

void power_idle_init(void)
{
    s_last_activity_us = esp_timer_get_time();
}

void power_idle_mark_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

int64_t power_idle_idle_s(void)
{
    int64_t idle_us = esp_timer_get_time() - s_last_activity_us;
    if (idle_us < 0) idle_us = 0;
    return idle_us / 1000000;
}

bool power_idle_expired(void)
{
    return power_idle_idle_s() >= CONFIG_APP_IDLE_SLEEP_S;
}

static void warn(const char *step, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deep sleep continues: %s failed: %s", step,
                 esp_err_to_name(err));
    }
}

// LONG fires while the finger is still down (500 ms press). All three
// buttons pull the shared GPIO0 ADC node below the digital-low threshold
// (OK ≈ 595 mV), so arming ESP_GPIO_WAKEUP_GPIO_LOW and calling
// esp_deep_sleep_start() while still held wakes instantly and looks like
// a reboot. Wait for a stable release before arming wakeup.
static void wait_for_button_release(void)
{
    // Above OK's 1900 mV window top, well below the released ~3300 mV.
    const int release_mv = 2500;
    const TickType_t poll = pdMS_TO_TICKS(20);
    int stable = 0;
    for (;;) {
        int mv = bsp_button_read_mv();
        bool released;
        if (mv < 0) {
            released = (gpio_get_level(GPIO_NUM_0) == 1);
        } else {
            released = (mv >= release_mv);
        }
        if (released) {
            if (++stable >= 3) {
                break;
            }
        } else {
            stable = 0;
        }
        vTaskDelay(poll);
    }
    // Settle the ADC node firmly high before the low-level wakeup is armed.
    vTaskDelay(pdMS_TO_TICKS(100));
}

void power_idle_enter_deep_sleep(void)
{
    if (s_sleeping) {
        // Second caller (idle timer vs. long-press) just parks here; the
        // first one is already tearing down toward deep sleep.
        vTaskDelay(portMAX_DELAY);
        return;
    }
    s_sleeping = true;

    ESP_LOGI(TAG, "idle %llds, entering deep sleep",
             (long long)power_idle_idle_s());
    ESP_LOGI(TAG, "waiting for button release before sleep");
    wait_for_button_release();

    // All display work happens under a single LVGL lock: destroy the UI
    // and suspend the panel back-to-back. After that nothing remains to
    // flush, so no second lock is needed. (A second lock after Wi-Fi stop
    // timed out reliably — the LVGL task can stay inside
    // lv_timer_handler() while the network tears down — and the old
    // fallback rebooted instead of sleeping.)
    if (bsp_lvgl_lock(1000)) {
        ui_home_destroy();
        warn("ST7789 suspend", bsp_display_prepare_deep_sleep());
        bsp_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "UI teardown without LVGL lock skipped");
    }

    warn("Wi-Fi stop", wifi_mgr_stop());
    warn("CW2017 suspend", bsp_battery_sleep());
    warn("shared I2C pin release", bsp_i2c_prepare_deep_sleep());

    // The button ADC driver left GPIO0 with the digital input buffer
    // disabled (gpio_config_as_analog on this target only gates input,
    // output, and pulls), so the deep-sleep sampler reads a constant LOW
    // and a LOW-level wakeup fires instantly. Re-enable the input first;
    // the external 10 k pullup then holds the released level HIGH. Sleep
    // is terminal (wake reboots and re-inits the button ADC), so no ADC
    // state needs preserving here.
    warn("button pin input restore", gpio_input_enable(GPIO_NUM_0));

    // Any of UP/DOWN/OK pulls GPIO0 (the shared ADC node) below the
    // digital low threshold; deep sleep cannot distinguish which one.
    // C3 deep-sleep GPIO wakeup covers GPIO0..GPIO5; ext0/ext1 do not
    // exist on this target. If GPIO wakeup is unavailable, fall back to
    // a 60 s timer so the device still recovers instead of rebooting.
    esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(
        1ULL << GPIO_NUM_0, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button wakeup unavailable: %s; timer fallback 60 s",
                 esp_err_to_name(err));
        err = esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "timer wakeup failed too: %s, rebooting",
                     esp_err_to_name(err));
            esp_restart();
        }
    } else {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();

    ESP_LOGE(TAG, "esp_deep_sleep_start returned unexpectedly, rebooting");
    esp_restart();
}
