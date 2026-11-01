#include "power_interval.h"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace PowerInterval;
    Totals t;
    Sample active{0, true, true, true, false, 80, 60, 25, 4100};
    t.add(active, 100000);
    Sample asleep{6, false, false, false, true, 80, 60, 25, 4000};
    t.add(asleep, 200000);
    assert(t.elapsed == PERIOD_MS);
    assert(t.screen == 100000 && t.wifi == 100000 && t.audio == 100000);
    assert(t.eco == 200000 && t.games[0] == 100000 && t.games[6] == 200000);
    assert(t.brightness / t.elapsed == 26);
    assert(t.batteryMin == 4000 && t.batteryMax == 4100);
    assert(t.batterySum / t.batterySamples == 4050);
    Sample missing{99, false, false, false, false, 0, 0, 0, 0};
    t.add(missing, 1000);
    assert(t.batterySamples == 2 && t.games[6] == 201000);
    uint32_t before = UINT32_MAX - 100, after = 149;
    assert(after - before == 250); // millisecond / byte counter wrap
    t = {};
    assert(t.elapsed == 0 && t.batterySamples == 0 && t.batteryMin == UINT16_MAX);
    puts("Power interval: durations, weighted brightness, battery validity and reset passed");
}
