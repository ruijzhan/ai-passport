// main/wifi_signal.h — RSSI to bar mapping without ESP-IDF/LVGL.
//
// Keeps the thresholds and the connecting animation testable on the host;
// main/ui_home.c renders the resulting level with LVGL objects.
#pragma once

// Map RSSI in dBm to 0-4 bars. Returns -1 when rssi_dbm == 0 (unknown,
// matching wifi_mgr_rssi()). Thresholds follow common phone behavior:
// >= -55: 4, >= -65: 3, >= -75: 2, >= -85: 1, below: 0.
int wifi_signal_level(int rssi_dbm);

// Connecting animation: cycles the lit-bar count 1..4 as tick advances.
// ui_home.c passes its 1 s refresh counter so no LVGL timer is needed here.
int wifi_signal_anim_level(unsigned tick);
