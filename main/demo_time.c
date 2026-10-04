// main/demo_time.c —— Time 演示页:展示 NTP 同步的日期时间与 NTP 客户端状态。
//
// 网络归属:本页不自建 Wi-Fi,只复用开机自连(boot-owned)链路,退出时不断网,
// 因此与 Wi-Fi 页无归属冲突。未配置 SSID 或关闭自连时显示离线提示,NTP 等待。
// SNTP 在首次进入时初始化并在页面间保持运行,退出只删 UI,不停服务,系统时钟
// 在后台保持对时;OK 短按手动触发一次重同步。
#include "demo.h"
#include "demo_time_util.h"
#include "bsp_display.h"    // bsp_lvgl_lock / bsp_lvgl_unlock
#include "ui_pixel.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#ifndef CONFIG_APP_NTP_SERVER
#define CONFIG_APP_NTP_SERVER "pool.ntp.org"
#endif
#ifndef CONFIG_APP_TIMEZONE
#define CONFIG_APP_TIMEZONE "CST-8"
#endif

static const char *TAG = "demo_time";

static lv_obj_t *s_scr;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_status;
static lv_obj_t *s_ntp;
static lv_obj_t *s_mascot;
static lv_timer_t *s_timer;

static bool s_sntp_inited;
static volatile unsigned s_sync_count;
static volatile time_t s_last_sync;
static char s_server[64];

static void time_sync_cb(struct timeval *tv)
{
    if (!tv) {
        return;
    }
    s_last_sync = tv->tv_sec;
    s_sync_count++;
    ESP_LOGI(TAG, "NTP synced, count=%u time=%lld",
             s_sync_count, (long long)tv->tv_sec);
}

// STA netif 不存在或无 IP 时返回 false;存在合法 IPv4 时拷出点分地址。
static bool wifi_has_ip(char *ip_out, size_t ip_len)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) {
        return false;
    }
    esp_netif_ip_info_t info = { 0 };
    if (esp_netif_get_ip_info(sta, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    if (ip_out && ip_len > 0) {
        esp_ip4addr_ntoa(&info.ip, ip_out, (int)ip_len);
    }
    return true;
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_scr) {
        return;
    }

    time_t now = 0;
    time(&now);
    if (demo_time_is_synced(now)) {
        struct tm tm_now = { 0 };
        localtime_r(&now, &tm_now);
        char date[32] = { 0 };
        char clock[16] = { 0 };
        demo_time_format(tm_now, date, sizeof(date), clock, sizeof(clock));
        if (s_time) {
            lv_label_set_text(s_time, clock[0] ? clock : "--:--:--");
        }
        if (s_date) {
            lv_label_set_text(s_date, date[0] ? date : "NOT SYNCED");
        }
    } else {
        if (s_time) {
            lv_label_set_text(s_time, "--:--:--");
        }
        if (s_date) {
            lv_label_set_text(s_date, "NOT SYNCED");
        }
    }

    char ip[16] = { 0 };
    bool online = wifi_has_ip(ip, sizeof(ip));
    if (s_status) {
        if (online) {
            lv_label_set_text_fmt(s_status, "WiFi ONLINE %s", ip);
        } else {
            lv_label_set_text(s_status, "WiFi OFFLINE\nSet APP_WIFI_SSID");
        }
    }

    if (s_ntp) {
        unsigned count = s_sync_count;
        const char *state = demo_time_ntp_state(s_sntp_inited, online, count);
        if (!s_sntp_inited) {
            lv_label_set_text_fmt(s_ntp, "NTP %s\n%s | OK:RESYNC",
                                  state,
                                  s_server[0] ? s_server : CONFIG_APP_NTP_SERVER);
        } else if (count == 0) {
            lv_label_set_text_fmt(s_ntp, "NTP %s\n%.24s | OK:RESYNC",
                                  state, s_server);
        } else {
            time_t last = s_last_sync;
            struct tm tm_last = { 0 };
            localtime_r(&last, &tm_last);
            char last_clock[16] = { 0 };
            demo_time_format(tm_last, NULL, 0, last_clock, sizeof(last_clock));
            lv_label_set_text_fmt(s_ntp, "NTP %s x%u %s\n%.24s | OK:RESYNC",
                                  state, count,
                                  last_clock[0] ? last_clock : "--:--:--",
                                  s_server);
        }
    }
}

void demo_time_enter(void)
{
    s_scr = ui_pixel_screen_create("TIME");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 18, 40, 284, 160, UI_PAPER);

    s_time = lv_label_create(panel);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_time, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 2);
    lv_label_set_text(s_time, "--:--:--");

    s_date = lv_label_create(panel);
    lv_obj_set_width(s_date, 258);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_date, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 30);
    lv_label_set_text(s_date, "NOT SYNCED");

    s_status = lv_label_create(panel);
    lv_obj_set_width(s_status, 258);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 2, 56);
    lv_label_set_text(s_status, "WiFi ...");

    s_ntp = lv_label_create(panel);
    lv_obj_set_width(s_ntp, 258);
    lv_obj_set_style_text_font(s_ntp, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ntp, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_align(s_ntp, LV_ALIGN_TOP_LEFT, 2, 100);
    lv_label_set_text(s_ntp, "NTP ...");

    s_mascot = ui_pixel_mascot_create(s_scr, 272, 158);
    s_timer = lv_timer_create(tick, 500, NULL);
    lv_screen_load(s_scr);
    tick(NULL);
}

esp_err_t demo_time_start(void)
{
    setenv("TZ", CONFIG_APP_TIMEZONE, 1);
    tzset();
    snprintf(s_server, sizeof(s_server), "%s", CONFIG_APP_NTP_SERVER);
    if (s_server[0] == '\0') {
        ESP_LOGE(TAG, "NTP server is empty, set APP_NTP_SERVER");
        return ESP_ERR_INVALID_ARG;
    }

    // 只复用开机自连,不自建 page-owned Wi-Fi,退出时不断网。
    esp_err_t wifi_err = demo_wifi_boot_autoconnect();
    if (wifi_err != ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi not connected (%s), NTP waits for network",
                 esp_err_to_name(wifi_err));
    }

    if (!s_sntp_inited) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_APP_NTP_SERVER);
        cfg.sync_cb = time_sync_cb;
        esp_err_t err = esp_netif_sntp_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(err));
            return err;
        }
        s_sntp_inited = true;
        ESP_LOGI(TAG, "SNTP started, server=%s tz=%s", s_server, CONFIG_APP_TIMEZONE);
    } else {
        (void)esp_netif_sntp_start();
    }
    return ESP_OK;
}

esp_err_t demo_time_stop(void)
{
    // SNTP 与 boot Wi-Fi 在页面间保持运行,退出只删 UI,不断网不停服。
    return ESP_OK;
}

void demo_time_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_time = s_date = s_status = s_ntp = s_mascot = NULL;
    }
}

void demo_time_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) {
        return;
    }
    if (s_sntp_inited) {
        (void)esp_netif_sntp_start();
        ESP_LOGI(TAG, "manual NTP resync requested");
    }
    if (!bsp_lvgl_lock(250)) {
        return;
    }
    ui_pixel_mascot_jump(s_mascot);
    tick(NULL);
    bsp_lvgl_unlock();
}
