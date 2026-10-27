// fat_time.cpp - supplies FATFS's get_fattime() so files written on FFat and the SD card get
// real local timestamps. Without it, IDF's default returns a fixed/garbage FAT date whose 7-bit
// year field overflows (1980+127 = 2107), which showed up in the file manager as "2108-04-01".
//
// Firmware only: FATFS is not linked by the host tests, so this file lives in main/ (not in the
// my_storage component, which must not depend on my_time).
#include <stdint.h>
#include <time.h>

#include "my_time.h"

// FATFS (fatfs component) declares `DWORD get_fattime(void)` with C linkage and ships a weak
// default. This strong definition overrides it. Packed FAT date/time:
//   bits 31..25 year-1980, 24..21 month, 20..16 day, 15..11 hour, 10..5 minute, 4..0 second/2.
extern "C" uint32_t get_fattime(void)
{
    int year = 2021, mon = 1, day = 1, hour = 0, min = 0, sec = 0;   // Safe fallback (year <= 2107)
    struct tm t = {};
    if (MyTime::certain() && MyTime::local(t)) {
        const int y = t.tm_year + 1900;
        if (y >= 1980 && y <= 2107) {
            year = y;
            mon = t.tm_mon + 1;
            day = t.tm_mday;
            hour = t.tm_hour;
            min = t.tm_min;
            sec = t.tm_sec;
        }
    }
    return ((uint32_t)(year - 1980) << 25) | ((uint32_t)mon << 21) | ((uint32_t)day << 16) |
           ((uint32_t)hour << 11) | ((uint32_t)min << 5) | ((uint32_t)(sec / 2));
}
