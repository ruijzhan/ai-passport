// main/time_sync.h — SNTP clock sync once Wi-Fi has an IP address.
#pragma once

#include <stdbool.h>

// Apply CONFIG_APP_TIMEZONE and arm SNTP against CONFIG_APP_NTP_SERVER.
// It starts by itself on the first IP_EVENT_STA_GOT_IP (poll mode,
// SNTP_SYNC_MODE_IMMED, 15 min re-poll), so callers need no ordering
// knowledge beyond "after wifi_mgr_start() created the event loop".
// Idempotent.
void time_sync_start(void);
// True once the clock holds a plausible UTC value.
bool time_sync_done(void);
