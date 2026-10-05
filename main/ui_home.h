// main/ui_home.h — the single redesigned home screen. Plain LVGL widgets
// only; intentionally independent of the baseline demo visual shell.
//
// All functions must be called with the LVGL lock held, except from the
// 1 s LVGL timer callback (already in LVGL context).
#pragma once

// Build the screen, start the 1 s refresh timer, and load it.
void ui_home_create(void);
// Stop the timer and delete the screen. Call before deep sleep.
void ui_home_destroy(void);
