// adv_catalog.h - Scans the SD card for adventures and builds a small, menu-ready catalog.
//
// An adventure is a subfolder of /sdcard/otr/adventure/ that contains one or more ordered map
// files named "NN_name.adv" (NN = a two-digit order, 01, 02, ...). Reserved folders (saves, maps)
// and folders with no map files are skipped. Each catalog entry carries the folder name, the
// ordered list of its map file names, and the @about/@meta header of the FIRST map (title,
// description, author) for the selection menu.
//
// Everything is fixed-size so the catalog can live in PSRAM without dynamic allocation. Scanning
// uses POSIX dirent (opendir/readdir), which works both on the ESP-IDF FATFS VFS and on the host
// (the host tests scan sdcard_bundle/otr/adventure as a fixture).
#pragma once

#include <stddef.h>

#include "adv_loader.h"

namespace Adv {

constexpr int MAX_ADVENTURES = 24;
constexpr int MAX_MAPS_PER_ADV = 16;
constexpr int MAP_NAME_CAP = 48;    // "01_some_long_map_name.adv"
constexpr int FOLDER_CAP = 40;

struct CatalogEntry {
    char folder[FOLDER_CAP];                         // e.g. "josef"
    char maps[MAX_MAPS_PER_ADV][MAP_NAME_CAP];       // ordered map file names
    int mapCount;
    Header header;                                   // @about/@meta of the first map
    bool headerOk;                                   // false: header unreadable, menu shows folder name
};

struct Catalog {
    CatalogEntry items[MAX_ADVENTURES];
    int count;
    void clear() { count = 0; }
};

// Scans `root` (e.g. "/sdcard/otr/adventure") and fills `out`. Returns the number of adventures
// found (0 if the folder is missing/empty - the caller shows the "no adventures" message). Folders
// are sorted alphabetically; within a folder, map files are sorted by name (so the NN_ prefix sets
// play order).
int scanCatalog(const char *root, Catalog &out);

// Builds "<root>/<folder>/<mapfile>" for map index `mapIndex` of entry `e`. Returns false if the
// index is out of range or the path would not fit in `cap`.
bool mapPath(char *out, size_t cap, const char *root, const CatalogEntry &e, int mapIndex);

// True if `name` is a valid ordered map file: "NN_*.adv" with NN two digits. (A loose "*.adv" with
// no NN prefix is also accepted and sorts after the numbered ones.)
bool isMapFile(const char *name);

}  // namespace Adv
