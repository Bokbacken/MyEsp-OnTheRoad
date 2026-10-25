// otr_log.h - v0.13.7 log capture for diagnostics.
//
// - Chains onto esp_log_set_vprintf (after MyLog::begin(), which keeps its own line ring and the
//   console echo) and copies every log line (ANSI colours stripped) into a 64 KB byte ring.
// - A background task appends new ring bytes to /sdcard/otr/logs/otr_0.log every 2 s when a card
//   is mounted; at 256 KB the files rotate otr_0 -> otr_1 -> otr_2 (oldest dropped).
// - Web:
//     GET /api/log/stream    Server-Sent Events, one "data:" event per log line (max 2 clients)
//     GET /api/log/download  ZIP (store) with ram.log, info.txt and the SD log files
#pragma once

#include <stddef.h>
#include <stdint.h>

// Installs the hook and starts the SD writer. Call once, right after MyLog::begin().
bool otr_log_init();
// Registers the two web routes (needs a started MyWeb server).
bool otr_log_register_routes();
// Total bytes captured since boot (for status displays)
uint64_t otr_log_bytes();
