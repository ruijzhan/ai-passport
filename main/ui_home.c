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

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
// The UI timer ticks at 1 Hz; both sensors are sampled far slower than
// that because each read is a synchronous bus transaction for a value
// that changes on a seconds-to-minutes scale.
#define WIFI_RSSI_SAMPLE_TICKS 10
#define BATT_SAMPLE_TICKS 30

// Shared geometry for the quota rows and the status line.
#define CONTENT_W 224
#define BAR_H 12

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

static lv_obj_t *s_scr;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi_bars[WIFI_BARS_COUNT];
static unsigned s_tick;
// Sampled sensor caches (see WIFI_RSSI_SAMPLE_TICKS/BATT_SAMPLE_TICKS).
// s_rssi/s_wifi_up resample on a Wi-Fi state transition so bars appear
// the moment the link comes up; s_soc retries every tick while the
// gauge has not answered yet (bsp_battery_init may still be running).
static bool s_wifi_up;
static int s_rssi = WIFI_MGR_RSSI_UNKNOWN;
static int s_soc = -1;
static lv_obj_t *s_date;
static lv_obj_t *s_batt;
static lv_obj_t *s_batt_frame;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_tip;
static lv_obj_t *s_pct[USAGE_WINDOW_COUNT];
static lv_obj_t *s_bar[USAGE_WINDOW_COUNT];
static lv_obj_t *s_marker[USAGE_WINDOW_COUNT];
static lv_obj_t *s_reset[USAGE_WINDOW_COUNT];
static lv_obj_t *s_status;
static lv_timer_t *s_timer;
// Last rendered values; INT_MIN forces the first refresh after create() to
// draw. Re-setting an unchanged label/style reallocates LVGL text or
// invalidates the area, so identical values are skipped.
static int s_last_soc = INT_MIN;
static int s_last_lit = INT_MIN;
static bool s_last_weak;

// Pace marker: 2px black line spanning the bar height, centered on the
// time-progress position so usage-vs-average is directly comparable.
#define USAGE_MARKER_W 2

