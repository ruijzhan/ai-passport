// main/usage_store.h — latest fetched usage snapshot shared by the
// network worker (writer) and the LVGL timer (reader).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "usage_parse.h"

typedef struct {
    usage_info_t info;
    bool has_data;          // at least one successful fetch
    time_t fetched_at;      // 0 when never
    bool last_failed;       // most recent attempt failed; old data retained
    char last_error[64];    // short reason of the most recent failure
} usage_snapshot_t;

// Thread-safe snapshot access. Short critical sections only.
void usage_store_init(void);
void usage_store_set_ok(const usage_info_t *info, time_t now);
void usage_store_set_failed(const char *reason, time_t now);
void usage_store_get(usage_snapshot_t *out);
