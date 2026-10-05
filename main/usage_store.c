// main/usage_store.c — see usage_store.h.
//
// Persistence: deep sleep wakes by reboot, so RAM is lost. The last good
// snapshot is saved to NVS (namespace "opencode_go", key "usage_snap")
// by usage_store_save() before sleep and reloaded in usage_store_init().
// Writes happen only on explicit save (sleep entry), not on every fetch,
// to avoid NVS flash wear from the 3-minute refresh loop. Failures to
// load/save only drop the cache; they never block boot or sleep.
#include "usage_store.h"

#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "usage_store";

#define USAGE_STORE_NS "opencode_go"
#define USAGE_STORE_KEY "usage_snap"
#define USAGE_STORE_MAGIC 0x4F475553u  // "OGUS"
#define USAGE_STORE_VERSION 1u

// Persisted image. Written/read as a single NVS blob; crc covers every
// field before it so a torn write or version mismatch is rejected.
typedef struct {
    uint32_t magic;
    uint32_t version;
    usage_info_t info;
    int64_t fetched_at;  // epoch seconds, 0 when never
    uint32_t crc;
} usage_persisted_t;

static SemaphoreHandle_t s_lock;
static usage_snapshot_t s_snapshot;

// FNV-1a over every field before crc. Must use offsetof: the struct has
// tail padding after crc, so sizeof(*p) - sizeof(crc) would wrongly include
// the crc field itself (zero at save time, nonzero at load time) and every
// saved blob would fail validation on reload.
static uint32_t persisted_crc(const usage_persisted_t *p)
{
    const uint8_t *bytes = (const uint8_t *)p;
    size_t len = offsetof(usage_persisted_t, crc);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool persisted_sane(usage_persisted_t *p)
{
    if (p->magic != USAGE_STORE_MAGIC || p->version != USAGE_STORE_VERSION) {
        return false;
    }
    if (p->crc != persisted_crc(p)) {
        return false;
    }
    if (p->fetched_at < 0) {
        return false;
    }
    bool any_valid = false;
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        const usage_window_t *w = usage_window_at(&p->info, i);
        if (w->valid) {
            any_valid = true;
            if (w->percent < 0 || w->percent > 100) {
                return false;
            }
            if (w->has_reset && w->resets_at_utc < 0) {
                return false;
            }
        }
    }
    return any_valid;
}

static void load_from_nvs(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(USAGE_STORE_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no cache (open: %s)", esp_err_to_name(err));
        return;
    }
    usage_persisted_t persisted;
    memset(&persisted, 0, sizeof(persisted));
    size_t len = sizeof(persisted);
    err = nvs_get_blob(handle, USAGE_STORE_KEY, &persisted, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(persisted)) {
        ESP_LOGI(TAG, "no cache (read: %s)", esp_err_to_name(err));
        return;
    }
    if (!persisted_sane(&persisted)) {
        ESP_LOGW(TAG, "cached usage rejected (corrupt or empty)");
        return;
    }
    s_snapshot.info = persisted.info;
    s_snapshot.has_data = true;
    s_snapshot.fetched_at = (time_t)persisted.fetched_at;
    s_snapshot.last_failed = false;
    s_snapshot.last_error[0] = '\0';
    ESP_LOGI(TAG, "cached usage restored (fetched_at=%lld)",
             (long long)persisted.fetched_at);
}

void usage_store_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    load_from_nvs();
}

void usage_store_set_ok(const usage_info_t *info, time_t now)
{
    if (!info) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snapshot.info = *info;
    s_snapshot.has_data = true;
    s_snapshot.fetched_at = now;
    s_snapshot.last_failed = false;
    s_snapshot.last_error[0] = '\0';
    xSemaphoreGive(s_lock);
    // No NVS write here: the refresh loop runs every few minutes and each
    // write costs flash wear. usage_store_save() persists on sleep entry.
}

void usage_store_set_failed(const char *reason)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snapshot.last_failed = true;
    snprintf(s_snapshot.last_error, sizeof(s_snapshot.last_error), "%s",
             reason ? reason : "fetch failed");
    xSemaphoreGive(s_lock);
}

void usage_store_get(usage_snapshot_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snapshot;
    xSemaphoreGive(s_lock);
}

void usage_store_save(void)
{
    usage_snapshot_t copy;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy = s_snapshot;
    xSemaphoreGive(s_lock);

    if (!copy.has_data) {
        ESP_LOGI(TAG, "cache save skipped (no data yet)");
        return;
    }
    usage_persisted_t persisted;
    memset(&persisted, 0, sizeof(persisted));
    persisted.magic = USAGE_STORE_MAGIC;
    persisted.version = USAGE_STORE_VERSION;
    persisted.info = copy.info;
    persisted.fetched_at = (int64_t)copy.fetched_at;
    persisted.crc = persisted_crc(&persisted);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(USAGE_STORE_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cache save skipped (open: %s)", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, USAGE_STORE_KEY, &persisted, sizeof(persisted));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cache save failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "cache saved (fetched_at=%lld)",
                 (long long)persisted.fetched_at);
    }
}
