// main/ui_home.c — see ui_home.h. Layout is 240x320 portrait:
//
//   14:22:05            WiFi -58dBm
//   2026-10-06 Tue        BAT 87%
//   ------------------------------
//   5H ROLLING            4%
//   [bar]
//   Reset in 02:15:33
//   WEEKLY ... / MONTHLY ...
//   ------------------------------
//   Updated 12s ago / errors here
//   OK Refresh
#include "ui_home.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "bsp_battery.h"
#include "time_sync.h"
#include "usage_store.h"
#include "wifi_mgr.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi;
static lv_obj_t *s_date;
static lv_obj_t *s_batt;
static lv_obj_t *s_pct[3];
static lv_obj_t *s_bar[3];
static lv_obj_t *s_reset[3];
static lv_obj_t *s_status;
static lv_timer_t *s_timer;

static const char *const TITLES[3] = {
    "5H ROLLING", "WEEKLY", "MONTHLY",
};

static void set_text(lv_obj_t *label, const char *text)
{
    if (label) lv_label_set_text(label, text);
}

static void window_row(int i, const usage_window_t *w, int64_t now,
                       bool time_ok)
{
    char buf[48];
    if (w->valid) {
        snprintf(buf, sizeof(buf), "%d%%", w->percent);
        set_text(s_pct[i], buf);
        lv_bar_set_value(s_bar[i], w->percent, LV_ANIM_OFF);
        // Before NTP sync the wall clock is bogus (1970), so a countdown
        // from cached resets_at_utc would show a huge day count. Keep the
        // cached percent/bar visible immediately and wait for time sync
        // before rendering the reset countdown.
        if (w->has_reset && time_ok) {
            char cd[24];
            usage_format_countdown(now, w->resets_at_utc, cd, sizeof(cd));
            snprintf(buf, sizeof(buf), "Reset in %s", cd);
        } else {
            snprintf(buf, sizeof(buf), "Reset --");
        }
    } else {
        set_text(s_pct[i], "--");
        lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "Reset --");
    }
    set_text(s_reset[i], buf);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    ui_home_refresh();
}

void ui_home_refresh(void)
{
    if (!s_scr) return;

    time_t now = time(NULL);
    struct tm tm_now;
    if (time_sync_done() && localtime_r(&now, &tm_now)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        set_text(s_clock, buf);
        static const char *WD[7] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
        };
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s",
                 tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                 WD[tm_now.tm_wday % 7]);
        set_text(s_date, buf);
    } else {
        set_text(s_clock, "--:--:--");
        set_text(s_date, "Syncing time...");
    }

    char buf[96];
    switch (wifi_mgr_state()) {
    case WIFI_MGR_UP: {
        int rssi = wifi_mgr_rssi();
        if (rssi != 0) snprintf(buf, sizeof(buf), "WiFi %ddBm", rssi);
        else snprintf(buf, sizeof(buf), "WiFi on");
        break;
    }
    case WIFI_MGR_CONNECTING:
        snprintf(buf, sizeof(buf), "WiFi...");
        break;
    default:
        snprintf(buf, sizeof(buf), "WiFi off");
        break;
    }
    set_text(s_wifi, buf);

    int soc = bsp_battery_soc();
    if (soc >= 0) snprintf(buf, sizeof(buf), "BAT %d%%", soc);
    else snprintf(buf, sizeof(buf), "BAT --");
    set_text(s_batt, buf);

    usage_snapshot_t snap;
    usage_store_get(&snap);
    const usage_window_t *wins[3] = {
        &snap.info.rolling, &snap.info.weekly, &snap.info.monthly
    };
    int64_t now_utc = (int64_t)now;
    bool time_ok = time_sync_done();
    for (int i = 0; i < 3; i++) window_row(i, wins[i], now_utc, time_ok);

    if (!snap.has_data) {
        set_text(s_status, snap.last_failed ? snap.last_error : "No data yet");
    } else if (snap.last_failed) {
        snprintf(buf, sizeof(buf), "Update failed, last data kept: %.48s",
                 snap.last_error);
        set_text(s_status, buf);
    } else if (time_ok && snap.fetched_at > 0) {
        long age = (long)(now - snap.fetched_at);
        if (age < 0) age = 0;
        if (age < 90) snprintf(buf, sizeof(buf), "Updated %lds ago", age);
        else snprintf(buf, sizeof(buf), "Updated %ldm ago", age / 60);
        set_text(s_status, buf);
    } else {
        // Cached snapshot restored after wake, fresh fetch pending.
        set_text(s_status, "Cached data");
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y,
                            const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    return label;
}

void ui_home_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_radius(s_scr, 0, 0);

    s_clock = make_label(s_scr, 8, 4, &lv_font_montserrat_20);
    s_wifi = make_label(s_scr, 8, 4, &lv_font_montserrat_14);
    lv_obj_set_width(s_wifi, 224);
    lv_obj_set_style_text_align(s_wifi, LV_TEXT_ALIGN_RIGHT, 0);

    s_date = make_label(s_scr, 8, 30, &lv_font_montserrat_14);
    s_batt = make_label(s_scr, 8, 30, &lv_font_montserrat_14);
    lv_obj_set_width(s_batt, 224);
    lv_obj_set_style_text_align(s_batt, LV_TEXT_ALIGN_RIGHT, 0);

    int y = 56;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *title = make_label(s_scr, 8, y, &lv_font_montserrat_14);
        lv_label_set_text(title, TITLES[i]);
        s_pct[i] = make_label(s_scr, 8, y, &lv_font_montserrat_14);
        lv_obj_set_width(s_pct[i], 224);
        lv_obj_set_style_text_align(s_pct[i], LV_TEXT_ALIGN_RIGHT, 0);

        s_bar[i] = lv_bar_create(s_scr);
        lv_obj_set_size(s_bar[i], 224, 12);
        lv_obj_set_pos(s_bar[i], 8, y + 20);
        lv_bar_set_range(s_bar[i], 0, 100);
        lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);

        s_reset[i] = make_label(s_scr, 8, y + 36, &lv_font_montserrat_14);
        y += 58;
    }

    s_status = lv_label_create(s_scr);
    lv_obj_set_pos(s_status, 8, 232);
    lv_obj_set_width(s_status, 224);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status,
                               lv_palette_main(LV_PALETTE_GREY), 0);

    lv_obj_t *hint = make_label(s_scr, 8, 292, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(hint,
                               lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(hint, "OK Refresh  OK-hold Sleep");

    lv_screen_load(s_scr);
    s_timer = lv_timer_create(tick, 1000, NULL);
    ui_home_refresh();
}

void ui_home_destroy(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_clock = s_wifi = s_date = s_batt = s_status = NULL;
        for (int i = 0; i < 3; i++) {
            s_pct[i] = s_bar[i] = s_reset[i] = NULL;
        }
    }
}
