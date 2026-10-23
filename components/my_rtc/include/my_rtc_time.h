// my_rtc_time.h - pure helpers for the DS3231 register format (no IDF dependency, host-testable).
// Time is UTC; no TZ / timegm needed.
#pragma once

#include <stdint.h>
#include <time.h>

namespace MyRtcTime {

inline uint8_t toBcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
inline int fromBcd(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
inline bool bcdOk(uint8_t b) { return (b & 0x0F) <= 9 && (b >> 4) <= 9; }

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
inline int64_t daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline void civilFromDays(int64_t z, int &y, int &m, int &d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

inline int daysInMonth(int y, int m)
{
    static const uint8_t DIM[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : DIM[m - 1];
}

// regs[0..6] = seconds, minutes, hours, weekday, date, month/century, year (DS3231 0x00..0x06).
// Returns false when a field is out of range (garbage after a power loss).
inline bool decode(const uint8_t *r, time_t &out)
{
    for (int i = 0; i < 7; i++) {
        uint8_t v = r[i];
        if (i == 2) v &= (r[2] & 0x40) ? 0x1F : 0x3F;
        if (i == 5) v &= 0x1F;
        if (!bcdOk(v)) return false;
    }
    int sec = fromBcd(r[0] & 0x7F), min = fromBcd(r[1] & 0x7F);
    int hour;
    if (r[2] & 0x40) {   // 12 h mode (never written by us, but handle it)
        hour = fromBcd(r[2] & 0x1F) % 12;
        if (r[2] & 0x20) hour += 12;
    } else {
        hour = fromBcd(r[2] & 0x3F);
    }
    int day = fromBcd(r[4] & 0x3F), mon = fromBcd(r[5] & 0x1F);
    int year = 2000 + fromBcd(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    if (sec > 59 || min > 59 || hour > 23 || mon < 1 || mon > 12 || day < 1 || day > daysInMonth(year, mon))
        return false;
    out = (time_t)(daysFromCivil(year, mon, day) * 86400 + hour * 3600 + min * 60 + sec);
    return true;
}

inline void encode(time_t t, uint8_t *r)
{
    int64_t days = (int64_t)t / 86400;
    int64_t rem = (int64_t)t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int y, m, d;
    civilFromDays(days, y, m, d);
    int wd = (int)((days % 7 + 11) % 7);   // 1970-01-01 was a Thursday -> 1 = Monday .. 7 = Sunday
    r[0] = toBcd((int)(rem % 60));
    r[1] = toBcd((int)(rem / 60 % 60));
    r[2] = toBcd((int)(rem / 3600));       // 24 h mode
    r[3] = (uint8_t)(wd == 0 ? 7 : wd);
    r[4] = toBcd(d);
    r[5] = (uint8_t)(toBcd(m) | (y >= 2100 ? 0x80 : 0));
    r[6] = toBcd(y % 100);
}

}  // namespace MyRtcTime
