// Host rollback/event test for the Wi-Fi STA demo with Kconfig credentials.
// Uses credentialed Kconfig macros so the connect path is exercised;
// the empty-SSID gate build is a strict subset (skips set_config/connect).
#define CONFIG_APP_WIFI_SSID "TestSSID"
#define CONFIG_APP_WIFI_PASSWORD "TestPass"
#define CONFIG_APP_WIFI_AUTOCONNECT 1
#include "demo_stubs/demo_runtime.c"
#include "../main/demo_wifi.c"

#include <stdio.h>

static unsigned step, fail_at, destroys;
static unsigned connects, disconnects;
static bool netif_live, driver_registered, defaults_live, wifi_live, wifi_running;
static bool wifi_reg, ip_reg;
static esp_netif_t netif;
static void (*captured_wifi_cb)(void *, esp_event_base_t, int32_t, void *);
static void (*captured_ip_cb)(void *, esp_event_base_t, int32_t, void *);
static esp_event_handler_instance_t wifi_token;
static esp_event_handler_instance_t ip_token;

static esp_err_t next_step(void) { return ++step == fail_at ? ESP_ERR_NO_MEM : ESP_OK; }

void esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int buflen)
{
    snprintf(buf, (size_t)buflen, "%u.%u.%u.%u",
             (unsigned)((addr->addr >> 24) & 0xFF),
             (unsigned)((addr->addr >> 16) & 0xFF),
             (unsigned)((addr->addr >> 8) & 0xFF),
             (unsigned)(addr->addr & 0xFF));
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg) {
    (void)cfg;
    assert(!netif_live);
    if (next_step() != ESP_OK) return NULL;
    netif_live = true;
    return &netif;
}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *created) {
    assert(created == &netif && netif_live);
    driver_registered = true; // IDF stores the pointer even when attach fails.
    return next_step();
}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void) {
    assert(driver_registered);
    esp_err_t result = next_step();
    defaults_live = result == ESP_OK; // IDF rolls back partially added handlers.
    return result;
}
void esp_netif_destroy_default_wifi(void *created) {
    assert(created == &netif && netif_live && !wifi_live && !wifi_reg && !ip_reg);
    netif_live = driver_registered = defaults_live = false;
    destroys++;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) {
    (void)cfg;
    assert(defaults_live);
    esp_err_t result = next_step();
    wifi_live = result == ESP_OK;
    return result;
}
esp_err_t esp_wifi_deinit(void) {
    assert(wifi_live && !wifi_running && !wifi_reg && !ip_reg);
    wifi_live = false;
    return ESP_OK;
}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg,
    esp_event_handler_instance_t *instance) {
    (void)arg;
    esp_err_t result = next_step();
    if (result != ESP_OK) return result;
    if (base == WIFI_EVENT && id == ESP_EVENT_ANY_ID) {
        assert(!wifi_reg);
        wifi_reg = true;
        captured_wifi_cb = callback;
        wifi_token = &netif;
        *instance = wifi_token;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        assert(!ip_reg);
        ip_reg = true;
        captured_ip_cb = callback;
        ip_token = &test_tasks;
        *instance = ip_token;
    } else {
        assert(!"unexpected event registration");
    }
    return ESP_OK;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
    esp_event_handler_instance_t instance) {
    if (base == WIFI_EVENT && id == ESP_EVENT_ANY_ID) {
        assert(wifi_reg && instance == wifi_token);
        wifi_reg = false;
        captured_wifi_cb = NULL;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        assert(ip_reg && instance == ip_token);
        ip_reg = false;
        captured_ip_cb = NULL;
    } else {
        assert(!"unexpected event unregistration");
    }
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(int storage) { (void)storage; return next_step(); }
esp_err_t esp_wifi_set_mode(int mode) { (void)mode; return next_step(); }
esp_err_t esp_wifi_set_config(int ifx, const wifi_config_t *cfg) {
    (void)ifx;
    assert(cfg && cfg->sta.ssid[0] != '\0');
    return next_step();
}
esp_err_t esp_wifi_connect(void) { connects++; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { disconnects++; return ESP_OK; }
esp_err_t esp_netif_get_dns_info(esp_netif_t *netif, int type, esp_netif_dns_info_t *dns) {
    assert(type == ESP_NETIF_DNS_MAIN);
    assert(netif != NULL && dns != NULL);
    dns->ip.type = ESP_IPADDR_TYPE_V4;
    dns->ip.u_addr.ip4.addr = 0x08080808u; // 8.8.8.8
    return ESP_OK;
}
esp_err_t esp_wifi_start(void) {
    esp_err_t result = next_step();
    wifi_running = result == ESP_OK;
    return result;
}
esp_err_t esp_wifi_stop(void) { assert(wifi_running); wifi_running = false; return ESP_OK; }
esp_err_t esp_wifi_scan_start(const void *cfg, bool blocking) {
    (void)cfg; (void)blocking; assert(wifi_running); return next_step();
}
esp_err_t esp_wifi_scan_stop(void) { assert(wifi_running); return ESP_OK; }

static void assert_clean(void) {
    assert(!s_sta_netif && !s_wifi_initialized && !s_wifi_started);
    assert(!s_wifi_handler_registered && !s_ip_handler_registered);
    assert(!s_boot_owned && !s_should_connect && !s_has_ip);
    assert(s_cfg_ssid[0] == '\0' && s_ip[0] == '\0');
    assert(s_gw[0] == '\0' && s_mask[0] == '\0' && s_dns[0] == '\0');
    assert(!netif_live && !driver_registered && !defaults_live);
    assert(!wifi_live && !wifi_running && !wifi_reg && !ip_reg);
    assert(captured_wifi_cb == NULL && captured_ip_cb == NULL);
}

int main(void) {
    // Failure injection over the 11 credentialed bring-up steps.
    for (unsigned failure = 1; failure <= 11; failure++) {
        step = 0;
        fail_at = failure;
        connects = disconnects = 0;
        unsigned before = destroys;
        assert(demo_wifi_start() != ESP_OK);
        assert(step == failure && s_state == WIFI_DEMO_FAILED);
        assert(destroys == before + (failure > 1));
        assert_clean();
        assert(demo_wifi_stop() == ESP_OK);
        assert(s_state == WIFI_DEMO_OFF);
        assert_clean();
        // Once resources are available, entry after the failure must work.
        step = fail_at = 0;
        assert(demo_wifi_start() == ESP_OK);
        assert(step == 11 && s_state == WIFI_DEMO_SCANNING);
        assert(s_should_connect && !s_has_ip);
        assert(demo_wifi_start() == ESP_ERR_INVALID_STATE);
        assert(demo_wifi_stop() == ESP_OK);
        assert(s_state == WIFI_DEMO_OFF);
        assert_clean();
    }
    puts("Wi-Fi credentialed allocation/attach/handler/start failure rollback tests: PASS");

    // Event flow: STA_START triggers connect, SCAN_DONE readies results,
    // GOT_IP records the address, disconnect retries.
    step = fail_at = 0;
    connects = disconnects = 0;
    assert(demo_wifi_start() == ESP_OK);
    assert(s_state == WIFI_DEMO_SCANNING);
    captured_wifi_cb(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
    assert(connects == 1);
    captured_wifi_cb(NULL, WIFI_EVENT, WIFI_EVENT_SCAN_DONE, NULL);
    assert(s_state == WIFI_DEMO_READY);
    ip_event_got_ip_t got_ip = { 0 };
    got_ip.ip_info.ip.addr = 0xC0A80102u; // 192.168.1.2
    got_ip.ip_info.netmask.addr = 0xFFFFFF00u; // 255.255.255.0
    got_ip.ip_info.gw.addr = 0xC0A80101u; // 192.168.1.1
    captured_ip_cb(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip);
    assert(s_has_ip && strcmp(s_ip, "192.168.1.2") == 0);
    assert(strcmp(s_mask, "255.255.255.0") == 0 && strcmp(s_gw, "192.168.1.1") == 0);
    assert(strcmp(s_dns, "8.8.8.8") == 0);
    captured_wifi_cb(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(!s_has_ip && s_state == WIFI_DEMO_CONNECTING && connects == 2);
    assert(s_gw[0] == '\0' && s_mask[0] == '\0' && s_dns[0] == '\0');
    captured_ip_cb(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip);
    assert(s_has_ip && s_state == WIFI_DEMO_OFF); // no UI: back to idle
    assert(demo_wifi_stop() == ESP_OK);
    assert(disconnects == 1);
    assert(s_state == WIFI_DEMO_OFF);
    assert_clean();
    puts("Wi-Fi connect/disconnect/IP event tests: PASS");

    // Boot ownership: background connect without scan, page attaches with
    // scan, page exit keeps the link, boot stop tears it down.
    step = fail_at = 0;
    connects = disconnects = 0;
    assert(demo_wifi_boot_autoconnect() == ESP_OK);    assert(s_boot_owned && s_wifi_started && s_state == WIFI_DEMO_CONNECTING);
    assert(demo_wifi_start() == ESP_OK); // page attaches: rescan
    assert(s_state == WIFI_DEMO_SCANNING);
    assert(demo_wifi_stop() == ESP_OK); // page exit keeps boot link
    assert(s_wifi_started && s_boot_owned);
    demo_wifi_boot_stop();
    assert(disconnects == 1);
    assert(s_state == WIFI_DEMO_OFF);
    assert_clean();
    puts("Wi-Fi boot autoconnect ownership tests: PASS");

    // Credential length validation: empty SSID and over-long inputs are
    // rejected, an empty password (open network) is accepted.
    {
        wifi_config_t cfg = { 0 };
        char long_ssid[40];
        char long_pass[70];
        memset(long_ssid, 'A', sizeof(long_ssid) - 1);
        long_ssid[sizeof(long_ssid) - 1] = '\0';
        memset(long_pass, 'B', sizeof(long_pass) - 1);
        long_pass[sizeof(long_pass) - 1] = '\0';
        assert(wifi_apply_credentials(&cfg, "", "x") == ESP_ERR_INVALID_ARG);
        assert(wifi_apply_credentials(&cfg, long_ssid, "x") == ESP_ERR_INVALID_ARG);
        assert(wifi_apply_credentials(&cfg, "ssid", long_pass) == ESP_ERR_INVALID_ARG);
        assert(wifi_apply_credentials(&cfg, "ssid", "") == ESP_OK);
        assert(strcmp((const char *)cfg.sta.ssid, "ssid") == 0);
        assert(wifi_apply_credentials(&cfg, "ssid", "pass") == ESP_OK);
        assert(strcmp((const char *)cfg.sta.password, "pass") == 0);
    }
    puts("Wi-Fi credential length validation tests: PASS");
    return 0;
}
