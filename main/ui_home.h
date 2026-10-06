// main/ui_home.h — redesigned home screen: overview, detail, and system
// pages sharing one Pip-Boy chrome. Plain LVGL widgets only;
// intentionally independent of the baseline demo visual shell.
//
// All functions must be called with the LVGL lock held, except from the
// 1 s LVGL timer callback (already in LVGL context).
#pragma once

#include "ui_pages.h"

// Build the screen, start the 1 s refresh timer, and load it.
void ui_home_create(void);
// Stop the timer and delete the screen. Call before deep sleep.
void ui_home_destroy(void);
// Page navigation for UP/DOWN keys. No-op before create().
void ui_home_show_page(ui_page_t page);
void ui_home_page_next(void);
void ui_home_page_prev(void);
