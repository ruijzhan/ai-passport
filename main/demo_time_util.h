// main/demo_time_util.h — Time 演示页的纯文本辅助函数,不依赖 ESP-IDF/LVGL。
// 可在宿主机上直接编译测试;耗时/网络状态机保持与 LVGL 无关。
#pragma once

#include <stddef.h>
#include <time.h>

// 早于该 UTC 秒数的系统时间视为从未 NTP 同步(2000-01-01 00:00:00)。
#define DEMO_TIME_EPOCH_GUARD 946684800L

// 把 struct tm 格式化为 "YYYY-MM-DD DDD" 与 "HH:MM:SS";输出截断时置空串。
void demo_time_format(struct tm t, char *date_out, size_t date_len,
                      char *time_out, size_t time_len);

// NTP 客户端状态名: "STOPPED" / "SYNCED" / "SYNCING" / "WAITING WIFI"。
const char *demo_time_ntp_state(int sntp_inited, int wifi_online,
                                unsigned sync_count);

// 系统时间是否可信(达到守卫年份即认为此前已同步过)。
int demo_time_is_synced(time_t now);
