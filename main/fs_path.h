// fs_path.h - safe path handling for the web file browser (pure C, host-tested by
// tests/test_fspath.cpp). Every path the browser touches must come out of FsPath::resolve().
#pragma once

#include <stddef.h>

namespace FsPath {

// Builds the absolute path for a user-supplied path inside `root` ("/sdcard" or "/ffat").
// `user` may be relative ("otr/a.wav"), absolute inside the root ("/otr/a.wav") or carry the
// root prefix ("/sdcard/otr/a.wav"); "" or "/" is the root itself. Repeated '/' and "." are
// collapsed. Rejects "..", backslashes, control characters, FAT-invalid characters (:*?"<>|),
// components longer than 255 bytes and results that do not fit in `cap`.
bool resolve(const char *root, const char *user, char *out, size_t cap);

// True if `name` is a valid single file/directory name (for rename / mkdir).
bool validName(const char *name);

// True if `path` equals `root` (so it must never be deleted or renamed).
bool isRoot(const char *root, const char *path);

}  // namespace FsPath
