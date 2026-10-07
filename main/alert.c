// main/alert.c — see alert.h.
#include "alert.h"

#define MASCOT_RUN_MS 70

alert_level_t alert_level_for_percent(int percent)
{
    if (percent >= ALERT_CRIT_PCT) {
        return ALERT_CRIT;
    }
    if (percent >= ALERT_WARN_PCT) {
        return ALERT_WARN;
    }
    return ALERT_NORMAL;
}

alert_level_t alert_level_for_windows(usage_info_t *info)
{
    alert_level_t level = ALERT_NORMAL;
    if (!info) {
        return level;
    }
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        const usage_window_t *w = usage_window_at(info, i);
        if (!w || !w->valid) {
            continue;
        }
        alert_level_t here = alert_level_for_percent(w->percent);
        if (here > level) {
            level = here;
        }
    }
    return level;
}

mascot_cfg_t mascot_config(alert_level_t level, bool fetch_failed)
{
    // Indexed by alert_level_t; the enum is dense from ALERT_NORMAL.
    static const mascot_cfg_t BY_LEVEL[ALERT_CRIT + 1] = {
        { MASCOT_WALK_MS, MASCOT_TINT_GREEN, false },
        { MASCOT_RUN_MS,  MASCOT_TINT_AMBER, false },
        { 0,              MASCOT_TINT_RED,   true  },
    };
    // A failed fetch below the thresholds stops the walk amber: the data
    // is stale, not exhausted.
    static const mascot_cfg_t STALE = { 0, MASCOT_TINT_AMBER, true };
    if (level == ALERT_NORMAL && fetch_failed) {
        return STALE;
    }
    if ((unsigned)level > (unsigned)ALERT_CRIT) {
        return BY_LEVEL[ALERT_NORMAL];
    }
    return BY_LEVEL[level];
}
