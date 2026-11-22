// supernova_library.h - the device side of the Supernova song library: loads/saves the
// supernova.idx cache and rescans the song folders in a background task (only folders that
// are new or changed are read again). The model lives in supernova_index.h.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

#include "supernova_index.h"

namespace SupernovaLibrary {

struct Media {
    // Full paths; audio may be empty. audio = full mix (or the vocals file when that is all there is),
    // instrumental = "[Kar]"/"[Inst..]" file (SingStar conversions), both empty when absent.
    std::string audio, video, lyrics, instrumental;
};

// Finds the media of a song folder (full path). True when video and lyrics exist (.avi is
// preferred over .mp4).
bool findMedia(const std::string &folderPath, Media &out);
std::string folderPath(const char *folderName);

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
