// main/usage_parse.c — minimal JSON field scanner for the usage response.
// See usage_parse.h for the contract.
#include "usage_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Days since 1970-01-01 (Howard Hinnant's algorithm), valid for all int years.
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (int)(m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153U * (m + (m > 2 ? -3U : 9U)) + 2U) / 5U + d - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int yy = (int)yoe + (int)(era * 400);
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    const unsigned dd = doy - (153U * mp + 2U) / 5U + 1U;
    const unsigned mm = mp + (mp < 10U ? 3U : (unsigned)-9);
    *y = yy + (mm <= 2U);
    *m = mm;
    *d = dd;
}

// Locate the object following "key": bounded by its braces. Window objects
// contain only scalars, so the first '{' ... first '}' pair is sufficient.
static bool window_span(const char *json, const char *key,
                        const char **begin, const char **end)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *k = strstr(json, pattern);
    if (!k) return false;
    const char *open = strchr(k + strlen(pattern), '{');
    if (!open) return false;
    const char *close = strchr(open, '}');
    if (!close) return false;
    *begin = open;
    *end = close;
    return true;
}

// Find `:"value"` or `:value` for a field inside [begin, end).
static const char *field_value(const char *begin, const char *end,
                               const char *field)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    const char *p = begin;
    while (p < end) {
        p = strstr(p, pattern);
        if (!p || p >= end) return NULL;
        const char *colon = strchr(p + strlen(pattern), ':');
        if (!colon || colon >= end) return NULL;
        // Reject matches from a longer name such as "percent_x".
        const char *after_key = p + strlen(pattern);
        if (after_key < colon) {
            const char c = *after_key;
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                p = after_key;
                continue;
            }
        }
        p = colon + 1;
        while (p < end && (*p == ' ' || *p == '\t' ||
                           *p == '\r' || *p == '\n')) p++;
        return (p < end) ? p : NULL;
    }
    return NULL;
}

static bool parse_status_ok(const char *begin, const char *end)
{
    const char *v = field_value(begin, end, "status");
    if (!v || v + 4 > end || *v != '"') return false;
    return strncmp(v + 1, "ok\"", 3) == 0;
}

static bool parse_percent(const char *begin, const char *end, int *percent)
{
    const char *v = field_value(begin, end, "percent");
    if (!v) return false;
    char *stop = NULL;
    // strtod tolerates both "4" and "4.0"; trailing text is ignored.
    double d = strtod(v, &stop);
    if (stop == v || d < 0.0 || d > 100.0) return false;
    *percent = (int)(d + 0.5);
    if (*percent < 0) *percent = 0;
    if (*percent > 100) *percent = 100;
    return true;
}

int64_t usage_parse_time(const char *iso8601)
{
    if (!iso8601) return -1;
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0, ss = 0;
    // Requires the full date-time prefix; fraction and 'Z' are optional.
    if (sscanf(iso8601, "%4d-%2d-%2dT%2d:%2d:%2d",
               &y, &mo, &d, &hh, &mm, &ss) != 6) return -1;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return -1;
    // Reject impossible month/day combos (leap years included).
    static const unsigned char dim[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    unsigned limit = dim[(unsigned)mo - 1U];
    if (mo == 2 &&
        ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) limit = 29;
    if ((unsigned)d > limit) return -1;
    return days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 +
           (int64_t)hh * 3600 + (int64_t)mm * 60 + ss;
}

static void parse_window(const char *json, const char *key,
                         usage_window_t *w)
{
    w->valid = false;
    w->percent = 0;
    w->resets_at_utc = -1;
    w->has_reset = false;
    const char *begin = NULL, *end = NULL;
    if (!window_span(json, key, &begin, &end)) return;
    if (!parse_status_ok(begin, end)) return;
    if (!parse_percent(begin, end, &w->percent)) return;
    w->valid = true;
    const char *v = field_value(begin, end, "resetsAt");
    if (v && *v == '"') {
        char stamp[40];
        size_t i = 0;
        v++;
        while (v < end && *v != '"' && i + 1 < sizeof(stamp)) {
            stamp[i++] = *v++;
        }
        stamp[i] = '\0';
        int64_t t = usage_parse_time(stamp);
        if (t >= 0) {
            w->resets_at_utc = t;
            w->has_reset = true;
        }
    }
}

void usage_parse(const char *json, usage_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->rolling.resets_at_utc = -1;
    out->weekly.resets_at_utc = -1;
    out->monthly.resets_at_utc = -1;
    if (!json) return;
    parse_window(json, "rolling", &out->rolling);
    parse_window(json, "weekly", &out->weekly);
    parse_window(json, "monthly", &out->monthly);
}

void usage_format_countdown(int64_t now_utc, int64_t reset_utc,
                            char *buf, size_t len)
{
    if (!buf || len == 0) return;
    if (reset_utc < 0) {
        snprintf(buf, len, "--");
        return;
    }
    int64_t diff = reset_utc - now_utc;
    if (diff < 0) diff = 0;
    long days = (long)(diff / 86400);
    unsigned hh = (unsigned)((diff % 86400) / 3600);
    unsigned mm = (unsigned)((diff % 3600) / 60);
    unsigned ss = (unsigned)(diff % 60);
    if (days > 0) {
        snprintf(buf, len, "%ldd %02u:%02u:%02u", days, hh, mm, ss);
    } else {
        snprintf(buf, len, "%02u:%02u:%02u", hh, mm, ss);
    }
}

void usage_format_utc(int64_t epoch, char *buf, size_t len)
{
    if (!buf || len == 0) return;
    if (epoch < 0) {
        snprintf(buf, len, "--");
        return;
    }
    int64_t days = epoch / 86400;
    unsigned tod = (unsigned)(epoch % 86400);
    int y = 0;
    unsigned mo = 0, d = 0;
    civil_from_days(days, &y, &mo, &d);
    snprintf(buf, len, "%04d-%02u-%02u %02u:%02u:%02u",
             y, mo, d, tod / 3600U, (tod % 3600U) / 60U, tod % 60U);
}

static unsigned month_days(int y, unsigned mo)
{
    static const unsigned char dim[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (mo < 1U || mo > 12U) return 0;
    unsigned limit = dim[mo - 1U];
    if (mo == 2U &&
        ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) limit = 29;
    return limit;
}

int usage_days_in_month(int64_t epoch_utc)
{
    if (epoch_utc < 0) return -1;
    int64_t days = epoch_utc / 86400;
    int y = 0;
    unsigned mo = 0, d = 0;
    civil_from_days(days, &y, &mo, &d);
    unsigned limit = month_days(y, mo);
    if (limit == 0) return -1;
    return (int)limit;
}

int64_t usage_month_period_s(int64_t now_utc)
{
    int dim = usage_days_in_month(now_utc);
    if (dim <= 0) return -1;
    return (int64_t)dim * 86400;
}

int usage_time_progress(int64_t now_utc, int64_t reset_utc, int64_t period_s)
{
    if (reset_utc < 0 || period_s <= 0 || now_utc < 0) return -1;
    int64_t remaining = reset_utc - now_utc;
    if (remaining <= 0) return 100;
    if (remaining >= period_s) return 0;
    int64_t elapsed = period_s - remaining;
    long pct = (long)((elapsed * 100 + period_s / 2) / period_s);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)pct;
}
