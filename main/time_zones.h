#pragma once

#include <stddef.h>
#include <string.h>

namespace TimeZones {
struct Zone { const char *label; const char *tz; };
inline constexpr Zone ZONES[] = {
    {"(UTC-11:00) Samoa (amerikanska)", "SST11"},
    {"(UTC-10:00) Hawaii", "HST10"},
    {"(UTC-09:00) Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"(UTC-08:00) Los Angeles, Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"(UTC-07:00) Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"(UTC-07:00) Phoenix (utan sommartid)", "MST7"},
    {"(UTC-06:00) Chicago, Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
    {"(UTC-06:00) Mexico City (utan sommartid)", "CST6"},
    {"(UTC-05:00) New York, Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"(UTC-05:00) Bogota, Lima", "EST5"},
    {"(UTC-04:00) Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"(UTC-03:30) Newfoundland", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"(UTC-03:00) Buenos Aires, Sao Paulo", "BRT3"},
    {"(UTC-01:00) Azorerna", "AZOT1AZOST,M3.5.0/0,M10.5.0/1"},
    {"(UTC+00:00) UTC, Reykjavik (utan sommartid)", "UTC0"},
    {"(UTC+00:00) London, Dublin, Lissabon", "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"(UTC+01:00) Stockholm, Berlin, Paris, Rom", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"(UTC+02:00) Helsingfors, Tallinn, Aten", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"(UTC+02:00) Johannesburg (utan sommartid)", "SAST-2"},
    {"(UTC+03:00) Istanbul, Moskva, Nairobi", "TRT-3"},
    {"(UTC+04:00) Dubai", "GST-4"},
    {"(UTC+05:00) Karachi", "PKT-5"},
    {"(UTC+05:30) Indien", "IST-5:30"},
    {"(UTC+05:45) Kathmandu", "NPT-5:45"},
    {"(UTC+06:00) Dhaka", "BST-6"},
    {"(UTC+07:00) Bangkok, Hanoi, Jakarta", "ICT-7"},
    {"(UTC+08:00) Peking, Singapore, Perth", "CST-8"},
    {"(UTC+09:00) Tokyo, Seoul", "JST-9"},
    {"(UTC+09:30) Darwin (utan sommartid)", "ACST-9:30"},
    {"(UTC+09:30) Adelaide", "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3"},
    {"(UTC+10:00) Brisbane (utan sommartid)", "AEST-10"},
    {"(UTC+10:00) Sydney, Melbourne", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
    {"(UTC+12:00) Auckland, Wellington", "NZST-12NZDT,M9.5.0/2,M4.1.0/3"},
    {"(UTC+13:00) Samoa", "WSST-13"},
    {"(UTC+14:00) Kiritimati", "LINT-14"},
};
inline constexpr size_t COUNT = sizeof(ZONES) / sizeof(ZONES[0]);
inline int find(const char *tz)
{
    for (size_t i = 0; i < COUNT; ++i)
        if (strcmp(tz, ZONES[i].tz) == 0) return static_cast<int>(i);
    return -1;
}
}
