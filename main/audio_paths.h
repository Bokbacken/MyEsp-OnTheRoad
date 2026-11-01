// audio_paths.h - Which audio caches a file-manager change can affect. Audio reads only
// /sdcard/otr/sounds (effect clips) and /sdcard/otr/music (background music); other changes must
// not restart the music.
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>

namespace AudioPaths {
enum : uint8_t { NONE = 0, SOUNDS = 1, MUSIC = 2, ALL = SOUNDS | MUSIC };

// True when path is dir, below dir, or an ancestor of dir (FAT names are case-insensitive)
inline bool related(const char *path, const char *dir)
{
    size_t p = strlen(path);
    while (p > 1 && path[p - 1] == '/') p--;
    const size_t d = strlen(dir);
    const size_t n = p < d ? p : d;
    if (strncasecmp(path, dir, n)) return false;
    if (p == d) return true;
    return p > d ? path[d] == '/' : dir[p] == '/';
}

inline uint8_t affected(const char *path)
{
    if (!path) return ALL;
    uint8_t m = NONE;
    if (related(path, "/sdcard/otr/sounds")) m |= SOUNDS;
    if (related(path, "/sdcard/otr/music")) m |= MUSIC;
    return m;
}
}  // namespace AudioPaths
