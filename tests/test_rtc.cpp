// Host tests for components/my_rtc/include/my_rtc_time.h (DS3231 BCD <-> UTC epoch).
//   g++ -std=c++17 -Wall -Wextra -Icomponents/my_rtc/include tests/test_rtc.cpp -o tests/test_rtc && tests/test_rtc
#include <cstdio>
#include <cstring>

#include "my_rtc_time.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) g_pass++;                                                      \
        else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

using namespace MyRtcTime;

int main()
{
    uint8_t r[7];
    // 2026-10-23 19:30:05 UTC = 1792783805, a Friday
    encode((time_t)1792783805, r);
    CHECK(r[0] == 0x05 && r[1] == 0x30 && r[2] == 0x19);
    CHECK(r[3] == 5);                       // Friday (1 = Monday)
    CHECK(r[4] == 0x23 && r[5] == 0x10 && r[6] == 0x26);
    time_t t = 0;
    CHECK(decode(r, t) && t == 1792783805);

    // 1970-01-01 was a Thursday; 2024-01-01 a Monday; 2024-03-03 a Sunday
    encode(0, r);
    CHECK(r[3] == 4);
    encode((time_t)1704067200, r);
    CHECK(r[3] == 1 && r[4] == 0x01 && r[5] == 0x01 && r[6] == 0x24);
    encode((time_t)(1704067200 + 62LL * 86400), r);
    CHECK(r[3] == 7 && r[4] == 0x03 && r[5] == 0x03);

    // Leap day 2024-02-29 valid, 2025-02-29 invalid, 2100 not a leap year
    const uint8_t leap24[7] = {0x00, 0x00, 0x12, 4, 0x29, 0x02, 0x24};
    CHECK(decode(leap24, t) && t == (time_t)(1704067200 + 59LL * 86400 + 12 * 3600));
    const uint8_t leap25[7] = {0x00, 0x00, 0x12, 6, 0x29, 0x02, 0x25};
    CHECK(!decode(leap25, t));
    CHECK(daysInMonth(2100, 2) == 28 && daysInMonth(2000, 2) == 29);

    // Garbage after a power loss
    const uint8_t badBcd[7] = {0x5A, 0x00, 0x00, 1, 0x01, 0x01, 0x24};
    CHECK(!decode(badBcd, t));
    const uint8_t badMonth[7] = {0x00, 0x00, 0x00, 1, 0x01, 0x13, 0x24};
    CHECK(!decode(badMonth, t));
    const uint8_t zeroDay[7] = {0x00, 0x00, 0x00, 1, 0x00, 0x01, 0x24};
    CHECK(!decode(zeroDay, t));
    const uint8_t badHour[7] = {0x00, 0x00, 0x24, 1, 0x01, 0x01, 0x24};
    CHECK(!decode(badHour, t));

    // 12 h mode: 0x40 = 12 h, 0x20 = PM -> 11 PM = 23:00
    const uint8_t pm[7] = {0x00, 0x00, 0x40 | 0x20 | 0x11, 1, 0x01, 0x01, 0x24};
    CHECK(decode(pm, t) && t == (time_t)(1704067200 + 23 * 3600));
    const uint8_t am12[7] = {0x00, 0x00, 0x40 | 0x12, 1, 0x01, 0x01, 0x24};   // 12 AM = 00:00
    CHECK(decode(am12, t) && t == (time_t)1704067200);

    // Round trip every ~37 h from 2024 to 2099, plus month/year edges
    int bad = 0;
    for (int64_t s = 1704067200; s < 4102444800LL; s += 133337) {
        encode((time_t)s, r);
        time_t back = 0;
        if (!decode(r, back) || back != (time_t)s) bad++;
    }
    CHECK(bad == 0);
    encode((time_t)4102444799LL, r);   // 2099-12-31 23:59:59
    CHECK(r[4] == 0x31 && r[5] == 0x12 && r[6] == 0x99 && decode(r, t) && t == (time_t)4102444799LL);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
