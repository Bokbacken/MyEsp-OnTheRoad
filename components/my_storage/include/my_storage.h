// my_storage.h - NVS helpers + FAT (wear levelled) mount + atomic file writes.
//
// MyNvs keeps the Arduino "Preferences" encoding (strings as NVS str, bool as u8, bytes as blob),
// so settings written by an Arduino build are read unchanged after moving to ESP-IDF.
// MyFat mounts a "fat" data partition with wear levelling, exactly like Arduino FFat.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nvs.h"

namespace MyNvs {

bool initFlash();   // nvs_flash_init(), erases + retries on a version/full error

// RAII namespace handle: `MyNvs::Handle h("otr", true); if (h) h.getU8("bl", 80);`
class Handle {
public:
    Handle(const char *ns, bool readOnly);
    ~Handle();
    explicit operator bool() const { return ok_; }
    uint8_t getU8(const char *key, uint8_t def) const;
    uint16_t getU16(const char *key, uint16_t def) const;
    uint32_t getU32(const char *key, uint32_t def) const;
    uint64_t getU64(const char *key, uint64_t def) const;
    bool getBool(const char *key, bool def) const { return getU8(key, def ? 1 : 0) != 0; }
    bool getStr(const char *key, char *out, size_t cap) const;   // false = missing (out untouched)
    size_t blobSize(const char *key) const;                      // 0 = missing
    bool getBlob(const char *key, void *out, size_t len) const;  // exact length required
    bool setU8(const char *key, uint8_t v);
    bool setU16(const char *key, uint16_t v);
    bool setU32(const char *key, uint32_t v);
    bool setU64(const char *key, uint64_t v);
    bool setBool(const char *key, bool v) { return setU8(key, v ? 1 : 0); }
    bool setStr(const char *key, const char *v);
    bool setBlob(const char *key, const void *data, size_t len);
    bool erase(const char *key);
    bool eraseAll();
    bool commit();   // Also done by the destructor of a writable handle
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

private:
    nvs_handle_t h_ = 0;
    bool ok_ = false;
    bool rw_ = false;
    bool dirty_ = false;
};

struct Usage { size_t used, free, total; };
Usage usage();

}  // namespace MyNvs

namespace MyFat {

// Mounts partition `label` at `basePath` (e.g. "ffat" at "/ffat"). formatIfFail formats on the
// first boot (takes a few seconds for 9 MB).
bool mount(const char *label, const char *basePath, bool formatIfFail, int maxFiles = 8);
bool mounted();
const char *basePath();
uint64_t totalBytes();
uint64_t usedBytes();
// Builds "<basePath><rel>" ("/players.bin" -> "/ffat/players.bin").
void path(char *out, size_t cap, const char *rel);
bool exists(const char *fullPath);
// Writes header + body to "<fullPath>.tmp", then renames over fullPath (power-cut safe).
bool writeAtomic(const char *fullPath, const void *head, size_t headLen, const void *body, size_t bodyLen);

}  // namespace MyFat
