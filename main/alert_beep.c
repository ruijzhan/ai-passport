// main/alert_beep.c — see alert_beep.h.
//
// Beep budget: 8 kHz mono keeps a 200 ms tone at 1600 samples
// (3.2 KB). Samples stream in 512-sample stack chunks so no heap or
// large static buffer is needed on this no-PSRAM target.
#include "alert_beep.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "alert_beep";

#define BEEP_HZ 8000
#define BEEP_CHUNK 512
#define BEEP_VOLUME 70
#define BEEP_AMP 6000

static bool s_ready;
static bool s_failed;

static bool beep_ensure(void)
{
    if (s_ready) {
        return true;
    }
    if (s_failed) {
        return false;
    }
    if (bsp_audio_init() != ESP_OK) {
        s_failed = true;
        return false;
    }
    if (bsp_audio_set_format(BEEP_HZ, 16, 1) != ESP_OK) {
        s_failed = true;
        return false;
    }
    bsp_audio_set_volume(BEEP_VOLUME);
    s_ready = true;
    return true;
}

static void beep_tone(int freq_hz, int ms)
{
    if (freq_hz <= 0 || ms <= 0) {
        return;
    }
    const int total = BEEP_HZ * ms / 1000;
    const int period = BEEP_HZ / freq_hz;
    int16_t chunk[BEEP_CHUNK];
    int phase = 0;
    int left = total;
    while (left > 0) {
        int n = left < BEEP_CHUNK ? left : BEEP_CHUNK;
        for (int i = 0; i < n; i++) {
            chunk[i] = (phase < period / 2) ? BEEP_AMP : -BEEP_AMP;
            if (++phase >= period) {
                phase = 0;
            }
        }
        if (bsp_audio_write(chunk, (size_t)n * sizeof(chunk[0])) != ESP_OK) {
            break;
        }
        left -= n;
    }
}

static void beep_gap(int ms)
{
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

void alert_beep_play(alert_level_t level)
{
    if (level == ALERT_NORMAL) {
        return;
    }
    if (!beep_ensure()) {
        return;
    }
    if (level == ALERT_WARN) {
        beep_tone(880, 200);
    } else {
        beep_tone(660, 150);
        beep_gap(80);
        beep_tone(660, 150);
    }
}

void alert_beep_prepare_deep_sleep(void)
{
    esp_err_t err = bsp_audio_sleep();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deep sleep continues: audio sleep failed: %s",
                 esp_err_to_name(err));
    }
    err = bsp_audio_prepare_deep_sleep();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deep sleep continues: audio pin release failed: %s",
                 esp_err_to_name(err));
    }
}
