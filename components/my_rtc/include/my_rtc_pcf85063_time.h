#pragma once

#include <stdint.h>
#include <time.h>

#include "my_rtc_time.h"

namespace MyRtcPcf85063Time {

inline bool decode(const uint8_t *r, time_t &out)
{
    const uint8_t fields[] = {
        static_cast<uint8_t>(r[0] & 0x7F),
        static_cast<uint8_t>(r[1] & 0x7F),
        static_cast<uint8_t>(r[2] & 0x3F),
        static_cast<uint8_t>(r[3] & 0x3F),
        static_cast<uint8_t>(r[4] & 0x07),
        static_cast<uint8_t>(r[5] & 0x1F),
        r[6],
    };
    for (uint8_t field : fields) {
        if (!MyRtcTime::bcdOk(field)) return false;
    }

    const int second = MyRtcTime::fromBcd(fields[0]);
    const int minute = MyRtcTime::fromBcd(fields[1]);
    const int hour = MyRtcTime::fromBcd(fields[2]);
    const int day = MyRtcTime::fromBcd(fields[3]);
    const int weekday = MyRtcTime::fromBcd(fields[4]);
    const int month = MyRtcTime::fromBcd(fields[5]);
    const int year = 2000 + MyRtcTime::fromBcd(fields[6]);
    if (second > 59 || minute > 59 || hour > 23 || day < 1 || month < 1 || month > 12 ||
        weekday > 6 || day > MyRtcTime::daysInMonth(year, month))
        return false;

    out = static_cast<time_t>(MyRtcTime::daysFromCivil(year, month, day) * 86400 +
                              hour * 3600 + minute * 60 + second);
    return true;
}

inline void encode(time_t utc, uint8_t *r)
{
    int64_t days = static_cast<int64_t>(utc) / 86400;
    int64_t seconds = static_cast<int64_t>(utc) % 86400;
    if (seconds < 0) {
        seconds += 86400;
        --days;
    }
    int year = 0, month = 0, day = 0;
    MyRtcTime::civilFromDays(days, year, month, day);
    r[0] = MyRtcTime::toBcd(static_cast<int>(seconds % 60));
    r[1] = MyRtcTime::toBcd(static_cast<int>(seconds / 60 % 60));
    r[2] = MyRtcTime::toBcd(static_cast<int>(seconds / 3600));
    r[3] = MyRtcTime::toBcd(day);
    r[4] = static_cast<uint8_t>((days + 4) % 7);  // PCF85063: Sunday = 0.
    r[5] = MyRtcTime::toBcd(month);
    r[6] = MyRtcTime::toBcd(year % 100);
}

}  // namespace MyRtcPcf85063Time
