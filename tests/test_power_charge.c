// tests/test_power_charge.c — host tests for main/power_charge.c.
#include <assert.h>
#include <stdio.h>

#include "power_charge.h"

int main(void)
{
    power_charge_t c;

    // Empty history inhibits nothing.
    power_charge_reset(&c);
    assert(c.count == 0);
    assert(!power_charge_rising(&c));
    assert(!power_charge_inhibit(false, -1, &c));
    assert(!power_charge_inhibit(false, 50, &c));

    // USB host alone inhibits (even with dead SOC signal).
    assert(power_charge_inhibit(true, -1, &c));
    assert(power_charge_inhibit(true, 0, &c));

    // Near-full SOC inhibits, boundary inclusive; just below does not.
    assert(power_charge_inhibit(false, 95, &c));
    assert(power_charge_inhibit(false, 100, &c));
    assert(!power_charge_inhibit(false, 94, &c));
    assert(!power_charge_inhibit(false, -1, &c));

    // Slope needs enough in-window samples.
    power_charge_push(&c, 0, 4100);
    power_charge_push(&c, 60, 4104);
    assert(!power_charge_rising(&c));  // only 2 of 3 required
    assert(!power_charge_inhibit(false, 50, &c));
    power_charge_push(&c, 120, 4108);  // +8 mV over 120 s
    assert(power_charge_rising(&c));
    assert(power_charge_inhibit(false, 50, &c));

    // Flat or falling voltage does not inhibit.
    power_charge_reset(&c);
    power_charge_push(&c, 0, 4100);
    power_charge_push(&c, 60, 4100);
    power_charge_push(&c, 120, 4100);
    assert(!power_charge_rising(&c));
    power_charge_reset(&c);
    power_charge_push(&c, 0, 4120);
    power_charge_push(&c, 60, 4110);
    power_charge_push(&c, 120, 4100);
    assert(!power_charge_rising(&c));

    // Stale samples fall out of the window relative to the newest push.
    power_charge_reset(&c);
    power_charge_push(&c, 0, 4100);
    power_charge_push(&c, 60, 4108);
    power_charge_push(&c, 120, 4116);
    assert(power_charge_rising(&c));
    power_charge_push(&c, 4000, 4116);  // window now [3400, 4000]: one sample
    assert(c.count == 1);
    assert(!power_charge_rising(&c));

    // Bad reads never poison history; backward timestamps are ignored.
    power_charge_reset(&c);
    power_charge_push(&c, 100, -1);
    power_charge_push(&c, 100, 0);
    assert(c.count == 0);
    power_charge_push(&c, 200, 4100);
    power_charge_push(&c, 100, 4200);  // stepped back: dropped
    assert(c.count == 1 && c.buf[0].mv == 4100);

    // Ring overflow keeps the newest samples.
    power_charge_reset(&c);
    for (int i = 0; i < POWER_CHARGE_HISTORY + 2; i++) {
        power_charge_push(&c, (int64_t)i * 10, 4000 + i);
    }
    assert(c.count == POWER_CHARGE_HISTORY);
    assert(c.buf[c.count - 1].mv == 4000 + POWER_CHARGE_HISTORY + 1);

    printf("test_power_charge: PASS\n");
    return 0;
}
