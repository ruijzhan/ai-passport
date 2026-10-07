// main/power_charge.h — host-testable charge-inhibit policy for idle sleep.
//
// The board has no charger-status pin and the CW2017 exposes no charging
// bit, so "charging" is inferred from three weak signals; any one of them
// inhibits automatic idle deep sleep (a manual OK long-press still sleeps):
//   - USB Serial/JTAG sees SOF packets from a host PC. Wall chargers and
//     power banks send no SOF, so they are NOT covered by this signal.
//   - Fuel-gauge SOC at or above 95% (near-full cell).
//   - Battery voltage trending up over a minutes-scale window (CC charge).
// Note the requested semantic: a full battery left unplugged stays awake
// until SOC drops below the threshold. That costs power; it is the chosen
// tradeoff, not a defect to "fix" silently.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// SOC at or above this inhibits idle sleep (near-full cell).
#define POWER_CHARGE_SOC_INHIBIT_PCT 95
// Newest-vs-oldest voltage delta that counts as "rising" (mV).
#define POWER_CHARGE_RISE_MIN_MV 8
// Samples older than this relative to the newest push fall off (s). The
// worker samples every few minutes (3 min refresh cadence), so the window
// must span several samples; at a 30 s cadence the ring fills first.
#define POWER_CHARGE_RISE_WINDOW_S 600
// Minimum samples in the window before a slope is trusted.
#define POWER_CHARGE_RISE_MIN_SAMPLES 3
// Ring capacity; the window above trims first, this only caps memory.
#define POWER_CHARGE_HISTORY 8

typedef struct {
    int64_t t_s;  // monotonic seconds (esp_timer_get_time() / 1000000)
    int mv;       // battery voltage, mV
} power_charge_sample_t;

typedef struct {
    power_charge_sample_t buf[POWER_CHARGE_HISTORY];
    int count;  // valid entries, oldest first
} power_charge_t;

void power_charge_reset(power_charge_t *c);
// Record a voltage sample. Ignores mv <= 0 (read failure) and
// non-monotonic timestamps; evicts samples outside the window.
void power_charge_push(power_charge_t *c, int64_t now_s, int mv);
// True when the window holds enough samples and the net rise covers
// POWER_CHARGE_RISE_MIN_MV. A single noisy read can only hold sleep off
// briefly; the next samples correct it.
bool power_charge_rising(const power_charge_t *c);
// Inhibit automatic idle sleep: USB host connected, SOC near full, or
// voltage rising. soc_pct < 0 (gauge not ready) disables the SOC signal.
bool power_charge_inhibit(bool usb_connected, int soc_pct, const power_charge_t *c);
