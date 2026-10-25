// fs_path.cpp - see fs_path.h.
#include "fs_path.h"

#include <string.h>

namespace FsPath {

static bool badChar(unsigned char c)
{
    return c < 0x20 || c == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
           c == '<' || c == '>' || c == '|';
}

bool validName(const char *name)
{
    if (!name || !*name) return false;
    size_t n = strlen(name);
    if (n > 255) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (size_t i = 0; i < n; i++)
        if (badChar((unsigned char)name[i]) || name[i] == '/') return false;
    // FAT silently drops trailing dots/spaces - refuse them to avoid surprises
    if (name[n - 1] == '.' || name[n - 1] == ' ') return false;
    return true;
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
        if (len > 255) return false;
        for (size_t i = 0; i < len; i++)
            if (badChar((unsigned char)s[i])) return false;
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
