// main/alert.h — quota alert levels and mascot motion without ESP-IDF/LVGL.
//
// Thresholds and the mascot timing/tint stay testable on the host;
// main/ui_home.c renders colors with LVGL, main/main.c plays the beep
// from the worker task.
#pragma once

#include <stdbool.h>

#include "usage_parse.h"

#define ALERT_WARN_PCT 80
#define ALERT_CRIT_PCT 95

typedef enum {
    ALERT_NORMAL = 0,
    ALERT_WARN,
    ALERT_CRIT,
} alert_level_t;

// Level for one used-percent value. Negative (unknown) is NORMAL so an
// empty cache never alarms.
alert_level_t alert_level_for_percent(int percent);

// Level for the quota windows of one usage_info_t: the highest level
// among the valid windows, walked over USAGE_WINDOW_COUNT so a new window
// is never silently dropped. No valid window yields NORMAL. Read-only;
// the non-const parameter matches usage_window_at().
alert_level_t alert_level_for_windows(usage_info_t *info);

typedef enum {
    MASCOT_TINT_GREEN = 0,
    MASCOT_TINT_AMBER,
    MASCOT_TINT_RED,
} mascot_tint_t;

typedef struct {
    int period_ms;      // frame step; 0 when paused
    mascot_tint_t tint;
    bool paused;
} mascot_cfg_t;

// Motion for the current alert state. CRIT always stops red; a failed
// fetch with low usage stops amber (stale, not exhausted); WARN runs
// faster amber; otherwise the normal green walk.
mascot_cfg_t mascot_config(alert_level_t level, bool fetch_failed);
