// main/usage_client.h — blocking HTTPS fetch of the Go usage document.
#pragma once

#include "esp_err.h"
#include "usage_parse.h"

// GET https://opencode.ai/zen/go/v1/usage with the sdkconfig API key.
// Runs in a worker task (blocks up to ~15 s). The key is never logged.
// Returns ESP_OK and fills *out on HTTP 200 + valid payload;
// ESP_ERR_INVALID_ARG when the key is unset (no network attempt is made
// — callers can distinguish config errors from fetch failures); other
// errors come from the HTTP/TLS fetch. The caller decides whether to
// keep stale data on failure.
esp_err_t usage_fetch(usage_info_t *out);
