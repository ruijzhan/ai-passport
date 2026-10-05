// main/power_idle.h — idle tracking and terminal deep sleep.
//
// Any button event counts as activity. After CONFIG_APP_IDLE_SLEEP_S
// seconds without input the app stops refreshing and enters deep sleep.
// Wakeup is by any button (all three pull GPIO0 low) or power cycle.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Remember boot as activity. Call once from app_main.
void power_idle_init(void);
// Call on every button event (any type).
void power_idle_mark_activity(void);
// Seconds since the last input.
int64_t power_idle_idle_s(void);
// True when the idle timeout has elapsed.
bool power_idle_expired(void);
// Stop UI/network/peripherals and enter deep sleep. Never returns.
void power_idle_enter_deep_sleep(void);
