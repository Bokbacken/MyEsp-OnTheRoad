// my_time.h - wall clock: NTP when the network is up, manual setting otherwise, NVS restore.
//
// Sources, best first:
//   NTP       synced via SNTP (3 servers)
//   RTC       read from a battery-backed RTC at boot (my_rtc, set by the app via setFromRtc)
//   MANUAL    set by hand on the device or from the web page (browser time)
//   RESTORED  last time saved in NVS (every 10 min + on manual set). Always too early after a
//             power cut, so it is "uncertain" until NTP or a manual set.
//   NONE      nothing known
// NTP, RTC and manual sets anchor the log clock (MyLog::setClockAnchor).
#pragma once

#include <stdint.h>
#include <time.h>

namespace MyTime {

enum class Source : uint8_t { NONE = 0, RESTORED, MANUAL, NTP, RTC };

constexpr time_t MIN_EPOCH = 1704067200;   // 2024-01-01 00:00 UTC
constexpr time_t MAX_EPOCH = 4102444800;   // 2100-01-01 00:00 UTC

// nvsNamespace/nvsKey: where the last known time is kept (u64 epoch). tz: POSIX TZ string.
void begin(const char *nvsNamespace, const char *nvsKey, const char *tz);
void loop();                     // Periodic NVS save; call every few hundred ms
void onNetworkUp();              // Starts / refreshes SNTP
void setTz(const char *tz);

bool valid();                    // Time is set (any source except NONE)
bool certain();                  // NTP, RTC or MANUAL
Source source();
const char *sourceText(Source s);   // Swedish UI text
time_t now();                    // 0 if not valid
bool setManual(time_t epoch);    // UTC epoch; false if out of range
bool setFromRtc(time_t epoch);   // Boot time from a valid RTC (system clock already set or not)
// True once after each NTP sync or manual set (the SNTP callback runs in its own task; poll this
// from the main loop to e.g. write the RTC).
bool takeClockChanged();
bool local(struct tm &out);      // Local broken-down time; false if not valid
uint32_t lastNtpSyncS();         // Seconds since the last NTP sync (UINT32_MAX = never)
uint32_t ntpSyncs();             // Successful syncs since boot

}  // namespace MyTime
