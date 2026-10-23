// my_time.h - wall clock: NTP when the network is up, manual setting otherwise, NVS restore.
//
// Boards without an RTC battery lose the time at power-off. Sources, best first:
//   NTP       synced via SNTP (3 servers)
//   MANUAL    set by hand on the device or from the web page (browser time)
//   RESTORED  last time saved in NVS (every 10 min + on manual set). Always too early after a
//             power cut, so it is "uncertain" until NTP or a manual set.
//   NONE      nothing known
// Logs (my_log) use the clock automatically as soon as it is valid.
#pragma once

#include <stdint.h>
#include <time.h>

namespace MyTime {

enum class Source : uint8_t { NONE = 0, RESTORED, MANUAL, NTP };

constexpr time_t MIN_EPOCH = 1704067200;   // 2024-01-01 00:00 UTC
constexpr time_t MAX_EPOCH = 4102444800;   // 2100-01-01 00:00 UTC

// nvsNamespace/nvsKey: where the last known time is kept (u64 epoch). tz: POSIX TZ string.
void begin(const char *nvsNamespace, const char *nvsKey, const char *tz);
void loop();                     // Periodic NVS save; call every few hundred ms
void onNetworkUp();              // Starts / refreshes SNTP
void setTz(const char *tz);

bool valid();                    // Time is set (any source except NONE)
bool certain();                  // NTP or MANUAL
Source source();
const char *sourceText(Source s);   // Swedish UI text
time_t now();                    // 0 if not valid
bool setManual(time_t epoch);    // UTC epoch; false if out of range
bool local(struct tm &out);      // Local broken-down time; false if not valid
uint32_t lastNtpSyncS();         // Seconds since the last NTP sync (UINT32_MAX = never)
uint32_t ntpSyncs();             // Successful syncs since boot

}  // namespace MyTime
