// main/power_idle.c — see power_idle.h.
//
// Deep-sleep order mirrors the validated baseline: battery gauge sleep,
// shared-I2C pin release, LCD suspend under the LVGL lock, then sleep.
// Audio is never initialized by this app, so there is nothing to suspend.
#include "power_idle.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "ui_home.h"
#include "wifi_mgr.h"

static const char *TAG = "power_idle";
static int64_t s_last_activity_us;

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

void power_idle_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "idle %llds, entering deep sleep",
             (long long)power_idle_idle_s());

    if (bsp_lvgl_lock(1000)) {
        ui_home_destroy();
        bsp_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "UI teardown without LVGL lock skipped");
    }

    warn("Wi-Fi stop", wifi_mgr_stop());
    warn("CW2017 suspend", bsp_battery_sleep());
    warn("shared I2C pin release", bsp_i2c_prepare_deep_sleep());

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "cannot stop LVGL flush before deep sleep, rebooting");
        esp_restart();
    }
    warn("ST7789 suspend", bsp_display_prepare_deep_sleep());

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
