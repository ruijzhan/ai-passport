// main/demo_wifi.c —— STA 扫描 + 可选的 Kconfig 凭证自动连接。
// 未配置 SSID 时保持纯扫描行为,不连接网络、不保存凭证。
// SSID/密码来自 Kconfig (本地 menuconfig 配置,不提交);密码永不打日志。
#include "demo.h"
#include "demo_radio.h"
#include "bsp_display.h"
#include "ui_pixel.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#ifndef CONFIG_APP_WIFI_SSID
#define CONFIG_APP_WIFI_SSID ""
#endif
#ifndef CONFIG_APP_WIFI_PASSWORD
#define CONFIG_APP_WIFI_PASSWORD ""
#endif

static const char *TAG = "demo_wifi";

#define WIFI_RESULT_COUNT 5

typedef enum {
    WIFI_DEMO_OFF = 0,
    WIFI_DEMO_STARTING,
    WIFI_DEMO_CONNECTING,
    WIFI_DEMO_SCANNING,
    WIFI_DEMO_READY,
    WIFI_DEMO_FAILED,
} wifi_demo_state_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_net;
static lv_obj_t *s_results;
static lv_timer_t *s_timer;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static volatile wifi_demo_state_t s_state;
static volatile esp_err_t s_error;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static bool s_boot_owned;
static bool s_should_connect;
static bool s_has_ip;
static char s_cfg_ssid[33];
static char s_ip[16];
static char s_gw[16];
static char s_mask[16];
static char s_dns[16];

static bool wifi_autoconnect_enabled(void)
{
#ifdef CONFIG_APP_WIFI_AUTOCONNECT
    return true;
#else
    return false;
#endif
}

static void wifi_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    snprintf(ssid, ssid_len, "%s", CONFIG_APP_WIFI_SSID);
    snprintf(pass, pass_len, "%s", CONFIG_APP_WIFI_PASSWORD);
}

// Copy runtime credentials into the driver config. Rejects over-long input
// instead of silently truncating it (a truncated password only causes
// confusing auth failures). The config must be zero-initialized by the
// caller so both fields stay NUL-terminated.
static esp_err_t wifi_apply_credentials(wifi_config_t *sta_cfg, const char *ssid, const char *pass)
{
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(pass);
    if (ssid_len == 0 || ssid_len >= sizeof(sta_cfg->sta.ssid) ||
        pass_len >= sizeof(sta_cfg->sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(sta_cfg->sta.ssid, ssid, ssid_len);
    memcpy(sta_cfg->sta.password, pass, pass_len);
    return ESP_OK;
}

static esp_err_t start_scan(void)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err == ESP_OK) {
        s_state = WIFI_DEMO_SCANNING;
    } else {
        s_error = err;
        s_state = WIFI_DEMO_FAILED;
    }
    return err;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_SCAN_DONE) {
        if (s_state == WIFI_DEMO_SCANNING) s_state = WIFI_DEMO_READY;
        return;
    }
    if (id == WIFI_EVENT_STA_START) {
        if (s_should_connect && !s_has_ip) {
            (void)esp_wifi_connect();
        }
        return;
    }
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_has_ip = false;
        s_ip[0] = s_gw[0] = s_mask[0] = s_dns[0] = '\0';
        if (s_should_connect) {
            s_state = WIFI_DEMO_CONNECTING;
            (void)esp_wifi_connect();
        }
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
    if (event) {
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip, sizeof(s_ip));
        esp_ip4addr_ntoa(&event->ip_info.netmask, s_mask, sizeof(s_mask));
        esp_ip4addr_ntoa(&event->ip_info.gw, s_gw, sizeof(s_gw));
    } else {
        snprintf(s_ip, sizeof(s_ip), "?");
        snprintf(s_mask, sizeof(s_mask), "?");
        snprintf(s_gw, sizeof(s_gw), "?");
    }
    esp_netif_dns_info_t dns = { 0 };
    if (s_sta_netif &&
        esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4) {
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, s_dns, sizeof(s_dns));
    } else {
        snprintf(s_dns, sizeof(s_dns), "--");
    }
    s_has_ip = true;
    ESP_LOGI(TAG, "Wi-Fi connected, IP=%s", s_ip);
    if (s_state == WIFI_DEMO_CONNECTING) {
        if (s_scr) {
            (void)start_scan();
        } else {
            s_state = WIFI_DEMO_OFF;
        }
    }
}

