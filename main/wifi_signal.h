// main/wifi_signal.h — RSSI to bar mapping without ESP-IDF/LVGL.
//
// Keeps the thresholds and the connecting animation testable on the host;
// main/ui_home.c renders the resulting level with LVGL objects.
#pragma once

// Map RSSI in dBm to 0-4 bars. Thresholds follow common phone behavior:
// >= -55: 4, >= -65: 3, >= -75: 2, >= -85: 1, below: 0. Unknown RSSI
// (below any real reading, e.g. wifi_mgr_rssi() when not associated)
// lands in the 0-bar bucket naturally, so no sentinel case is needed.
int wifi_signal_level(int rssi_dbm);

// Connecting animation: cycles the lit-bar count 1..4 as tick advances.
// ui_home.c passes its 1 s refresh counter so no LVGL timer is needed here.
int wifi_signal_anim_level(unsigned tick);
