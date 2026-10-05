// tests/test_wifi_signal.c — host tests for main/wifi_signal.c.
#include <assert.h>
#include <stdio.h>

#include "wifi_signal.h"

int main(void)
{
    // Unknown sentinel (matches wifi_mgr_rssi() when not associated).
    assert(wifi_signal_level(0) == -1);

    // Threshold boundaries.
    assert(wifi_signal_level(-30) == 4);
    assert(wifi_signal_level(-55) == 4);
    assert(wifi_signal_level(-56) == 3);
    assert(wifi_signal_level(-65) == 3);
    assert(wifi_signal_level(-66) == 2);
    assert(wifi_signal_level(-75) == 2);
    assert(wifi_signal_level(-76) == 1);
    assert(wifi_signal_level(-85) == 1);
    assert(wifi_signal_level(-86) == 0);
    assert(wifi_signal_level(-100) == 0);

    // Connecting animation cycles 1..4.
    assert(wifi_signal_anim_level(0) == 1);
    assert(wifi_signal_anim_level(1) == 2);
    assert(wifi_signal_anim_level(2) == 3);
    assert(wifi_signal_anim_level(3) == 4);
    assert(wifi_signal_anim_level(4) == 1);
    assert(wifi_signal_anim_level(7) == 4);
    assert(wifi_signal_anim_level(8) == 1);

    puts("wifi_signal tests: PASS");
    return 0;
}
