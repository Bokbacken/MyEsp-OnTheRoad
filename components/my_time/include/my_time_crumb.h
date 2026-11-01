#pragma once

// "Last alive" time breadcrumb kept in RTC_NOINIT memory. Pure logic for host tests.
// RTC_NOINIT survives panic, watchdog and software resets, but not power loss.

#include <stdint.h>
#include <time.h>

namespace MyTimeCrumb {

constexpr uint32_t MAGIC = 0x4C415354;   // "LAST"

struct Crumb {
    uint32_t magic;
    uint32_t flags;   // bit 0: time was certain when written
    int64_t utc;
    uint64_t check;
};

inline uint64_t checksum(const Crumb &c)
{
    return (static_cast<uint64_t>(c.utc) * 0x9E3779B97F4A7C15ULL) ^ c.flags ^ MAGIC;
}

inline void seal(Crumb &c, time_t utc, bool certain)
{
    c.magic = 0;
    c.utc = utc;
    c.flags = certain ? 1u : 0u;
    c.check = checksum(c);
    c.magic = MAGIC;
}

inline bool valid(const Crumb &c, time_t minT, time_t maxT)
{
    return c.magic == MAGIC && c.check == checksum(c) && c.flags <= 1u && c.utc >= minT && c.utc < maxT;
}

inline bool certain(const Crumb &c) { return (c.flags & 1u) != 0; }

// The newest plausible fallback wins; returns 0 if neither is usable.
inline time_t newestFallback(time_t nvs, const Crumb &c, time_t minT, time_t maxT)
{
    const time_t n = nvs >= minT && nvs < maxT ? nvs : 0;
    const time_t r = valid(c, minT, maxT) ? static_cast<time_t>(c.utc) : 0;
    return n > r ? n : r;
}

}  // namespace MyTimeCrumb
