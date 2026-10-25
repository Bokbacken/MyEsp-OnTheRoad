// files_page.h - v0.13.7 pure helpers for the paginated file manager API (host-tested in
// tests/test_filespage.cpp): sorting (folders always first), page maths and path relations.
#pragma once

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <vector>

namespace FilesPage {

// Names live in one shared pool (PSRAM on the device): name = offset of a 0-terminated string.
struct Item {
    uint32_t name = 0;
    bool dir = false;
    uint64_t size = 0;
    int64_t mtime = 0;
};

enum class Sort { NAME, SIZE, MTIME };

inline Sort parseSort(const char *s)
{
    if (s && !strcmp(s, "size")) return Sort::SIZE;
    if (s && (!strcmp(s, "mtime") || !strcmp(s, "date"))) return Sort::MTIME;
    return Sort::NAME;
}

// Folders first (always by name ascending among themselves unless sorting by name descending),
// then files by key; ties by name (case-insensitive), so the order is stable between pages.
template <class V> void sortItems(V &v, const char *pool, Sort key, bool desc)
{
    std::sort(v.begin(), v.end(), [pool, key, desc](const Item &a, const Item &b) {
        if (a.dir != b.dir) return a.dir;
        const char *an = pool + a.name, *bn = pool + b.name;
        const int byName = strcasecmp(an, bn);
        int c = 0;
        if (key == Sort::SIZE && !a.dir) c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
        else if (key == Sort::MTIME) c = a.mtime < b.mtime ? -1 : a.mtime > b.mtime ? 1 : 0;
        if (c == 0) c = byName ? byName : strcmp(an, bn);
        return desc ? c > 0 : c < 0;
    });
}

struct Page {
    int page;    // 1-based, clamped into [1, pages]
    int pages;   // >= 1
    int first;   // Index of the first item on the page
    int count;   // Items on the page
};

inline Page pageOf(int total, int page, int limit)
{
    if (limit < 1) limit = 1;
    if (total < 0) total = 0;
    Page p;
    p.pages = total == 0 ? 1 : (total + limit - 1) / limit;
    p.page = page < 1 ? 1 : page > p.pages ? p.pages : page;
    p.first = (p.page - 1) * limit;
    p.count = total - p.first < limit ? total - p.first : limit;
    if (p.count < 0) p.count = 0;
    return p;
}

// True when path equals dir or lies below it ("/a/b" is inside "/a", "/ab" is not).
inline bool isInside(const char *path, const char *dir)
{
    const size_t n = strlen(dir);
    if (strncasecmp(path, dir, n) != 0) return false;   // FAT is case-insensitive
    return path[n] == 0 || path[n] == '/' || (n > 0 && dir[n - 1] == '/');
}

inline const char *baseName(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

}  // namespace FilesPage
