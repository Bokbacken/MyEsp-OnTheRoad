// upload_admission.h - Memory-aware SD upload concurrency. Each upload task needs an internal-RAM
// stack plus socket buffers; S3 has little internal RAM left after Wi-Fi/LVGL/karaoke, and running
// it dry makes unrelated code fail (e.g. flash mmap, HTTP parsing). New uploads are admitted only
// while the floor is kept; the browser retries on 429.
#pragma once
#include <stddef.h>

namespace UploadAdmission {
constexpr int MAX_UPLOADS = 4;
constexpr size_t INTERNAL_FLOOR = 40 * 1024;   // Left for Wi-Fi/lwIP, flash mmap, LVGL and audio
constexpr size_t PER_UPLOAD = 12 * 1024;        // Upload task stack + socket buffers

// active: uploads already running; internalFree: current free internal heap
inline bool admit(int active, size_t internalFree)
{
    if (active >= MAX_UPLOADS) return false;
    return active == 0 || internalFree >= INTERNAL_FLOOR + PER_UPLOAD;
}

// The DMA-capable internal write cache is used only when the floor survives it; else PSRAM.
inline bool internalCache(size_t internalFree, size_t cacheBytes) { return internalFree >= INTERNAL_FLOOR + cacheBytes; }

// Parallel uploads the browser should start, given free internal heap with no upload running
inline int suggestedSlots(size_t internalFree)
{
    if (internalFree <= INTERNAL_FLOOR + PER_UPLOAD) return 1;
    const size_t n = (internalFree - INTERNAL_FLOOR) / PER_UPLOAD;
    return n >= (size_t)MAX_UPLOADS ? MAX_UPLOADS : (int)n;
}
}  // namespace UploadAdmission
