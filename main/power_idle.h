// main/power_idle.h — idle tracking and terminal deep sleep.
//
// Any button event counts as activity. After CONFIG_APP_IDLE_SLEEP_S
// seconds without input the app stops refreshing and enters deep sleep,
// unless a charge signal inhibits it: USB host connected, fuel-gauge SOC
// near full, or battery voltage trending up. Wakeup is by any button
// (all three pull GPIO0 low) or power cycle.
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
// True when a charge signal currently inhibits automatic idle sleep.
// Samples USB/SOC/voltage on a minutes-scale cadence; call at most once
// per worker-loop wake, not per button event. Only gates the idle path —
// a manual long-press sleep always proceeds.
bool power_idle_charge_inhibited(void);
// Stop UI/network/peripherals and enter deep sleep. Never returns.
void power_idle_enter_deep_sleep(void);
