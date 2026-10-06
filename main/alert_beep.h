// main/alert_beep.h — quota threshold beeps via the ES8311 codec.
//
// Worker-task context only: playback blocks for a few hundred ms.
// The 1 s LVGL timer and button callbacks must never call these; they
// only compute the alert level. All failures stay silent so a missing
// codec never breaks quota display. Deep sleep teardown is idempotent
// when audio was never initialized.
#pragma once

#include "alert.h"

// Play the edge-triggered pattern for a risen threshold. NORMAL is a
// no-op. Lazy-initializes the codec on first use.
void alert_beep_play(alert_level_t level);

// Suspend the codec and release I2C/I2S pins for deep sleep. Call on
// the terminal sleep path before bsp_i2c_prepare_deep_sleep().
void alert_beep_prepare_deep_sleep(void);
