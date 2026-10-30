#include "fs_upload.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace FsUpload {

bool siblingPath(const char *destination, uint32_t nonce, bool backup, char *out, size_t cap)
{
    if (!destination || !out || cap == 0) return false;
    const char *slash = strrchr(destination, '/');
    const size_t directoryLength = slash ? (size_t)(slash - destination) + 1 : 0;
    char name[16];
    const int n = snprintf(name, sizeof(name), ".u%08x.%s", (unsigned)nonce, backup ? "bak" : "part");
    if (n < 0 || (size_t)n >= sizeof(name) || directoryLength + (size_t)n + 1 > cap) return false;
    memcpy(out, destination, directoryLength);
    memcpy(out + directoryLength, name, (size_t)n + 1);
    return true;
}

bool requestUriFits(size_t endpointLength, size_t queryLength, size_t limit)
{
    if (endpointLength >= limit) return false;
    const size_t remaining = limit - endpointLength;
    return queryLength ? queryLength < remaining - 1 : true;
}

void discard(const char *temporary)
{
    if (temporary) unlink(temporary);
}

bool install(const char *temporary, const char *destination, const char *backup, bool overwrite)
{
    if (!temporary || !destination) {
        errno = EINVAL;
        return false;
    }

    struct stat st = {};
    if (stat(destination, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            errno = EISDIR;
            return false;
        }
        if (!overwrite) {
            errno = EEXIST;
            return false;
        }
        if (!backup || !strcmp(backup, destination) || !strcmp(backup, temporary)) {
            errno = EINVAL;
            return false;
        }
        if (stat(backup, &st) == 0) {
            errno = EEXIST;
            return false;
        }
        if (errno != ENOENT) return false;
        if (rename(destination, backup) != 0) return false;
        if (rename(temporary, destination) == 0) {
            unlink(backup);
            return true;
        }
        const int installError = errno ? errno : EIO;
        if (rename(backup, destination) != 0) return false;
        errno = installError;
        return false;
    }
    if (errno != ENOENT) return false;
    return rename(temporary, destination) == 0;
}

}  // namespace FsUpload
