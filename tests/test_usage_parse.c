// tests/test_usage_parse.c — host tests for main/usage_parse.c.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usage_parse.h"

static const char SAMPLE[] =
    "{ \"usage\": {"
    " \"rolling\": { \"status\": \"ok\", \"percent\": 4, "
    "   \"resetsAt\": \"2026-08-13T16:27:38.287Z\" },"
    " \"weekly\": { \"status\": \"ok\", \"percent\": 3, "
    "   \"resetsAt\": \"2026-08-17T00:00:00.287Z\" },"
    " \"monthly\": { \"status\": \"ok\", \"percent\": 1, "
    "   \"resetsAt\": \"2026-09-13T06:06:01.287Z\" } } }";

int main(void)
{
    usage_info_t u;
    char buf[32];

    usage_parse(SAMPLE, &u);
    assert(u.rolling.valid && u.rolling.percent == 4 && u.weekly.valid &&
           u.weekly.percent == 3 && u.monthly.valid && u.monthly.percent == 1);
    assert(u.rolling.has_reset && u.weekly.has_reset && u.monthly.has_reset);
    assert(u.rolling.resets_at_utc == usage_parse_time("2026-08-13T16:27:38.287Z"));
    assert(u.weekly.resets_at_utc == usage_parse_time("2026-08-17T00:00:00Z"));
    assert(u.monthly.resets_at_utc == usage_parse_time("2026-09-13T06:06:01Z"));

    // Countdown formatting.
    usage_format_countdown(1000, 1000 + 7266, buf, sizeof(buf));
    assert(strcmp(buf, "02:01:06") == 0);
    usage_format_countdown(0, 90061, buf, sizeof(buf));
    assert(strcmp(buf, "1d 01:01:01") == 0);
    usage_format_countdown(5000, -1, buf, sizeof(buf));
    assert(strcmp(buf, "--") == 0);
    usage_format_countdown(5000, 4000, buf, sizeof(buf));
    assert(strcmp(buf, "00:00:00") == 0);

    // Time parsing.
    assert(usage_parse_time("1970-01-01T00:00:00Z") == 0);
    assert(usage_parse_time("not-a-time") < 0);
    assert(usage_parse_time("2026-02-30T00:00:00Z") < 0);
    assert(usage_parse_time("2026-13-01T00:00:00Z") < 0);

    // Degraded inputs leave windows invalid without crashing.
    usage_parse(NULL, &u);
    assert(!u.rolling.valid && !u.weekly.valid && !u.monthly.valid);
    usage_parse("{}", &u);
    assert(!u.rolling.valid && !u.weekly.valid && !u.monthly.valid);
    usage_parse("{broken", &u);
    assert(!u.rolling.valid);

    // Non-ok status and non-numeric percent are unusable.
    usage_parse("{\"rolling\":{\"status\":\"error\",\"percent\":9}}", &u);
    assert(!u.rolling.valid);
    usage_parse("{\"rolling\":{\"status\":\"ok\"}}", &u);
    assert(!u.rolling.valid);
    usage_parse("{\"weekly\":{\"status\":\"ok\",\"percent\":\"3\"}}", &u);
    assert(!u.weekly.valid);

    // Missing resetsAt keeps the window usable for percent display.
    usage_parse("{\"monthly\":{\"status\":\"ok\",\"percent\":42}}", &u);
    assert(u.monthly.valid && u.monthly.percent == 42 && !u.monthly.has_reset);

    // Subscription-anchored monthly period: the same day one calendar month
    // before the reset, clamped when that day does not exist.
    const int64_t nov_reset = usage_parse_time("2026-11-17T03:02:59Z");
    assert(usage_month_period_to_reset(nov_reset) == 31 * 86400);
    assert(usage_month_period_to_reset(
               usage_parse_time("2026-10-05T12:00:00Z")) == 30 * 86400);
    assert(usage_month_period_to_reset(
               usage_parse_time("2026-03-31T00:00:00Z")) == 31 * 86400);
    assert(usage_month_period_to_reset(
               usage_parse_time("2024-03-31T00:00:00Z")) == 31 * 86400);
    assert(usage_month_period_to_reset(
               usage_parse_time("2026-03-30T00:00:00Z")) == 30 * 86400);
    assert(usage_month_period_to_reset(
               usage_parse_time("2026-01-15T00:00:00Z")) == 31 * 86400);
    assert(usage_month_period_to_reset(
               usage_parse_time("2026-03-01T00:00:00Z")) == 28 * 86400);
    assert(usage_month_period_to_reset(-1) < 0);

    // The window table drives the parser, store validator, and UI rows
    // in the same order; the accessor maps index -> struct field.
    assert(USAGE_WINDOW_COUNT == 3);
    assert(strcmp(USAGE_WINDOWS[0].json_key, "\"rolling\"") == 0);
    assert(strcmp(USAGE_WINDOWS[1].json_key, "\"weekly\"") == 0);
    assert(strcmp(USAGE_WINDOWS[2].json_key, "\"monthly\"") == 0);
    assert(strcmp(USAGE_WINDOWS[0].title, "5H ROLLING") == 0);
    assert(strcmp(USAGE_WINDOWS[2].title, "MONTHLY") == 0);
    assert(usage_window_at(&u, 0) == &u.rolling);
    assert(usage_window_at(&u, 2) == &u.monthly);
    assert(usage_window_at(&u, USAGE_WINDOW_COUNT) == NULL);
    assert(usage_window_at(NULL, 0) == NULL);
    assert(usage_window_period_to_reset(0, nov_reset) ==
           USAGE_ROLLING_PERIOD_S);
    assert(usage_window_period_to_reset(1, nov_reset) ==
           USAGE_WEEKLY_PERIOD_S);
    assert(usage_window_period_to_reset(2, nov_reset) == 31 * 86400);
    assert(usage_window_period_to_reset(2, -1) < 0);
    assert(usage_window_period_to_reset(USAGE_WINDOW_COUNT, nov_reset) < 0);

    // Time progress: elapsed / period.
    assert(usage_time_progress(1000, 1000 + USAGE_ROLLING_PERIOD_S,
                               USAGE_ROLLING_PERIOD_S) == 0);
    assert(usage_time_progress(1000, 1000 + USAGE_ROLLING_PERIOD_S / 2,
                               USAGE_ROLLING_PERIOD_S) == 50);
    assert(usage_time_progress(1000, 1000, USAGE_ROLLING_PERIOD_S) == 100);
    assert(usage_time_progress(1000, 500, USAGE_ROLLING_PERIOD_S) == 100);
    assert(usage_time_progress(1000, 1000 + 2 * USAGE_ROLLING_PERIOD_S,
                               USAGE_ROLLING_PERIOD_S) == 0);
    assert(usage_time_progress(1000, -1, USAGE_ROLLING_PERIOD_S) < 0);
    assert(usage_time_progress(1000, 2000, 0) < 0);
    assert(usage_time_progress(1000, 2000, -5) < 0);

    // Month-end projection: used * period / elapsed at the cycle-to-date
    // average rate. Halfway through a 30-day cycle at 50% projects 100%.
    const int64_t base = 1000;
    const int64_t day = 86400;
    assert(usage_projected_percent(50, base + 15 * day, base + 30 * day,
                                   30 * day) == 100);
    assert(usage_projected_percent(41, base + 16 * day, base + 30 * day,
                                   30 * day) == 77);
    assert(usage_projected_percent(0, base + day, base + 30 * day,
                                   30 * day) == 0);
    assert(usage_projected_percent(80, base + 24 * day, base + 30 * day,
                                   30 * day) == 100);
    // A tiny elapsed time would overflow the display, so it caps.
    assert(usage_projected_percent(100, base + 1, base + 30 * day,
                                   30 * day) == USAGE_PROJECT_CAP);
    // No basis: passed/unknown reset, no elapsed time, invalid period, or
    // a negative usage value.
    assert(usage_projected_percent(50, base, base, 30 * day) < 0);
    assert(usage_projected_percent(50, base, -1, 30 * day) < 0);
    assert(usage_projected_percent(50, base, base + 30 * day, 30 * day) < 0);
    assert(usage_projected_percent(50, base, base + 30 * day, 0) < 0);
    assert(usage_projected_percent(-1, base, base + 30 * day, 30 * day) < 0);

    // Realistic anchor: 41% used 14 days before the Nov 17 reset projects
    // 75% of the quota at this pace.
    assert(usage_projected_percent(
               41, usage_parse_time("2026-11-03T03:02:59Z"), nov_reset,
               usage_month_period_to_reset(nov_reset)) == 75);

    puts("usage_parse tests: PASS");
    return 0;
}