static void set_text(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void marker_update(int i, const usage_window_t *w, int64_t now,
                          bool time_ok)
{
    if (i < 0 || i >= USAGE_WINDOW_COUNT || !s_marker[i] || !s_bar[i]) {
        return;
    }
    if (!w->valid || !w->has_reset || !time_ok) {
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int pct = usage_time_progress(now, w->resets_at_utc,
                                  usage_window_period_s(i, now));
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
    lv_obj_set_pos(s_marker[i], mx, bar_y);
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

static void ui_home_refresh(void);

static void tick(lv_timer_t *timer)
{
    (void)timer;
    ui_home_refresh();
}

static void ui_home_refresh(void)
{
    if (!s_scr) return;

    time_t now = time(NULL);
    struct tm tm_now;
    if (time_sync_done() && localtime_r(&now, &tm_now)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        set_text(s_clock, buf);
        strftime(buf, sizeof(buf), "%Y-%m-%d %a", &tm_now);
        set_text(s_date, buf);
    } else {
        set_text(s_clock, "--:--:--");
        set_text(s_date, "Syncing time...");
    }

    char buf[96];
    s_tick++;
    // Top-right shows bars only (no "WiFi" text): lit count = signal
    // level, red = weak, animated = connecting, all dim = off. State
    // transitions (connect/disconnect) are logged by wifi_mgr.
    int lit = 0;
    bool weak = false;
    switch (wifi_mgr_state()) {
    case WIFI_MGR_UP:
        if (!s_wifi_up || s_tick % WIFI_RSSI_SAMPLE_TICKS == 0) {
            s_wifi_up = true;
            s_rssi = wifi_mgr_rssi();
        }
        lit = wifi_signal_level(s_rssi);
        weak = lit <= 1;
        break;
    case WIFI_MGR_CONNECTING:
        s_wifi_up = false;
        lit = wifi_signal_anim_level(s_tick);
        break;
    default:
        s_wifi_up = false;
        lit = 0;
        break;
    }
    if (lit != s_last_lit || weak != s_last_weak) {
        s_last_lit = lit;
        s_last_weak = weak;
        wifi_bars_set(lit, weak);
    }

    if (s_soc < 0 || s_tick % BATT_SAMPLE_TICKS == 0) {
        int soc = bsp_battery_soc();
        if (soc >= 0) s_soc = soc;  // keep the last good value on a flaky read
    }
    if (s_soc != s_last_soc) {
        s_last_soc = s_soc;
        if (s_soc >= 0) snprintf(buf, sizeof(buf), "%d%%", s_soc);
        else snprintf(buf, sizeof(buf), "--");
        set_text(s_batt, buf);
        batt_icon_set(s_soc);
        batt_layout();
    }

    usage_snapshot_t snap;
    usage_store_get(&snap);
    int64_t now_utc = (int64_t)now;
    bool time_ok = time_sync_done();
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        window_row(i, usage_window_at(&snap.info, i), now_utc, time_ok);
    }

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
    s_wifi_up = false;
    s_rssi = WIFI_MGR_RSSI_UNKNOWN;
    s_soc = -1;
    s_last_soc = INT_MIN;
    s_last_lit = INT_MIN;
    s_last_weak = false;

    s_date = make_label(s_scr, 8, 30, &lv_font_montserrat_14);
    // Percent label auto-sizes to its text; batt_layout() pins the
    // whole group to the right edge whenever the SOC changes.
    s_batt = make_label(s_scr, BATT_GROUP_RIGHT - 48, 30,
                        &lv_font_montserrat_14);

    s_batt_frame = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_frame, BATT_FRAME_W, BATT_FRAME_H);
    lv_obj_set_style_radius(s_batt_frame, 2, 0);
    lv_obj_set_style_bg_opa(s_batt_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_batt_frame, 1, 0);
    lv_obj_set_style_border_color(s_batt_frame,
                                  lv_palette_main(LV_PALETTE_GREY), 0);

    s_batt_fill = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_fill, 0, BATT_FILL_MAX_H);
    lv_obj_set_style_radius(s_batt_fill, 1, 0);
    lv_obj_set_style_border_width(s_batt_fill, 0, 0);
    lv_obj_set_style_bg_color(s_batt_fill,
                              lv_palette_main(LV_PALETTE_GREEN), 0);

    s_batt_tip = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_tip, BATT_TIP_W, BATT_TIP_H);
    lv_obj_set_style_radius(s_batt_tip, 1, 0);
    lv_obj_set_style_border_width(s_batt_tip, 0, 0);
    lv_obj_set_style_bg_color(s_batt_tip,
                              lv_palette_main(LV_PALETTE_GREY), 0);
    // No explicit positions for the icon parts: the first refresh at the
    // end of create() runs batt_layout() before the screen can render,
    // and it owns the group's geometry from then on.

    int y = 56;
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        lv_obj_t *title = make_label(s_scr, 8, y, &lv_font_montserrat_14);
        lv_label_set_text(title, USAGE_WINDOWS[i].title);
        s_pct[i] = make_label(s_scr, 8, y, &lv_font_montserrat_14);
        lv_obj_set_width(s_pct[i], CONTENT_W);
        lv_obj_set_style_text_align(s_pct[i], LV_TEXT_ALIGN_RIGHT, 0);

        s_bar[i] = lv_bar_create(s_scr);
        lv_obj_set_size(s_bar[i], CONTENT_W, BAR_H);
        lv_obj_set_pos(s_bar[i], 8, y + 20);
        lv_bar_set_range(s_bar[i], 0, 100);
        lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);

        s_marker[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_marker[i], USAGE_MARKER_W, BAR_H);
        lv_obj_set_pos(s_marker[i], 8, y + 20);
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
    lv_obj_set_width(s_status, CONTENT_W);
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
        for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
            s_pct[i] = s_bar[i] = s_reset[i] = s_marker[i] = NULL;
        }
        for (int i = 0; i < WIFI_BARS_COUNT; i++) {
            s_wifi_bars[i] = NULL;
        }
    }
}
