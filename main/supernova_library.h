// supernova_library.h - the device side of the Supernova song library: loads/saves the
// supernova.idx cache and rescans the song folders in a background task (only folders that
// are new or changed are read again). The model lives in supernova_index.h.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

#include "supernova_index.h"
#include "supernova_playlists.h"
#include "supernova_sync.h"

namespace SupernovaLibrary {

struct Media {
    // Full paths; audio may be empty. audio = full mix (or the vocals file when that is all there is),
    // instrumental = "[Kar]"/"[Inst..]" file (SingStar conversions), both empty when absent.
    std::string audio, video, lyrics, instrumental;
    std::string mp4;   // Original .mp4 when present (video above prefers .avi); only the web player uses it
};

// Finds the media of a song folder (full path). True when video and lyrics exist (.avi is
// preferred over .mp4).
bool findMedia(const std::string &folderPath, Media &out);
std::string folderPath(const char *folderName);

// Lyrics sync corrections / "broken sync" marks (supernova.sync). LVGL task only. The store is
// loaded on first use; saveSync() writes it back (false if the card is missing or the write failed).
SupernovaSync::Store &syncStore();
bool saveSync();

// Playlists (supernova.pl), shared by the web handlers and the device UI. Both calls lock the
// store; fn must not call back into the library. editPlaylists runs fn, and when it returns true
// writes the file (before returning); `saved` tells whether that write worked.
void readPlaylists(const std::function<void(const SupernovaPlaylists::Store &)> &fn);
bool editPlaylists(const std::function<bool(SupernovaPlaylists::Store &)> &fn, bool &saved);

// Loads the cache (first call only) and starts a background scan unless one is running.
// force = read every folder again, otherwise unchanged folders (same mtime) are reused.
void start(bool force);
bool scanning();
void progress(size_t &done, size_t &total);
uint32_t generation();            // Changes whenever the library content was replaced
bool catalogError();              // The song root could not be opened / no card

// Runs fn with the library locked; keep it short (no SD access, no UI calls).
void withLibrary(const std::function<void(const SupernovaIndex::Library &)> &fn);

}  // namespace SupernovaLibrary
