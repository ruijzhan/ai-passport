// Host test for the Time demo pure text helpers (no ESP-IDF/LVGL needed).
#include "demo_time_util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    // 2026-10-04 12:34:56, Sunday.
    struct tm t = { 0 };
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 4;
    t.tm_hour = 12;
    t.tm_min = 34;
    t.tm_sec = 56;
    t.tm_wday = 0;
    char date[32] = { 0 };
    char clock[16] = { 0 };
    demo_time_format(t, date, sizeof(date), clock, sizeof(clock));
    assert(strcmp(date, "2026-10-04 Sun") == 0);
    assert(strcmp(clock, "12:34:56") == 0);

    // Midnight pads with zeros; weekend name follows struct tm.
    struct tm midnight = { 0 };
    midnight.tm_year = 125;
    midnight.tm_mon = 0;
    midnight.tm_mday = 1;
    midnight.tm_wday = 3;
    demo_time_format(midnight, date, sizeof(date), clock, sizeof(clock));
    assert(strcmp(date, "2025-01-01 Wed") == 0);
    assert(strcmp(clock, "00:00:00") == 0);

    // Truncation yields empty strings instead of partial output.
    char tiny[4] = { 0 };
    demo_time_format(t, tiny, sizeof(tiny), tiny, sizeof(tiny));
    assert(tiny[0] == '\0');

    // NTP client state mapping.
    assert(strcmp(demo_time_ntp_state(0, 0, 0), "STOPPED") == 0);
    assert(strcmp(demo_time_ntp_state(0, 1, 5), "STOPPED") == 0);
    assert(strcmp(demo_time_ntp_state(1, 0, 0), "WAITING WIFI") == 0);
    assert(strcmp(demo_time_ntp_state(1, 1, 0), "SYNCING") == 0);
    assert(strcmp(demo_time_ntp_state(1, 0, 2), "SYNCED") == 0);
    assert(strcmp(demo_time_ntp_state(1, 1, 2), "SYNCED") == 0);

    // Epoch guard: 1970 and 1999 are unsynced, 2000-01-01 passes.
    assert(!demo_time_is_synced(0));
    assert(!demo_time_is_synced(946684799L));
    assert(demo_time_is_synced(946684800L));
    assert(demo_time_is_synced(1791093296L));

    puts("Time util format/state tests: PASS");
    return 0;
}
