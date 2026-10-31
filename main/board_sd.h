// board_sd.h - microSD card on the board-selected 4-bit SDMMC slot,
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
    uint32_t generation;    // Changes after mount/remount and formatting
    int32_t lastError;      // ESP error from the last failed mount/status/unmount, 0 after success
    uint32_t errors;
};

bool begin();                // Mount at boot (false = no card / failed, logged)
bool mounted();
bool transferHealthy();      // Mounted and no recovery requested
void requestRecovery();      // Abort transfers; remount only after the last transfer closes
bool beginTransfer();
void endTransfer();
struct Transfer {
    bool active;
    explicit Transfer(bool enabled = true) : active(enabled && beginTransfer()) {}
    Transfer(const Transfer &) = delete;
    Transfer &operator=(const Transfer &) = delete;
    void finish() { if (active) { active = false; endTransfer(); } }
    ~Transfer() { finish(); }
};
bool traceSupported();
void setTrace(bool enabled); // Temporary, expires after ten minutes; off after restart
bool traceEnabled();
uint32_t traceRemainingSeconds();
Info info();                 // Cheap: totals are cached (refreshed by poll / after mount)
void poll(uint32_t nowMs);   // Call from the main loop (every ~20 ms is fine; works on its own schedule)
bool remount();              // Unmount (if needed) and mount again; used by the web UI / settings
bool format();               // Explicitly format as FAT; may mount an unsupported filesystem first
void refreshUsage();         // Re-read free space (after uploads / deletes)
uint32_t clusterBytes();     // v0.13.7: FAT cluster size (0 = not mounted / unknown)

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
