// board_ota.h - firmware update over Wi-Fi (web upload of the app .bin into the other OTA slot).
//
// The partition scheme app3M_fat9M_16MB has two 3 MB app slots (ota_0 / ota_1). The web server
// streams the uploaded file into the inactive slot via Update.h; after a verified end() the
// boot partition is switched and the device restarts. A failed or aborted upload leaves the
// running firmware untouched.
//
// Threading: begin/write/end/abort are called from the async_tcp task (one upload at a time);
// state() may be read from any task (the LVGL overlay polls it).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Ota {

enum class Phase : uint8_t { IDLE = 0, RUNNING, DONE, FAILED };

struct State {
    Phase phase;
    uint32_t written;    // Bytes written so far
    uint32_t total;      // Expected size (0 = unknown)
    char error[64];      // Swedish, set when phase == FAILED
};

bool begin(uint32_t total);   // total may be 0 (unknown). False if busy or no OTA slot.
bool write(const uint8_t *data, size_t len);
bool end();                   // Verifies the image and selects it for the next boot
void abort(const char *why);
State state();
bool busy();                  // RUNNING
void checkStale();            // Aborts a RUNNING update that received no data for 20 s
const char *runningPartition();   // "app0" / "app1" label of the running slot
const char *nextPartition();      // Slot the next update will be written to
uint32_t slotSize();              // Size of the update slot in bytes

}  // namespace Ota
