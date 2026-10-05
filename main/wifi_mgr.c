// main/wifi_mgr.c — see wifi_mgr.h. Reconnects on disconnect; the UI
// polls state/RSSI, so no callbacks into LVGL are needed here.
#include "wifi_mgr.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_mgr";

static esp_netif_t *s_sta;
static esp_event_handler_instance_t s_ip_handler;
static esp_event_handler_instance_t s_wifi_handler;
static volatile wifi_mgr_state_t s_state = WIFI_MGR_DOWN;
static bool s_netif_ready;
static bool s_loop_ready;
static bool s_wifi_init;
static bool s_wifi_on;
static bool s_handlers;
static unsigned s_retry;

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    s_retry = 0;
    s_state = WIFI_MGR_UP;
    ESP_LOGI(TAG, "got IP");
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == WIFI_MGR_UP) ESP_LOGW(TAG, "disconnected, reconnecting");
        s_state = WIFI_MGR_CONNECTING;
        s_retry++;
        // Best effort: the stack retries on its own if this fails.
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "reconnect failed: %s", esp_err_to_name(err));
        }
    } else if (id == WIFI_EVENT_STA_START) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "initial connect failed: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t prepare_platform(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s (not erasing user data)",
                 esp_err_to_name(err));
        return err;
    }
    if (!s_netif_ready) {
        err = esp_netif_init();
        if (err != ESP_OK) return err;
        s_netif_ready = true;
    }
    if (!s_loop_ready) {
        err = esp_event_loop_create_default();
        if (err != ESP_OK) return err;
        s_loop_ready = true;
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_start(void)
{
    const char *ssid = CONFIG_APP_WIFI_SSID;
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGW(TAG, "SSID not configured; staying offline");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_wifi_on) return ESP_OK;

    esp_err_t err = prepare_platform();
    if (err != ESP_OK) return err;

    if (!s_sta) {
        s_sta = esp_netif_create_default_wifi_sta();
        if (!s_sta) return ESP_ERR_NO_MEM;
    }
    if (!s_wifi_init) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) return err;
        s_wifi_init = true;
    }
    if (!s_handlers) {
        err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL, &s_ip_handler);
        if (err != ESP_OK) return err;
        err = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, &s_wifi_handler);
        if (err != ESP_OK) {
            esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  s_ip_handler);
            return err;
        }
        s_handlers = true;
    }

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    const char *pass = CONFIG_APP_WIFI_PASSWORD;
    if (pass) strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_on = true;
    s_state = WIFI_MGR_CONNECTING;
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_stop(void)
{
    if (s_wifi_on) {
        esp_wifi_stop();
        s_wifi_on = false;
    }
    if (s_handlers) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_handler);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_handler);
        s_handlers = false;
    }
    if (s_wifi_init) {
        esp_wifi_deinit();
        s_wifi_init = false;
    }
    if (s_sta) {
        esp_netif_destroy_default_wifi(s_sta);
        s_sta = NULL;
    }
    s_state = WIFI_MGR_DOWN;
    return ESP_OK;
}

wifi_mgr_state_t wifi_mgr_state(void)
{
    return s_state;
}

int wifi_mgr_rssi(void)
{
    if (s_state != WIFI_MGR_UP) return 0;
    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}
