// main/ui_home.c — see ui_home.h. Mark VI Pip-Boy terminal theme on the
// 240x320 portrait display: phosphor-green ink on a near-black tube,
// dim/dark hierarchy, and a walking mascot animation in the lower-right
// viewport.
//
//   14:22:05            [bars]
//   2026-10-06 TUE   [icon] 87%
//   ------------------------------     green rule
//   5H ROLLING                  4%
//   [========== bar ==========]
//   RESET IN 02:15:33
//   WEEKLY / MONTHLY (same layout)
//   > SYNC 12S AGO             +----+  mascot
//                              |    |  8 frames
//   [ OK REFRESH   HOLD SLEEP ]+----+
#include "ui_home.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "bsp_battery.h"
#include "pipboy_frames.h"
#include "time_sync.h"
#include "usage_store.h"
#include "wifi_mgr.h"
#include "wifi_signal.h"

// Pip-Boy palette shared with the reference visual language: a near-black
// tube, bright phosphor green, a dim green for secondary text, a very dark
// green for tracks/inactive parts, and blue/amber for state accents.
#define PIP_BG    0x000806
#define PIP_GREEN 0x00E87B
#define PIP_DIM   0x087A4B
#define PIP_DARK  0x063B28
#define PIP_BLUE  0x35A7FF
#define PIP_AMBER 0xFFB347
#define PIP_RED   0xFF4D4D

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
#define CONTENT_X 8
#define CONTENT_W 224
#define BAR_H 12

// Green rule separating the status header from the quota rows.
#define RULE_Y 48
#define RULE_H 2

// Status text is capped at x=156 so the animation viewport on the right
// never overlaps it.
#define STATUS_X 8
#define STATUS_Y 232
#define STATUS_W 148

// Lower-right mascot viewport: green viewfinder brackets tightly around the
// 52x75 walk-cycle sprite (4px side padding, ~1-2px top/bottom). The bottom
// edge stops 4px above the footer and the right edge aligns with the rest
// of the content (x=232). Top edge at y=208 clears the monthly bar
// (ends y=204) by 4px.
#define ANIM_X 172
#define ANIM_Y 208
#define ANIM_W 60
#define ANIM_H 78
#define ANIM_BRACKET_ARM 10
#define ANIM_BRACKET_T 2
#define ANIM_FRAME_MS 100

// Dim footer bar with dark text, like the reference terminal footer.
#define FOOTER_X 8
#define FOOTER_Y 290
#define FOOTER_W 224
#define FOOTER_H 17

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

static const lv_image_dsc_t *const PIPBOY_FRAMES[PIPBOY_FRAME_COUNT] = {
    &pipboy_frame_0,
    &pipboy_frame_1,
    &pipboy_frame_2,
    &pipboy_frame_3,
    &pipboy_frame_4,
    &pipboy_frame_5,
    &pipboy_frame_6,
    &pipboy_frame_7,
};

static lv_obj_t *s_scr;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi_bars[WIFI_BARS_COUNT];
static unsigned s_tick;
// Sampled sensor caches (see WIFI_RSSI_SAMPLE_TICKS/BATT_SAMPLE_TICKS).
// s_rssi/s_wifi_up resample on a Wi-Fi state transition so bars appear
// the moment the link comes up; s_soc retries every tick while unknown,
// suspect-zero, or unconfirmed (gauge init/recalc after wake), and starts
// from the RTC cache saved before deep sleep.
static bool s_wifi_up;
static int s_rssi = WIFI_MGR_RSSI_UNKNOWN;
static int s_soc = -1;
// 单次 0% 读数多为唤醒后电量计重算的 transient 值：连续两次才采纳，
// 期间保持显示 RTC 缓存。s_zero_streak>0 时下一 tick 立刻重采。
static int s_zero_streak;
static lv_obj_t *s_date;
static lv_obj_t *s_batt;
static lv_obj_t *s_batt_frame;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_tip;
static lv_obj_t *s_pct[USAGE_WINDOW_COUNT];
static lv_obj_t *s_bar[USAGE_WINDOW_COUNT];
static lv_obj_t *s_marker[USAGE_WINDOW_COUNT];
// -1 unknown, 0 line sits on the dark track, 1 line covered by the fill.
// Cached so the per-second refresh only touches the style on a real flip.
static int s_marker_cover[USAGE_WINDOW_COUNT];
static lv_obj_t *s_reset[USAGE_WINDOW_COUNT];
static lv_obj_t *s_status;
static lv_obj_t *s_sprite;
static lv_timer_t *s_timer;
static lv_timer_t *s_anim_timer;
static unsigned s_anim_frame;
// Last rendered values; INT_MIN forces the first refresh after create() to
// draw. Re-setting an unchanged label/style reallocates LVGL text or
// invalidates the area, so identical values are skipped.
static int s_last_soc = INT_MIN;
static int s_last_lit = INT_MIN;
static bool s_last_weak;
// -1 forces the first status write, including its color.
static int s_status_warn = -1;

