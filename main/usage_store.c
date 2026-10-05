// main/usage_store.c — see usage_store.h.
#include "usage_store.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static usage_snapshot_t s_snapshot;

void usage_store_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
}

void usage_store_set_ok(const usage_info_t *info, time_t now)
{
    if (!info || !s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    s_snapshot.info = *info;
    s_snapshot.has_data = true;
    s_snapshot.fetched_at = now;
    s_snapshot.last_failed = false;
    s_snapshot.last_error[0] = '\0';
    xSemaphoreGive(s_lock);
}

void usage_store_set_failed(const char *reason, time_t now)
{
    if (!s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    (void)now;
    s_snapshot.last_failed = true;
    snprintf(s_snapshot.last_error, sizeof(s_snapshot.last_error), "%s",
             reason ? reason : "fetch failed");
    xSemaphoreGive(s_lock);
}

void usage_store_get(usage_snapshot_t *out)
{
    if (!out || !s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    *out = s_snapshot;
    xSemaphoreGive(s_lock);
}
