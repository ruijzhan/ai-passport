// main/usage_client.c — see usage_client.h.
#include "usage_client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "usage_client";

#define USAGE_URL "https://opencode.ai/zen/go/v1/usage"
#define RESPONSE_CAP 2048

esp_err_t usage_fetch(usage_info_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    const char *key = CONFIG_APP_OPENCODE_API_KEY;
    if (key[0] == '\0') {
        ESP_LOGW(TAG, "API key not configured");
        return ESP_ERR_INVALID_ARG;
    }

    char *body = malloc(RESPONSE_CAP);
    if (!body) return ESP_ERR_NO_MEM;
    size_t filled = 0;

    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", key);

    esp_http_client_config_t cfg = {
        .url = USAGE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 12000,
        .buffer_size = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body);
        memset(auth, 0, sizeof(auth));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "User-Agent", "cc-switch/1.0");
    memset(auth, 0, sizeof(auth));

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(client) < 0) err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP status %d", status);
            err = ESP_FAIL;
        }
    }
    while (err == ESP_OK && filled + 1 < RESPONSE_CAP) {
        int n = esp_http_client_read(client, body + filled,
                                     RESPONSE_CAP - 1 - filled);
        if (n < 0) {
            err = ESP_FAIL;
            break;
        }
        if (n == 0) break;
        filled += (size_t)n;
    }
    if (err == ESP_OK) {
        body[filled] = '\0';
        usage_info_t info;
        usage_parse(body, &info);
        if (!info.rolling.valid && !info.weekly.valid && !info.monthly.valid) {
            ESP_LOGW(TAG, "no usable window in response");
            err = ESP_FAIL;
        } else {
            *out = info;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    memset(body, 0, filled);
    free(body);
    return err;
}