// Pace marker: 2px line spanning the bar height, centered on the
// time-progress position so usage-vs-average is directly comparable. It
// flips color to stay readable in the Pip-Boy palette: bright phosphor
// green while it sits on the dark track, black once the usage fill covers
// it (black-on-dark would vanish, green-on-green would too).
#define USAGE_MARKER_W 2

static void set_text(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void set_text_color(lv_obj_t *label, uint32_t color)
{
    if (!label) return;
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

static void set_bg_color(lv_obj_t *obj, uint32_t color)
{
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

// Create a plain rectangle with no border, padding, or scrolling.
static lv_obj_t *make_rect(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void marker_update(int i, const usage_window_t *w, int64_t now,
                          bool time_ok)
{
    if (i < 0 || i >= USAGE_WINDOW_COUNT || !s_marker[i] || !s_bar[i]) {
        return;
    }
    if (!w->valid || !w->has_reset || !time_ok) {
        s_marker_cover[i] = -1;
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int pct = usage_time_progress(now, w->resets_at_utc,
                                  usage_window_period_s(i, now));
    if (pct < 0) {
        s_marker_cover[i] = -1;
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);
    // Keep the line above the bar fill/track regardless of sibling order.
    lv_obj_move_foreground(s_marker[i]);
    // Flip the line color so it stays readable: green on the dark track,
    // black where the bright fill has already passed it.
    int covered = (w->percent >= pct) ? 1 : 0;
    if (s_marker_cover[i] != covered) {
        s_marker_cover[i] = covered;
        set_bg_color(s_marker[i], covered ? 0x000000 : PIP_GREEN);
    }
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
    bool live_countdown = false;
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
            snprintf(buf, sizeof(buf), "RESET IN %s", cd);
            live_countdown = true;
        } else {
            snprintf(buf, sizeof(buf), "RESET --");
        }
    } else {
        set_text(s_pct[i], "--");
        lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "RESET --");
    }
    set_text(s_reset[i], buf);
    // Blue marks a live countdown; dim green keeps the idle "--" quiet.
    set_text_color(s_reset[i], live_countdown ? PIP_BLUE : PIP_DIM);
    marker_update(i, w, now, time_ok);
}

static void wifi_bars_set(int lit, bool weak)
{
    lv_color_t active = weak ? lv_color_hex(PIP_AMBER) : lv_color_hex(PIP_GREEN);
    lv_color_t dim = lv_color_hex(PIP_DARK);
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
    uint32_t fill;
    int fill_w = 0;
    if (soc < 0) {
        fill = PIP_DARK;
    } else {
        if (soc < BATT_CRIT_SOC) {
            fill = PIP_RED;
        } else if (soc < BATT_LOW_SOC) {
            fill = PIP_AMBER;
        } else {
            fill = PIP_GREEN;
        }
        if (soc > 100) soc = 100;
        fill_w = (BATT_FILL_MAX_W * soc) / 100;
    }
    set_bg_color(s_batt_fill, fill);
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

// Walk-cycle step: one A8 frame per tick, recolor-supplied by the widget.
static void anim_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_sprite) return;
    s_anim_frame = (s_anim_frame + 1) % PIPBOY_FRAME_COUNT;
    lv_image_set_src(s_sprite, PIPBOY_FRAMES[s_anim_frame]);
}

static void status_set(const char *text, bool warn)
{
    if (!s_status) return;
    const char *current = lv_label_get_text(s_status);
    if (s_status_warn == (warn ? 1 : 0) && current && strcmp(current, text) == 0) {
        return;
    }
    s_status_warn = warn ? 1 : 0;
    set_text_color(s_status, warn ? PIP_AMBER : PIP_DIM);
    set_text(s_status, text);
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
        // Terminals are uppercase; %a follows the C locale's mixed case.
        for (char *p = buf; *p; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
        set_text(s_date, buf);
    } else {
        set_text(s_clock, "--:--:--");
        set_text(s_date, "SYNCING TIME...");
    }

    char buf[96];
    s_tick++;
    // Top-right shows bars only (no "WiFi" text): lit count = signal
    // level, amber = weak, animated = connecting, all dark = off. State
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

    if (s_soc < 0 || s_soc == 0 || s_zero_streak > 0 ||
        s_tick % BATT_SAMPLE_TICKS == 0) {
        int soc = bsp_battery_soc();
        if (soc < 0) {
            // 读失败：保留上一次有效值（含 RTC 缓存）。
        } else if (soc == 0 && s_soc != 0) {
            // 骤降到 0 多为重算 transient：需连续确认才覆盖缓存/旧值。
            if (++s_zero_streak >= 2) {
                s_soc = 0;
                s_zero_streak = 0;
            }
        } else {
            s_zero_streak = 0;
            s_soc = soc;  // keep the last good value on a flaky read
        }
    }
    if (s_soc != s_last_soc) {
        s_last_soc = s_soc;
        if (s_soc >= 0) snprintf(buf, sizeof(buf), "%d%%", s_soc);
        else snprintf(buf, sizeof(buf), "--");
        set_text(s_batt, buf);
        set_text_color(s_batt, s_soc >= 0 ? PIP_GREEN : PIP_DIM);
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
        snprintf(buf, sizeof(buf), "> %.40s",
                 snap.last_failed ? snap.last_error : "NO DATA YET");
        status_set(buf, snap.last_failed);
    } else if (snap.last_failed) {
        snprintf(buf, sizeof(buf), "> %.32s; CACHED", snap.last_error);
        status_set(buf, true);
    } else if (time_ok && snap.fetched_at > 0) {
        long age = (long)(now - snap.fetched_at);
        if (age < 0) age = 0;
        if (age < 90) snprintf(buf, sizeof(buf), "> SYNC %ldS AGO", age);
        else snprintf(buf, sizeof(buf), "> SYNC %ldM AGO", age / 60);
        status_set(buf, false);
    } else {
        // Cached snapshot restored after wake, fresh fetch pending.
        status_set("> CACHED DATA", false);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y,
                            const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

// Four L-shaped viewfinder brackets around the mascot viewport.
static void anim_brackets(lv_obj_t *parent)
{
    const int x = ANIM_X;
    const int y = ANIM_Y;
    const int w = ANIM_W;
    const int h = ANIM_H;
    const int a = ANIM_BRACKET_ARM;
    const int t = ANIM_BRACKET_T;
    make_rect(parent, x, y, a, t, PIP_GREEN);
    make_rect(parent, x, y, t, a, PIP_GREEN);
    make_rect(parent, x + w - a, y, a, t, PIP_GREEN);
    make_rect(parent, x + w - t, y, t, a, PIP_GREEN);
    make_rect(parent, x, y + h - t, a, t, PIP_GREEN);
    make_rect(parent, x, y + h - a, t, a, PIP_GREEN);
    make_rect(parent, x + w - a, y + h - t, a, t, PIP_GREEN);
    make_rect(parent, x + w - t, y + h - a, t, a, PIP_GREEN);
}

void ui_home_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_radius(s_scr, 0, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(PIP_BG), 0);

    // Top-left clock is inset past the rounded-corner mask
    // (BSP_LVGL_SCREEN_RADIUS=30 hides x<16 at y=4).
    s_clock = make_label(s_scr, 18, 4, &lv_font_montserrat_20, PIP_GREEN);

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
        lv_obj_set_style_bg_color(bar, lv_color_hex(PIP_DARK), 0);
        s_wifi_bars[i] = bar;
    }
    s_tick = 0;
    s_wifi_up = false;
    s_rssi = WIFI_MGR_RSSI_UNKNOWN;
    // Deep sleep reboot 后 RAM 全丢：用 RTC 缓存做初始值，芯片就绪前显示
    // 上次休眠前的电量，而不是 0% 或 "--"。冷启动无缓存时仍为 -1。
    s_soc = bsp_battery_cached_soc();
    s_zero_streak = 0;
    s_last_soc = INT_MIN;
    s_last_lit = INT_MIN;
    s_last_weak = false;
    s_status_warn = -1;

    s_date = make_label(s_scr, CONTENT_X, 30, &lv_font_montserrat_14, PIP_DIM);
    // Percent label auto-sizes to its text; batt_layout() pins the
    // whole group to the right edge whenever the SOC changes.
    s_batt = make_label(s_scr, BATT_GROUP_RIGHT - 48, 30,
                        &lv_font_montserrat_14, PIP_GREEN);

    s_batt_frame = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_frame, BATT_FRAME_W, BATT_FRAME_H);
    lv_obj_set_style_radius(s_batt_frame, 2, 0);
    lv_obj_set_style_bg_opa(s_batt_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_batt_frame, 1, 0);
    lv_obj_set_style_border_color(s_batt_frame, lv_color_hex(PIP_DIM), 0);

    s_batt_fill = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_fill, 0, BATT_FILL_MAX_H);
    lv_obj_set_style_radius(s_batt_fill, 1, 0);
    lv_obj_set_style_border_width(s_batt_fill, 0, 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(PIP_DARK), 0);

    s_batt_tip = lv_obj_create(s_scr);
    lv_obj_set_size(s_batt_tip, BATT_TIP_W, BATT_TIP_H);
    lv_obj_set_style_radius(s_batt_tip, 1, 0);
    lv_obj_set_style_border_width(s_batt_tip, 0, 0);
    lv_obj_set_style_bg_color(s_batt_tip, lv_color_hex(PIP_DIM), 0);
    // No explicit positions for the icon parts: the first refresh at the
    // end of create() runs batt_layout() before the screen can render,
    // and it owns the group's geometry from then on.

    // Status header ends at the green rule; quota rows follow below.
    make_rect(s_scr, CONTENT_X, RULE_Y, CONTENT_W, RULE_H, PIP_GREEN);

    int y = 56;
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        lv_obj_t *title = make_label(s_scr, CONTENT_X, y,
                                     &lv_font_montserrat_14, PIP_DIM);
        lv_label_set_text(title, USAGE_WINDOWS[i].title);
        s_pct[i] = make_label(s_scr, CONTENT_X, y,
                              &lv_font_montserrat_14, PIP_GREEN);
        lv_obj_set_width(s_pct[i], CONTENT_W);
        lv_obj_set_style_text_align(s_pct[i], LV_TEXT_ALIGN_RIGHT, 0);

        s_bar[i] = lv_bar_create(s_scr);
        lv_obj_set_size(s_bar[i], CONTENT_W, BAR_H);
        lv_obj_set_pos(s_bar[i], CONTENT_X, y + 20);
        lv_obj_set_style_radius(s_bar[i], 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_bar[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_bar[i], lv_color_hex(PIP_GREEN),
                                      LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(PIP_DARK), LV_PART_MAIN);
        lv_obj_set_style_radius(s_bar[i], 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(PIP_GREEN),
                                  LV_PART_INDICATOR);
        lv_bar_set_range(s_bar[i], 0, 100);
        lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);

        s_marker[i] = make_rect(s_scr, CONTENT_X, y + 20,
                                USAGE_MARKER_W, BAR_H, PIP_GREEN);
        s_marker_cover[i] = -1;
        lv_obj_add_flag(s_marker[i], LV_OBJ_FLAG_HIDDEN);

        s_reset[i] = make_label(s_scr, CONTENT_X, y + 36,
                                &lv_font_montserrat_14, PIP_DIM);
        y += 58;
    }

    s_status = lv_label_create(s_scr);
    lv_obj_set_pos(s_status, STATUS_X, STATUS_Y);
    lv_obj_set_width(s_status, STATUS_W);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(PIP_DIM), 0);

    anim_brackets(s_scr);
    s_sprite = lv_image_create(s_scr);
    lv_image_set_src(s_sprite, PIPBOY_FRAMES[0]);
    lv_obj_set_pos(s_sprite,
                   ANIM_X + (ANIM_W - PIPBOY_FRAME_WIDTH) / 2,
                   ANIM_Y + (ANIM_H - PIPBOY_FRAME_HEIGHT) / 2);
    lv_obj_set_style_image_recolor(s_sprite, lv_color_hex(PIP_GREEN), 0);
    lv_obj_set_style_image_recolor_opa(s_sprite, LV_OPA_COVER, 0);
    s_anim_frame = 0;

    make_rect(s_scr, FOOTER_X, FOOTER_Y, FOOTER_W, FOOTER_H, PIP_DIM);
    lv_obj_t *hint = make_label(s_scr, FOOTER_X + 2, FOOTER_Y + 1,
                                &lv_font_montserrat_14, PIP_BG);
    lv_obj_set_width(hint, FOOTER_W - 4);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "OK REFRESH    HOLD SLEEP");

    lv_screen_load(s_scr);
    s_timer = lv_timer_create(tick, 1000, NULL);
    s_anim_timer = lv_timer_create(anim_tick, ANIM_FRAME_MS, NULL);
    ui_home_refresh();
}

void ui_home_destroy(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_anim_timer) {
        lv_timer_delete(s_anim_timer);
        s_anim_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_clock = s_date = s_batt = s_status = NULL;
        s_batt_frame = s_batt_fill = s_batt_tip = NULL;
        s_sprite = NULL;
        for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
            s_pct[i] = s_bar[i] = s_reset[i] = s_marker[i] = NULL;
        }
        for (int i = 0; i < WIFI_BARS_COUNT; i++) {
            s_wifi_bars[i] = NULL;
        }
    }
}
