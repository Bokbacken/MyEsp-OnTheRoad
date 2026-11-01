// my_ota.h - firmware update into the inactive OTA slot + rollback handling (shared component).
//
// - begin/write/end stream an app image (.bin, first byte 0xE9) into the next OTA partition;
//   end() verifies it and selects it for the next boot. A failed upload leaves the running
//   firmware untouched.
// - With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE a new image boots in "pending verify" state:
//   call markValid() once the application is healthy (boot checks passed). If the image
//   crashes before that, the bootloader returns to the previous one.
//
// Threading: one upload at a time (web server task); state() may be read from any task.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MyOta {

enum class Phase : uint8_t { IDLE = 0, RUNNING, DONE, FAILED };

struct State {
    Phase phase;
    uint32_t written;    // Bytes written so far
    uint32_t total;      // Expected size (0 = unknown)
    char error[64];      // Swedish, set when phase == FAILED
};

bool begin(uint32_t total);   // total may be 0 (unknown)
bool write(const uint8_t *data, size_t len);
bool end();
void abort(const char *why);
State state();
bool busy();                  // RUNNING
void checkStale();            // Aborts a RUNNING update that received no data for 20 s

// Lets another updater (Host radio OTA) drive the same progress state/overlay.
// beginExternal() fails if an app update is already RUNNING; it also blocks begin() meanwhile.
bool beginExternal(uint32_t total);
void progressExternal(uint32_t written);
void finishExternal(bool ok, const char *err = nullptr);
const char *runningPartition();
const char *nextPartition();
uint32_t slotSize();

void cacheBootState();        // Reads image state/rollback once (boot); later queries use the cache
bool pendingVerify();         // Running image waits for markValid()
void markValid();             // Cancels the rollback (logs an event the first time)
bool rolledBack();            // The previous image was invalid / rolled back at this boot

}  // namespace MyOta
