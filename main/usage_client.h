// main/usage_client.h — blocking HTTPS fetch of the Go usage document.
#pragma once

#include "esp_err.h"
#include "usage_parse.h"

// GET https://opencode.ai/zen/go/v1/usage with the sdkconfig API key.
// Runs in a worker task (blocks up to ~15 s). The key is never logged.
// Returns ESP_OK and fills *out on HTTP 200 + valid payload; the caller
// decides whether to keep stale data on failure.
esp_err_t usage_fetch(usage_info_t *out);
