// main/time_sync.h — one-shot SNTP sync after Wi-Fi is up.
#pragma once

#include <stdbool.h>

// Start SNTP against CONFIG_APP_NTP_SERVER and apply CONFIG_APP_TIMEZONE.
// Idempotent; safe to call before Wi-Fi is up (sync completes later).
void time_sync_start(void);
// True once the clock holds a plausible UTC value.
bool time_sync_done(void);
