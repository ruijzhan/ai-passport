// main/power_charge.c — see power_charge.h.
#include "power_charge.h"

#include <stddef.h>
#include <string.h>

void power_charge_reset(power_charge_t *c)
{
    c->count = 0;
}

void power_charge_push(power_charge_t *c, int64_t now_s, int mv)
{
    if (c == NULL || mv <= 0) {
        return;
    }
    if (c->count > 0 && now_s < c->buf[c->count - 1].t_s) {
        return;  // clock stepped back (tests); keep the old history
    }
    if (c->count == POWER_CHARGE_HISTORY) {
        memmove(&c->buf[0], &c->buf[1],
                (size_t)(c->count - 1) * sizeof(c->buf[0]));
        c->count--;
    }
    c->buf[c->count].t_s = now_s;
    c->buf[c->count].mv = mv;
    c->count++;
    // Compact once: drop samples outside the window relative to newest.
    int keep = 0;
    for (int i = 0; i < c->count; i++) {
        if (now_s - c->buf[i].t_s <= POWER_CHARGE_RISE_WINDOW_S) {
            c->buf[keep++] = c->buf[i];
        }
    }
    c->count = keep;
}

bool power_charge_rising(const power_charge_t *c)
{
    if (c == NULL || c->count < POWER_CHARGE_RISE_MIN_SAMPLES) {
        return false;
    }
    return c->buf[c->count - 1].mv - c->buf[0].mv >= POWER_CHARGE_RISE_MIN_MV;
}

bool power_charge_inhibit(bool usb_connected, int soc_pct, const power_charge_t *c)
{
    // soc_pct < 0 (gauge not ready, read failure) fails the comparison and
    // so leaves the SOC signal disabled, as the header documents.
    return usb_connected || soc_pct >= POWER_CHARGE_SOC_INHIBIT_PCT ||
        power_charge_rising(c);
}
