// tests/test_alert.c — host tests for main/alert.c.
#include <assert.h>
#include <stdio.h>

#include "alert.h"

static usage_info_t windows(int p0, bool v0, int p1, bool v1, int p2, bool v2)
{
    usage_info_t info = { 0 };
    info.rolling.percent = p0;
    info.rolling.valid = v0;
    info.weekly.percent = p1;
    info.weekly.valid = v1;
    info.monthly.percent = p2;
    info.monthly.valid = v2;
    return info;
}

int main(void)
{
    // Threshold boundaries.
    assert(alert_level_for_percent(-1) == ALERT_NORMAL);
    assert(alert_level_for_percent(0) == ALERT_NORMAL);
    assert(alert_level_for_percent(79) == ALERT_NORMAL);
    assert(alert_level_for_percent(80) == ALERT_WARN);
    assert(alert_level_for_percent(94) == ALERT_WARN);
    assert(alert_level_for_percent(95) == ALERT_CRIT);
    assert(alert_level_for_percent(100) == ALERT_CRIT);

    // Window aggregation takes the max of valid windows only.
    usage_info_t info = windows(10, true, 20, true, 30, true);
    assert(alert_level_for_windows(&info) == ALERT_NORMAL);
    info = windows(85, true, 20, true, 30, true);
    assert(alert_level_for_windows(&info) == ALERT_WARN);
    info = windows(20, true, 99, true, 30, true);
    assert(alert_level_for_windows(&info) == ALERT_CRIT);
    info = windows(99, false, 99, false, 30, true);
    assert(alert_level_for_windows(&info) == ALERT_NORMAL);
    info = windows(99, false, 99, false, 99, false);
    assert(alert_level_for_windows(&info) == ALERT_NORMAL);
    info = windows(-1, false, -1, false, -1, false);
    assert(alert_level_for_windows(&info) == ALERT_NORMAL);
    // A missing snapshot is not an alarm.
    assert(alert_level_for_windows(NULL) == ALERT_NORMAL);

    // Mascot motion follows the alert state.
    mascot_cfg_t cfg = mascot_config(ALERT_NORMAL, false);
    assert(!cfg.paused && cfg.period_ms == 100 && cfg.tint == MASCOT_TINT_GREEN);
    cfg = mascot_config(ALERT_WARN, false);
    assert(!cfg.paused && cfg.period_ms == 70 && cfg.tint == MASCOT_TINT_AMBER);
    cfg = mascot_config(ALERT_WARN, true);
    assert(!cfg.paused && cfg.tint == MASCOT_TINT_AMBER);
    cfg = mascot_config(ALERT_CRIT, false);
    assert(cfg.paused && cfg.tint == MASCOT_TINT_RED);
    cfg = mascot_config(ALERT_CRIT, true);
    assert(cfg.paused && cfg.tint == MASCOT_TINT_RED);
    cfg = mascot_config(ALERT_NORMAL, true);
    assert(cfg.paused && cfg.tint == MASCOT_TINT_AMBER);
    // A level outside the enum degrades to the normal walk.
    cfg = mascot_config((alert_level_t)99, false);
    assert(!cfg.paused && cfg.period_ms == 100 && cfg.tint == MASCOT_TINT_GREEN);

    puts("alert tests: PASS");
    return 0;
}
