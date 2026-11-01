#include "time_zones.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <ctime>

static tm local(const char *tz, int month, int day, int hour, int minute = 0)
{
    setenv("TZ", tz, 1);
    tzset();
    tm utc{};
    utc.tm_year = 126;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    const time_t epoch = timegm(&utc);
    tm result{};
    localtime_r(&epoch, &result);
    return result;
}

int main()
{
    size_t optionsBytes = 0;
    for (size_t i = 0; i < TimeZones::COUNT; ++i) {
        const auto &zone = TimeZones::ZONES[i];
        assert(strlen(zone.tz) < 48);
        assert(TimeZones::find(zone.tz) == static_cast<int>(i));
        optionsBytes += strlen(zone.label) + 1;
    }
    assert(optionsBytes + 64 < 2048);
    const char *sweden = "CET-1CEST,M3.5.0,M10.5.0/3";
    assert(TimeZones::find(sweden) >= 0 && TimeZones::find("custom") == -1);
    assert(local(sweden, 1, 15, 12).tm_hour == 13);
    assert(local(sweden, 7, 15, 12).tm_hour == 14);
    auto before = local(sweden, 3, 29, 0, 59), after = local(sweden, 3, 29, 1);
    assert(before.tm_hour == 1 && before.tm_min == 59 && !before.tm_isdst);
    assert(after.tm_hour == 3 && after.tm_isdst);
    before = local(sweden, 10, 25, 0, 59); after = local(sweden, 10, 25, 1);
    assert(before.tm_hour == 2 && before.tm_min == 59 && before.tm_isdst);
    assert(after.tm_hour == 2 && !after.tm_isdst);
    assert(local("IST-5:30", 1, 15, 12).tm_hour == 17);
    assert(local("IST-5:30", 1, 15, 12).tm_min == 30);
    assert(local("AEST-10AEDT,M10.1.0/2,M4.1.0/3", 1, 15, 12).tm_hour == 23);
    assert(local("AEST-10AEDT,M10.1.0/2,M4.1.0/3", 7, 15, 12).tm_hour == 22);
    puts("Readable timezone catalogue: bounds, Swedish DST edges, half-hour and southern zones passed");
}
