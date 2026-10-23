// my_log.h - time-stamped log capture + persistent event log (shared MyEsp/MyPi component).
//
// - begin() hooks esp_log (esp_log_set_vprintf): every ESP_LOGx line still goes to the console
//   and is also copied into a RAM ring (PSRAM when available) with its uptime; shown as real
//   time once the clock is anchored (setClockAnchor), otherwise as uptime.
// - event() is for things worth keeping across reboots (boot, reset reason, OTA, Wi-Fi changes,
//   settings changes): logged like ESP_LOGI and appended to a text file on a mounted filesystem
//   (setEventFile), rotated to "<file>.1" at maxBytes.
// - read()/readEvents() feed the diagnostics web page (/api/log).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace MyLog {

constexpr time_t MIN_VALID_EPOCH = 1704067200;   // 2024-01-01: earlier = clock not set

struct Config {
    int entries = 400;         // RAM ring lines (about 160 bytes each)
    bool echoConsole = true;   // Keep printing to the original console
};

bool begin(const Config &cfg = Config());
// Persistent events: full path on a mounted VFS ("/ffat/events.log"). nullptr disables.
void setEventFile(const char *path, size_t maxBytes = 64 * 1024);
void event(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Formats "2026-10-23 14:05:09" (local time) when the clock is valid, else "+123.456s" (uptime).
void formatTime(char *out, size_t cap, time_t epoch, int64_t uptimeMs);
// Formats a log timestamp from uptime: real local time once the clock is anchored
// (setClockAnchor), else "+123.456s". Lines captured before the anchor get real time too.
void formatRef(char *out, size_t cap, int64_t uptimeMs);
// Call when the clock became trustworthy (RTC at boot, NTP, manual set). Pins
// "wall time = anchor + uptime" and flushes events queued while the clock was unknown.
void setClockAnchor();
bool clockAnchored();

// Copies RAM log lines with seq >= fromSeq as text ("<time> <L> <text>\n") into out.
// Returns bytes written (NUL-terminated); *nextSeq = seq to pass next time.
size_t read(char *out, size_t cap, uint32_t fromSeq, uint32_t *nextSeq);
// Copies the tail (last cap-1 bytes, whole lines) of the persistent event file.
size_t readEvents(char *out, size_t cap);

struct Stats {
    uint32_t lines;      // Lines captured since boot
    uint32_t dropped;    // Lines lost (ring busy / overwritten before read is not counted)
    uint32_t events;     // event() calls since boot
    uint32_t eventWriteErrors;
    int capacity;
    bool inPsram;
};
Stats stats();

}  // namespace MyLog
