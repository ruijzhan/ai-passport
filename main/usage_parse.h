// main/usage_parse.h — OpenCode Go usage response parsing and formatting.
//
// Pure C11, no ESP-IDF or LVGL dependencies, so host tests can cover it.
// Numbers follow the API contract: percent is the *used* percentage and a
// window is usable only when status == "ok" and percent is numeric.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool valid;              // status == "ok" and percent parsed
    int percent;             // used percent, clamped to 0..100
    int64_t resets_at_utc;   // epoch seconds, -1 when unknown
    bool has_reset;
} usage_window_t;

typedef struct {
    usage_window_t rolling;  // 5-hour rolling window
    usage_window_t weekly;   // week, Monday UTC reset
    usage_window_t monthly;  // billing month
} usage_info_t;

// Parse the body of GET /zen/go/v1/usage. Never fails hard: missing or
// malformed fields leave the corresponding window invalid.
void usage_parse(const char *json, usage_info_t *out);

// Parse "2026-08-13T16:27:38.287Z" (fraction and 'Z' optional) to epoch
// seconds. Returns -1 on error. Timezone independent.
int64_t usage_parse_time(const char *iso8601);

// Format the remaining time until a reset as "HH:MM:SS" or "Nd HH:MM:SS".
// reset_utc < 0 yields "--"; a passed reset yields "00:00:00".
void usage_format_countdown(int64_t now_utc, int64_t reset_utc,
                            char *buf, size_t len);

// Format an epoch as "YYYY-MM-DD HH:MM:SS" in UTC, "--" when negative.
void usage_format_utc(int64_t epoch, char *buf, size_t len);

// Fixed cycle lengths, in seconds.
#define USAGE_ROLLING_PERIOD_S (5 * 3600)
#define USAGE_WEEKLY_PERIOD_S (7 * 86400)

// Days in the calendar month containing epoch_utc (UTC): 28..31.
// Returns -1 when epoch_utc is negative.
int usage_days_in_month(int64_t epoch_utc);

// Monthly cycle length: days in the month containing now_utc, in seconds.
// Returns -1 when now_utc is negative.
int64_t usage_month_period_s(int64_t now_utc);

// Time progress 0..100: elapsed / period, where
// elapsed = period - (reset_utc - now_utc). 100 means the cycle has fully
// elapsed, 0 means it just started. Returns -1 when the reset is unknown
// (reset_utc < 0) or the period is invalid (period_s <= 0).
int usage_time_progress(int64_t now_utc, int64_t reset_utc, int64_t period_s);
