#pragma once

#include <stdint.h>
#include <stddef.h>

namespace FsUpload {

enum class InstallResult { INSTALLED, SKIPPED, FAILED };

bool siblingPath(const char *destination, uint32_t nonce, bool backup, char *out, size_t cap);
bool requestUriFits(size_t endpointLength, size_t queryLength, size_t limit);
void discard(const char *temporary);
InstallResult install(const char *temporary, const char *destination, const char *backup, bool overwrite,
                      bool skipExisting = false);

}  // namespace FsUpload