static void wifi_stack_stop(void);

static esp_err_t wifi_stack_start(bool with_scan)
{
    char ssid[sizeof(s_cfg_ssid)] = { 0 };
    char pass[65] = { 0 };
    wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    s_state = WIFI_DEMO_STARTING;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) goto fail;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) goto fail;

    // The convenience creator asserts/aborts on allocation or handler failure.
    // Use its checked steps so this optional demo can fail without rebooting.
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_sta_netif = esp_netif_new(&netif_cfg);
    if (!s_sta_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    // attach records the netif even on failure; destroy_default_wifi below also
    // clears that registration and any partially attached driver/handlers.
    err = esp_netif_attach_wifi_station(s_sta_netif);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) goto fail;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    s_wifi_handler_registered = true;

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    s_ip_handler_registered = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;

    s_should_connect = (ssid[0] != '\0');
    s_has_ip = false;
    s_ip[0] = s_gw[0] = s_mask[0] = s_dns[0] = '\0';
    snprintf(s_cfg_ssid, sizeof(s_cfg_ssid), "%s", ssid);
    if (s_should_connect) {
        wifi_config_t sta_cfg = { 0 };
        err = wifi_apply_credentials(&sta_cfg, ssid, pass);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi SSID/password length invalid");
            goto fail;
        }
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        if (err != ESP_OK) goto fail;
        ESP_LOGI(TAG, "Wi-Fi STA configured, SSID=%s", s_cfg_ssid);
    }
    memset(pass, 0, sizeof(pass));

    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;

    if (s_should_connect && !with_scan) {
        s_state = WIFI_DEMO_CONNECTING;
        return ESP_OK;
    }
    if (s_should_connect) {
        s_state = WIFI_DEMO_CONNECTING;
    }
    err = start_scan();
    if (err != ESP_OK) goto fail;
    return ESP_OK;

fail:
    memset(pass, 0, sizeof(pass));
    wifi_stack_stop();
    s_error = err;
    s_state = WIFI_DEMO_FAILED;
    ESP_LOGE(TAG, "Wi-Fi 初始化失败: %s", esp_err_to_name(err));
    return err;
}

esp_err_t demo_wifi_start(void)
{
    if (s_sta_netif || s_wifi_initialized) {
        if (s_wifi_started && s_boot_owned) {
            return start_scan();
        }
        return ESP_ERR_INVALID_STATE;
    }
    s_boot_owned = false;
    return wifi_stack_start(true);
}

