// main/ui_home.c — see ui_home.h. Layout is 240x320 portrait:
//
//   14:22:05            [bars]
//   2026-10-06 Tue   [icon] 87%
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

#include "esp_log.h"
#include "lvgl.h"
#include "bsp_battery.h"
#include "time_sync.h"
#include "usage_store.h"
#include "wifi_mgr.h"
#include "wifi_signal.h"

#define WIFI_BARS_COUNT 4
// Right edge pulled 8px in from 232: the rounded-corner mask
// (BSP_LVGL_SCREEN_RADIUS=30) clips the top-right bars at x>224.
#define WIFI_BARS_RIGHT 224
#define WIFI_BARS_BOTTOM 20
#define WIFI_BAR_W 6
#define WIFI_BAR_GAP 3

// Battery group is right-aligned at x=232: [icon] 4px "87%".
// Text width is measured at runtime so 8%..100% all hug the icon.
#define BATT_GROUP_RIGHT 232
#define BATT_ICON_TEXT_GAP 4
#define BATT_FRAME_Y 32
#define BATT_FRAME_W 25
#define BATT_FRAME_H 13
#define BATT_TIP_W 3
#define BATT_TIP_H 6
#define BATT_FILL_PAD 2
#define BATT_FILL_MAX_W (BATT_FRAME_W - 2 * BATT_FILL_PAD)
#define BATT_FILL_MAX_H (BATT_FRAME_H - 2 * BATT_FILL_PAD)
#define BATT_LOW_SOC 30
#define BATT_CRIT_SOC 15

static const char *TAG = "ui_home";

static lv_obj_t *s_scr;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi_bars[WIFI_BARS_COUNT];
static unsigned s_tick;
static int s_last_wifi_key = 0x7FFFFFFF;
static lv_obj_t *s_date;
static lv_obj_t *s_batt;
static lv_obj_t *s_batt_frame;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_tip;
static lv_obj_t *s_pct[3];
static lv_obj_t *s_bar[3];
static lv_obj_t *s_marker[3];
static lv_obj_t *s_reset[3];
static lv_obj_t *s_status;
static lv_timer_t *s_timer;

// Pace marker: 2px black line, same height as the 12px bar, centered on
// the time-progress position so usage-vs-average is directly comparable.
#define USAGE_MARKER_W 2
#define USAGE_MARKER_H 12
#define USAGE_MARKER_OVERHANG ((USAGE_MARKER_H - 12) / 2)

static const char *const TITLES[3] = {
    "5H ROLLING", "WEEKLY", "MONTHLY",
};

static void set_text(lv_obj_t *label, const char *text)
{
    if (label) lv_label_set_text(label, text);
}

static int64_t window_period_s(int i, int64_t now_utc)
{
    switch (i) {
    case 0:
        return USAGE_ROLLING_PERIOD_S;
    case 1:
        return USAGE_WEEKLY_PERIOD_S;
    case 2:
        return usage_month_period_s(now_utc);
    default:
        return -1;
    }
}

