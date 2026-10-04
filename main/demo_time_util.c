// main/demo_time_util.c — 见 demo_time_util.h。
#include "demo_time_util.h"

void demo_time_format(struct tm t, char *date_out, size_t date_len,
                      char *time_out, size_t time_len)
{
    if (date_out && date_len > 0) {
        if (strftime(date_out, date_len, "%Y-%m-%d %a", &t) == 0) {
            date_out[0] = '\0';
        }
    }
    if (time_out && time_len > 0) {
        if (strftime(time_out, time_len, "%H:%M:%S", &t) == 0) {
            time_out[0] = '\0';
        }
    }
}

const char *demo_time_ntp_state(int sntp_inited, int wifi_online,
                                unsigned sync_count)
{
    if (!sntp_inited) {
        return "STOPPED";
    }
    if (sync_count > 0) {
        return "SYNCED";
    }
    return wifi_online ? "SYNCING" : "WAITING WIFI";
}

int demo_time_is_synced(time_t now)
{
    return now >= (time_t)DEMO_TIME_EPOCH_GUARD;
}
