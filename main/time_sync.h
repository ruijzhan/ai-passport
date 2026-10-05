// main/time_sync.h — one-shot SNTP sync after Wi-Fi is up.
#pragma once

#include <stdbool.h>

// Start SNTP against CONFIG_APP_NTP_SERVER and apply CONFIG_APP_TIMEZONE.
// Idempotent; returns quietly when Wi-Fi is not up yet (do_refresh retries
// after the connection is established).
void time_sync_start(void);
// True once the clock holds a plausible UTC value.
bool time_sync_done(void);
