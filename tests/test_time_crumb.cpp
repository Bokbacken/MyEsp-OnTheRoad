// Host tests for components/my_time/include/my_time_crumb.h.
//   g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/my_time/include tests/test_time_crumb.cpp -o /tmp/test_time_crumb && /tmp/test_time_crumb
#include <cstdio>
#include <cstring>

#include "my_time_crumb.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) g_pass++;                                                      \
        else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

using namespace MyTimeCrumb;

int main()
{
    const time_t MIN_T = 1704067200, MAX_T = 4102444800LL, T = 1793500000;
    Crumb c;
    std::memset(&c, 0xA5, sizeof(c));   // Garbage after power-on
    CHECK(!valid(c, MIN_T, MAX_T));
    CHECK(newestFallback(0, c, MIN_T, MAX_T) == 0);
    CHECK(newestFallback(T, c, MIN_T, MAX_T) == T);

    seal(c, T, true);
    CHECK(valid(c, MIN_T, MAX_T) && certain(c));
    seal(c, T, false);
    CHECK(valid(c, MIN_T, MAX_T) && !certain(c));

    // NVS is saved at most once a minute; a newer crumb wins, an older one does not.
    CHECK(newestFallback(T - 59, c, MIN_T, MAX_T) == T);
    CHECK(newestFallback(T + 5, c, MIN_T, MAX_T) == T + 5);
    CHECK(newestFallback(MAX_T + 1, c, MIN_T, MAX_T) == T);

    Crumb bad = c;
    bad.utc += 1;   // Corrupted value without matching checksum
    CHECK(!valid(bad, MIN_T, MAX_T));
    bad = c;
    bad.flags = 2;
    bad.check = checksum(bad);
    CHECK(!valid(bad, MIN_T, MAX_T));
    seal(bad, MIN_T - 1, true);
    CHECK(!valid(bad, MIN_T, MAX_T));

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
