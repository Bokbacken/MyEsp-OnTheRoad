// board_clock.h - wall clock: NTP when Wi-Fi is up, manual setting otherwise.
//
// The board has no RTC battery, so after power-up the time is unknown. Sources, best first:
//   NTP       synced from pool.ntp.org / time.google.com (needs Wi-Fi with internet)
//   MANUAL    set by hand on the device or from the web page (browser time)
//   RESTORED  last time saved in NVS (every 10 min + on manual set). Always too early after a
//             power cut, so it is flagged "osäker" (uncertain) until NTP or a manual set.
//   NONE      nothing known (shows --:--)
// Game history uses the time with its source, so results stored with an uncertain time can be
// marked in the statistics.
#pragma once

#include <stdint.h>
#include <time.h>

namespace Clock {

enum class Source : uint8_t { NONE = 0, RESTORED, MANUAL, NTP };

void begin();                    // Applies Config tz, restores the NVS time if needed
void loop();                     // Periodic NVS save; call from loop()
void onWifiConnected();          // Starts/refreshes SNTP
void applyTz();                  // Re-applies Config::get().tz after a change

bool valid();                    // Time is set (any source except NONE)
bool certain();                  // NTP or MANUAL
Source source();
const char *sourceText(Source s);   // Swedish
time_t now();                    // 0 if not valid
bool setManual(time_t epoch);    // UTC epoch; false if out of range (2024..2099)
// Local broken-down time; false if not valid.
bool local(struct tm &out);
uint32_t lastNtpSyncS();         // Seconds since the last NTP sync (UINT32_MAX = never)

}  // namespace Clock