// Boot background connect. No UI is created here; the Wi-Fi page (when later
// entered) attaches to this connection and only triggers scans.
esp_err_t demo_wifi_boot_autoconnect(void)
{
    if (!wifi_autoconnect_enabled()) return ESP_ERR_INVALID_STATE;
    char ssid[sizeof(s_cfg_ssid)] = { 0 };
    char pass[65] = { 0 };
    wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    memset(pass, 0, sizeof(pass));
    if (ssid[0] == '\0') return ESP_ERR_INVALID_STATE;
    if (s_wifi_started) {
        s_boot_owned = true;
        return ESP_OK;
    }
    if (s_sta_netif || s_wifi_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wifi_stack_start(false);
    if (err == ESP_OK) {
        s_boot_owned = true;
    } else {
        s_boot_owned = false;
    }
    return err;
}

static void show_scan_results(void)
{
    uint16_t total = 0;
    uint16_t count = WIFI_RESULT_COUNT;
    wifi_ap_record_t records[WIFI_RESULT_COUNT] = { 0 };
    char text[320] = { 0 };
    size_t used = 0;

    esp_err_t err = esp_wifi_scan_get_ap_num(&total);
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        s_error = err;
        s_state = WIFI_DEMO_FAILED;
        return;
    }

    for (uint16_t i = 0; i < count && used < sizeof(text); i++) {
        int written = snprintf(text + used, sizeof(text) - used,
                               "%d  %.18s  ch%u\n",
                               records[i].rssi, (const char *)records[i].ssid,
                               records[i].primary);
        if (written < 0 || (size_t)written >= sizeof(text) - used) break;
        used += (size_t)written;
    }
    if (count == 0) snprintf(text, sizeof(text), "No access points found");

    if (s_has_ip) {
        lv_label_set_text_fmt(s_status, "Connected %.20s\n%u APs  |  OK: RESCAN",
                              s_cfg_ssid, total);
        lv_label_set_text_fmt(s_net, "IP %s\nGW %s MASK %s\nDNS %s",
                              s_ip, s_gw, s_mask, s_dns);
    } else if (s_should_connect) {
        lv_label_set_text_fmt(s_status, "Connecting %.20s...\n%u APs  |  OK: RESCAN",
                              s_cfg_ssid, total);
        lv_label_set_text(s_net, "Waiting for DHCP...");
    } else {
        lv_label_set_text_fmt(s_status, "No SSID configured\n%u APs  |  OK: RESCAN", total);
        lv_label_set_text(s_net, "Set APP_WIFI_SSID to connect");
    }
    lv_label_set_text(s_results, text);
    s_state = WIFI_DEMO_OFF;
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    switch (s_state) {
    case WIFI_DEMO_STARTING:
        lv_label_set_text(s_status, "Starting Wi-Fi...");
        break;
    case WIFI_DEMO_CONNECTING:
        lv_label_set_text_fmt(s_status, "Connecting %.20s...", s_cfg_ssid);
        lv_label_set_text(s_net, "Waiting for DHCP...");
        break;
    case WIFI_DEMO_SCANNING:
        lv_label_set_text(s_status, "Scanning 2.4 GHz...");
        break;
    case WIFI_DEMO_READY:
        show_scan_results();
        break;
    case WIFI_DEMO_FAILED:
        lv_label_set_text_fmt(s_status, "Wi-Fi failed: %s", esp_err_to_name(s_error));
        s_state = WIFI_DEMO_OFF;
        break;
    default:
        break;
    }
}

static void wifi_stack_stop(void)
{
    if (s_wifi_started) {
        esp_wifi_scan_stop();
        if (s_should_connect) {
            esp_wifi_disconnect();
        }
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_handler_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_handler);
        s_wifi_handler_registered = false;
    }
    if (s_ip_handler_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_handler);
        s_ip_handler_registered = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_should_connect = false;
    s_has_ip = false;
    s_cfg_ssid[0] = '\0';
    s_ip[0] = s_gw[0] = s_mask[0] = s_dns[0] = '\0';
    s_state = WIFI_DEMO_OFF;
}

esp_err_t demo_wifi_stop(void)
{
    // A boot-owned connection survives page exit so the device stays online.
    if (s_boot_owned) return ESP_OK;
    wifi_stack_stop();
    return ESP_OK;
}

void demo_wifi_boot_stop(void)
{
    s_boot_owned = false;
    wifi_stack_stop();
}

void demo_wifi_enter(void)
{
    s_scr = ui_pixel_screen_create("WI-FI");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 18, 40, 284, 160, UI_PAPER);

    s_status = lv_label_create(panel);
    lv_obj_set_width(s_status, 258);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_label_set_text(s_status, "Starting Wi-Fi...");

    s_net = lv_label_create(panel);
    lv_obj_set_width(s_net, 258);
    lv_obj_set_style_text_font(s_net, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_net, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_net, LV_ALIGN_TOP_LEFT, 2, 40);
    lv_label_set_text(s_net, "IP: --");

    s_results = lv_label_create(panel);
    lv_obj_set_width(s_results, 258);
    lv_obj_set_style_text_font(s_results, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_results, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_results, LV_ALIGN_TOP_LEFT, 2, 92);
    lv_label_set_text(s_results, "RSSI  SSID  CHANNEL");

    ui_pixel_mascot_create(s_scr, 272, 158);
    s_timer = lv_timer_create(tick, 100, NULL);
    lv_screen_load(s_scr);
    s_state = WIFI_DEMO_STARTING;
}

void demo_wifi_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = s_net = s_results = NULL;
    }
}

void demo_wifi_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK || s_state != WIFI_DEMO_OFF) return;
    if (!bsp_lvgl_lock(250)) return;
    lv_label_set_text(s_results, "RSSI  SSID  CHANNEL");
    bsp_lvgl_unlock();
    (void)start_scan();
}
