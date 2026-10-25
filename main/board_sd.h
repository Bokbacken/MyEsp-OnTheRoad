// board_sd.h - microSD card on the 4-bit SDMMC slot (CLK 5, CMD 4, D0 6, D1 7, D2 2, D3 3),
// mounted as FAT at /sdcard. Tries 4-bit first and falls back to 1-bit. A missing card is
// normal: everything that uses the card must cope with mounted() == false. There is no card
// detect pin, so poll() checks the card status now and then and unmounts a removed card;
// it also retries mounting (a newly inserted card appears within ~15 s).
#pragma once

#include <stdint.h>

namespace BoardSd {

constexpr const char *MOUNT = "/sdcard";

struct Info {
    bool mounted;
    uint8_t width;          // 4 or 1 (bus width that worked)
    uint32_t freqKhz;
    uint64_t totalBytes, freeBytes;
    char name[8];           // Card product name
    uint32_t generation;    // Increments on every successful mount
};

bool begin();                // Mount at boot (false = no card / failed, logged)
bool mounted();
Info info();                 // Cheap: totals are cached (refreshed by poll / after mount)
void poll(uint32_t nowMs);   // Call from the main loop (every ~20 ms is fine; works on its own schedule)
bool remount();              // Unmount (if needed) and mount again; used by the web UI / settings
void refreshUsage();         // Re-read free space (after uploads / deletes)

// Recursive mkdir ("/sdcard/otr/adventure/saves"). True if the directory exists afterwards.
bool ensureDir(const char *path);

// Guards against unmounting while a short file operation runs (audio reads, saves).
void lock();
void unlock();
struct Guard {
    Guard() { lock(); }
    ~Guard() { unlock(); }
};

}  // namespace BoardSd
