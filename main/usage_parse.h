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

// The three quota windows in one place, so the parser, the store
// validator, and the home-screen rows cannot drift apart. Index order is
// the contract: it matches usage_info_t's fields above.
#define USAGE_WINDOW_COUNT 3
typedef struct {
    const char *json_key;   // quoted key as the scanner expects, e.g. "\"rolling\""
    const char *title;      // home-screen row title
} usage_window_desc_t;
extern const usage_window_desc_t USAGE_WINDOWS[USAGE_WINDOW_COUNT];

// Window i of info (rolling, weekly, monthly); NULL on a bad index.
usage_window_t *usage_window_at(usage_info_t *info, int i);

// Cycle length of window i ending at reset_utc. Monthly is the actual
// calendar-month span before the reset (same day-of-month, clamped to the
// previous month's last day), which matches subscription-anchored resets.
// -1 on a bad index or negative reset.
int64_t usage_window_period_to_reset(int i, int64_t reset_utc);

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

// Fixed cycle lengths, in seconds.
#define USAGE_ROLLING_PERIOD_S (5 * 3600)
#define USAGE_WEEKLY_PERIOD_S (7 * 86400)

// Projection display cap: a value above this saturates so the readout
// stays three digits.
#define USAGE_PROJECT_CAP 999

// Monthly cycle length ending at reset_utc: the same day-of-month one
// calendar month earlier (clamped to the previous month's last day) up to
// reset_utc. Returns -1 when reset_utc is negative.
int64_t usage_month_period_to_reset(int64_t reset_utc);

// Time progress 0..100: elapsed / period, where
// elapsed = period - (reset_utc - now_utc). 100 means the cycle has fully
// elapsed, 0 means it just started. Returns -1 when the reset is unknown
// (reset_utc < 0) or the period is invalid (period_s <= 0).
int usage_time_progress(int64_t now_utc, int64_t reset_utc, int64_t period_s);

// Project used percent onto the full cycle at the cycle-to-date average
// rate: used * period_s / elapsed, using the same elapsed definition as
// usage_time_progress(). Returns -1 when there is no basis (unknown or
// passed reset, invalid period, or no elapsed time). Values above 100 mean
// the current pace exhausts the quota before the reset; the result is
// capped at USAGE_PROJECT_CAP for display.
int usage_projected_percent(int used_percent, int64_t now_utc,
                            int64_t reset_utc, int64_t period_s);
