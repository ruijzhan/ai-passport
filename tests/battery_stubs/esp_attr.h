#pragma once

// Host stub: on ESP-IDF RTC_DATA_ATTR places variables in RTC slow memory.
// For host tests plain static storage is sufficient.
#define RTC_DATA_ATTR
