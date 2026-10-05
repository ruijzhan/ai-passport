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

    // UTC formatting and time parsing round-trip.
    usage_format_utc(0, buf, sizeof(buf));
    assert(strcmp(buf, "1970-01-01 00:00:00") == 0);
    assert(usage_parse_time("1970-01-01T00:00:00Z") == 0);
    assert(usage_parse_time("not-a-time") < 0);
    assert(usage_parse_time("2026-02-30T00:00:00Z") < 0);
    assert(usage_parse_time("2026-13-01T00:00:00Z") < 0);
    usage_format_utc(-5, buf, sizeof(buf));
    assert(strcmp(buf, "--") == 0);

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

    puts("usage_parse tests: PASS");
    return 0;
}
