// main/wifi_signal.c — see wifi_signal.h.
#include "wifi_signal.h"

int wifi_signal_level(int rssi_dbm)
{
    if (rssi_dbm == 0) {
        return -1;
    }
    if (rssi_dbm >= -55) {
        return 4;
    }
    if (rssi_dbm >= -65) {
        return 3;
    }
    if (rssi_dbm >= -75) {
        return 2;
    }
    if (rssi_dbm >= -85) {
        return 1;
    }
    return 0;
}

int wifi_signal_anim_level(unsigned tick)
{
    return (int)(tick % 4) + 1;
}
