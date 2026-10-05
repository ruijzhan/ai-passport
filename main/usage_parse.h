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
