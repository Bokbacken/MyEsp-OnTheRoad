#pragma once

#include <limits.h>
#include <stdint.h>
#include <sys/time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace MyRtcSync {

struct Sample {
    timeval wall;
    int64_t monoBefore;
    int64_t monoAfter;
};

inline int64_t micros(const timeval &tv)
{
    return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

inline bool valid(const timeval &tv, time_t min, time_t max)
{
    return tv.tv_sec >= min && tv.tv_sec < max && tv.tv_usec >= 0 && tv.tv_usec < 1000000;
}

inline bool sample(Sample &s, time_t min, time_t max)
{
    s.monoBefore = esp_timer_get_time();
    const bool ok = gettimeofday(&s.wall, nullptr) == 0;
    s.monoAfter = esp_timer_get_time();
    return ok && valid(s.wall, min, max);
}

// RTC seconds describe [rtc, rtc+1), sampled somewhere inside the read bracket.
// Report only whole seconds of drift guaranteed by those two uncertainty intervals.
inline bool compare(time_t rtc, const Sample &before, const Sample &after, int32_t &drift)
{
    const int64_t elapsed = micros(after.wall) - micros(before.wall);
    // Both clocks have microsecond resolution. Bracket each wall sample with the
    // monotonic clock to reject a step without assuming an I2C latency/precision.
    // MyTime uses IDF's default immediate SNTP mode, not adjtime/smooth slew.
    if (before.monoAfter < before.monoBefore || after.monoAfter < after.monoBefore ||
        after.monoBefore < before.monoAfter || elapsed < 0 ||
        elapsed < after.monoBefore - before.monoAfter - 1 ||
        elapsed > after.monoAfter - before.monoBefore + 1)
        return false;
    const int64_t lower = static_cast<int64_t>(rtc) * 1000000 - micros(after.wall);
    const int64_t upper = (static_cast<int64_t>(rtc) + 1) * 1000000 - micros(before.wall);
    const int64_t seconds = lower >= 1000000 ? lower / 1000000 :
                            upper <= -1000000 ? upper / 1000000 : 0;
    drift = static_cast<int32_t>(seconds > INT32_MAX ? INT32_MAX :
                                 seconds < INT32_MIN ? INT32_MIN : seconds);
    return true;
}

inline bool alignedSystemTime(time_t &utc, time_t min, time_t max)
{
    timeval tv = {};
    if (gettimeofday(&tv, nullptr) != 0 || !valid(tv, min, max)) return false;
    // vTaskDelay can start just before a tick. The extra tick ensures the
    // requested remainder elapses; one bounded sleep, no polling/busy loop.
    const uint64_t remaining = 1000000 - tv.tv_usec;
    const TickType_t ticks = static_cast<TickType_t>(
        (remaining * configTICK_RATE_HZ + 999999) / 1000000 + 1);
    vTaskDelay(ticks);
    if (gettimeofday(&tv, nullptr) != 0 || !valid(tv, min, max)) return false;
    utc = tv.tv_sec; // Never pre-write the next second by rounding a late wakeup.
    return true;
}

}  // namespace MyRtcSync
