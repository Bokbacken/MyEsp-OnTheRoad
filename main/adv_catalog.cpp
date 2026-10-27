// adv_catalog.cpp - see adv_catalog.h. Firmware-safe: fixed buffers, POSIX dirent, no dynamic
// allocation. The Catalog object is large and should be allocated by the caller in PSRAM.
#include "adv_catalog.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace Adv {

namespace {

bool endsWithAdv(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".adv") == 0;
}

// Case-insensitive compare so sorting is stable regardless of FAT case folding.
int ci_cmp(const char *a, const char *b) { return strcasecmp(a, b); }

bool isReserved(const char *folder)
{
    return strcasecmp(folder, "saves") == 0 || strcasecmp(folder, "maps") == 0;
}

// Inserts `name` into the sorted string array `arr` (count entries, cap max), keeping it sorted
// (case-insensitive). Drops the name if the array is full. Each slot is `stride` bytes wide.
void sortedInsert(char *arr, int &count, int cap, int stride, const char *name)
{
    if (count >= cap) return;
    int pos = count;
    while (pos > 0 && ci_cmp(arr + (pos - 1) * stride, name) > 0) {
        memcpy(arr + pos * stride, arr + (pos - 1) * stride, stride);
        pos--;
    }
    snprintf(arr + pos * stride, stride, "%s", name);
    count++;
}

}  // namespace

bool isMapFile(const char *name)
{
    return name && name[0] != '.' && endsWithAdv(name);
}

bool mapPath(char *out, size_t cap, const char *root, const CatalogEntry &e, int mapIndex)
{
    if (mapIndex < 0 || mapIndex >= e.mapCount) return false;
    int n = snprintf(out, cap, "%s/%s/%s", root, e.folder, e.maps[mapIndex]);
    return n > 0 && (size_t)n < cap;
}

int scanCatalog(const char *root, Catalog &out)
{
    out.clear();
    DIR *d = opendir(root);
    if (!d) return 0;

    // Collect folder names first (sorted), so the menu order is stable.
    static char folders[MAX_ADVENTURES][FOLDER_CAP];   // static: too big for the stack, scan is serial
    int folderCount = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;   // "." ".." and hidden
        if (isReserved(name)) continue;
        // Confirm it is a directory (d_type is unreliable on some FS; stat to be sure).
        char path[160];
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", root, name) >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        sortedInsert(&folders[0][0], folderCount, MAX_ADVENTURES, FOLDER_CAP, name);
    }
    closedir(d);

    for (int i = 0; i < folderCount && out.count < MAX_ADVENTURES; i++) {
        CatalogEntry &e = out.items[out.count];
        snprintf(e.folder, sizeof(e.folder), "%.*s", FOLDER_CAP - 1, folders[i]);
        e.mapCount = 0;
        e.headerOk = false;

        char folderPath[160];
        if ((size_t)snprintf(folderPath, sizeof(folderPath), "%s/%s", root, folders[i]) >= sizeof(folderPath))
            continue;
        DIR *fd = opendir(folderPath);
        if (!fd) continue;
        struct dirent *fe;
        while ((fe = readdir(fd)) != nullptr) {
            if (!isMapFile(fe->d_name)) continue;
            sortedInsert(&e.maps[0][0], e.mapCount, MAX_MAPS_PER_ADV, MAP_NAME_CAP, fe->d_name);
        }
        closedir(fd);
        if (e.mapCount == 0) continue;   // not an adventure

        // Read the first map's header for the menu.
        char mp[256];
        char err[96];
        if (mapPath(mp, sizeof(mp), root, e, 0))
            e.headerOk = parseHeaderFile(mp, e.header, err, sizeof(err));
        out.count++;
    }
    return out.count;
}

}  // namespace Adv
