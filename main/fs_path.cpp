// fs_path.cpp - see fs_path.h.
#include "fs_path.h"

#include <stdint.h>
#include <string.h>

namespace FsPath {

static bool badChar(unsigned char c)
{
    return c < 0x20 || c == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
           c == '<' || c == '>' || c == '|';
}

static bool validComponent(const char *name, size_t n)
{
    if (!name || n == 0) return false;
    size_t units = 0;
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)name[i];
        if (badChar(c) || c == '/') return false;
        uint32_t cp;
        size_t bytes;
        if (c < 0x80) {
            cp = c;
            bytes = 1;
        } else if (c >= 0xC2 && c <= 0xDF) {
            cp = c & 0x1F;
            bytes = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            cp = c & 0x0F;
            bytes = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            cp = c & 0x07;
            bytes = 4;
        } else {
            return false;
        }
        if (i + bytes > n) return false;
        for (size_t j = 1; j < bytes; j++) {
            const unsigned char continuation = (unsigned char)name[i + j];
            if ((continuation & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (continuation & 0x3F);
        }
        if ((bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) ||
            (bytes == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F))
            return false;
        units += cp > 0xFFFF ? 2 : 1;
        if (units > 255) return false;
        i += bytes;
    }
    return name[n - 1] != '.' && name[n - 1] != ' ';
}

bool validName(const char *name)
{
    if (!name || !*name) return false;
    size_t n = strlen(name);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    return validComponent(name, n);
}

bool resolve(const char *root, const char *user, char *out, size_t cap)
{
    if (!root || !out || cap == 0) return false;
    size_t rl = strlen(root);
    if (rl == 0 || root[0] != '/' || rl + 1 > cap) return false;
    if (!user) user = "";
    // Strip a leading root prefix ("/sdcard" or "/sdcard/...")
    if (strncmp(user, root, rl) == 0 && (user[rl] == 0 || user[rl] == '/')) user += rl;

    memcpy(out, root, rl);
    size_t o = rl;
    const char *p = user;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - s);
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') return false;
        if (!validComponent(s, len)) return false;
        if (o + 1 + len + 1 > cap) return false;
        out[o++] = '/';
        memcpy(out + o, s, len);
        o += len;
    }
    out[o] = 0;
    return true;
}

bool isRoot(const char *root, const char *path)
{
    if (!root || !path) return false;
    size_t rl = strlen(root);
    if (strncmp(root, path, rl) != 0) return false;
    for (const char *p = path + rl; *p; p++)
        if (*p != '/') return false;
    return true;
}

}  // namespace FsPath