static void marker_update(int i, const usage_window_t *w, int64_t now,
                          bool time_ok)
{
    if (i < 0 || i >= 3 || !s_marker[i] || !s_bar[i]) {
        return;
    }
    if (!w->valid || !w->has_reset || !time_ok) {
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int pct = usage_time_progress(now, w->resets_at_utc,
                                  window_period_s(i, now));
    if (pct < 0) {
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
    // Center the 2px line on the time-progress position.
    lv_coord_t bar_x = lv_obj_get_x(s_bar[i]);
    lv_coord_t bar_y = lv_obj_get_y(s_bar[i]);
    lv_coord_t bar_w = lv_obj_get_width(s_bar[i]);
    lv_coord_t mx = bar_x + (bar_w * pct) / 100 - USAGE_MARKER_W / 2;
    lv_coord_t my = bar_y - USAGE_MARKER_OVERHANG;
    lv_obj_set_pos(s_marker[i], mx, my);
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
    marker_update(i, w, now, time_ok);
}

static void wifi_bars_set(int lit, bool weak)
{
    lv_color_t active = weak ? lv_palette_main(LV_PALETTE_RED) :
                               lv_palette_main(LV_PALETTE_BLUE);
    lv_color_t dim = lv_palette_lighten(LV_PALETTE_GREY, 2);
    for (int i = 0; i < WIFI_BARS_COUNT; i++) {
        if (!s_wifi_bars[i]) {
            continue;
        }
        bool on = lit > 0 && i < lit;
        lv_obj_set_style_bg_color(s_wifi_bars[i], on ? active : dim, 0);
    }
}

static void batt_icon_set(int soc)
{
    if (!s_batt_frame || !s_batt_fill || !s_batt_tip) {
        return;
    }
    lv_color_t fill;
    int fill_w = 0;
    if (soc < 0) {
        fill = lv_palette_lighten(LV_PALETTE_GREY, 2);
    } else {
        if (soc < BATT_CRIT_SOC) {
            fill = lv_palette_main(LV_PALETTE_RED);
        } else if (soc < BATT_LOW_SOC) {
            fill = lv_palette_main(LV_PALETTE_AMBER);
        } else {
            fill = lv_palette_main(LV_PALETTE_GREEN);
        }
        if (soc > 100) soc = 100;
        if (soc < 0) soc = 0;
        fill_w = (BATT_FILL_MAX_W * soc) / 100;
    }
    lv_obj_set_style_bg_color(s_batt_fill, fill, 0);
    lv_obj_set_size(s_batt_fill, fill_w, BATT_FILL_MAX_H);
}

static void batt_layout(void)
{
    if (!s_batt || !s_batt_frame || !s_batt_fill || !s_batt_tip) {
        return;
    }
    // Label auto-sizes to its text; measure it, then pin text right
    // edge to the screen edge and park the icon just left of it.
    lv_obj_update_layout(s_batt);
    lv_coord_t text_w = lv_obj_get_width(s_batt);
    lv_coord_t text_x = BATT_GROUP_RIGHT - text_w;
    lv_obj_set_pos(s_batt, text_x, 30);
    lv_coord_t frame_x = text_x - BATT_ICON_TEXT_GAP -
        (BATT_FRAME_W + 2 + BATT_TIP_W);
    lv_obj_set_pos(s_batt_frame, frame_x, BATT_FRAME_Y);
    lv_obj_set_pos(s_batt_fill, frame_x + BATT_FILL_PAD,
                   BATT_FRAME_Y + BATT_FILL_PAD);
    lv_obj_set_pos(s_batt_tip, frame_x + BATT_FRAME_W + 2,
                   BATT_FRAME_Y + (BATT_FRAME_H - BATT_TIP_H) / 2);
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
    s_tick++;
    // Top-right shows bars only (no "WiFi" text): lit count = signal
    // level, red = weak, animated = connecting, all dim = off.
    int lit = -1;
    bool weak = false;
    switch (wifi_mgr_state()) {
    case WIFI_MGR_UP: {
        int rssi = wifi_mgr_rssi();
        int level = wifi_signal_level(rssi);
        if (level < 0) {
            lit = -1;
        } else {
            if (level <= 1) {
                weak = true;
            }
            lit = level;
        }
        int key = (WIFI_MGR_UP << 16) | (level + 1);
        if (key != s_last_wifi_key) {
            s_last_wifi_key = key;
            ESP_LOGI(TAG, "wifi up rssi=%d level=%d", rssi, level);
        }
        break;
    }
    case WIFI_MGR_CONNECTING:
        lit = wifi_signal_anim_level(s_tick);
        if (s_last_wifi_key != (int)WIFI_MGR_CONNECTING) {
            s_last_wifi_key = (int)WIFI_MGR_CONNECTING;
            ESP_LOGI(TAG, "wifi connecting");
        }
        break;
    default:
        lit = 0;
        if (s_last_wifi_key != (int)WIFI_MGR_DOWN) {
            s_last_wifi_key = (int)WIFI_MGR_DOWN;
            ESP_LOGI(TAG, "wifi down");
        }
        break;
    }
    wifi_bars_set(lit, weak);

    int soc = bsp_battery_soc();
    if (soc >= 0) snprintf(buf, sizeof(buf), "%d%%", soc);
    else snprintf(buf, sizeof(buf), "--");
    set_text(s_batt, buf);
    batt_icon_set(soc);
    batt_layout();

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

    // Top-left clock is inset past the rounded-corner mask
    // (BSP_LVGL_SCREEN_RADIUS=30 hides x<16 at y=4).
    s_clock = make_label(s_scr, 18, 4, &lv_font_montserrat_20);

    int bars_x0 = WIFI_BARS_RIGHT -
        (WIFI_BARS_COUNT * WIFI_BAR_W + (WIFI_BARS_COUNT - 1) * WIFI_BAR_GAP);
    for (int i = 0; i < WIFI_BARS_COUNT; i++) {
        int h = 5 + 3 * i;
        lv_obj_t *bar = lv_obj_create(s_scr);
        lv_obj_set_size(bar, WIFI_BAR_W, h);
        lv_obj_set_pos(bar, bars_x0 + i * (WIFI_BAR_W + WIFI_BAR_GAP),
                       WIFI_BARS_BOTTOM - h);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar,
                                  lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
        s_wifi_bars[i] = bar;
    }
    s_tick = 0;
    s_last_wifi_key = 0x7FFFFFFF;

    s_date = make_label(s_scr, 8, 30, &lv_font_montserrat_14);
    // Percent label auto-sizes to its text; batt_layout() pins the
    // whole group to the right edge on every refresh.
    s_batt = make_label(s_scr, BATT_GROUP_RIGHT - 48, 30,
                        &lv_font_montserrat_14);

    s_batt_frame = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_frame, BATT_FRAME_W, BATT_FRAME_H);
    lv_obj_set_pos(s_batt_frame, 150, BATT_FRAME_Y);
    lv_obj_set_style_radius(s_batt_frame, 2, 0);
    lv_obj_set_style_bg_opa(s_batt_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_batt_frame, 1, 0);
    lv_obj_set_style_border_color(s_batt_frame,
                                  lv_palette_main(LV_PALETTE_GREY), 0);

    s_batt_fill = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_fill, 0, BATT_FILL_MAX_H);
    lv_obj_set_pos(s_batt_fill, 150 + BATT_FILL_PAD,
                   BATT_FRAME_Y + BATT_FILL_PAD);
    lv_obj_set_style_radius(s_batt_fill, 1, 0);
    lv_obj_set_style_border_width(s_batt_fill, 0, 0);
    lv_obj_set_style_bg_color(s_batt_fill,
                              lv_palette_main(LV_PALETTE_GREEN), 0);

    s_batt_tip = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_tip, BATT_TIP_W, BATT_TIP_H);
    lv_obj_set_pos(s_batt_tip, 150 + BATT_FRAME_W + 2,
                   BATT_FRAME_Y + (BATT_FRAME_H - BATT_TIP_H) / 2);
    lv_obj_set_style_radius(s_batt_tip, 1, 0);
    lv_obj_set_style_border_width(s_batt_tip, 0, 0);
    lv_obj_set_style_bg_color(s_batt_tip,
                              lv_palette_main(LV_PALETTE_GREY), 0);

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

        s_marker[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_marker[i], USAGE_MARKER_W, USAGE_MARKER_H);
        lv_obj_set_pos(s_marker[i], 8, y + 20 - USAGE_MARKER_OVERHANG);
        lv_obj_set_style_radius(s_marker[i], 0, 0);
        lv_obj_set_style_border_width(s_marker[i], 0, 0);
        lv_obj_set_style_bg_color(s_marker[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_marker[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);

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
        s_clock = s_date = s_batt = s_status = NULL;
        s_batt_frame = s_batt_fill = s_batt_tip = NULL;
        for (int i = 0; i < 3; i++) {
            s_pct[i] = s_bar[i] = s_reset[i] = s_marker[i] = NULL;
        }
        for (int i = 0; i < WIFI_BARS_COUNT; i++) {
            s_wifi_bars[i] = NULL;
        }
    }
}
