#include "my_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"

static const char *TAG = "storage";

namespace MyNvs {

bool initFlash()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

Handle::Handle(const char *ns, bool readOnly) : rw_(!readOnly)
{
    ok_ = nvs_open(ns, readOnly ? NVS_READONLY : NVS_READWRITE, &h_) == ESP_OK;
}

Handle::~Handle()
{
    if (!ok_) return;
    if (rw_ && dirty_) nvs_commit(h_);
    nvs_close(h_);
}

uint8_t Handle::getU8(const char *key, uint8_t def) const
{
    uint8_t v = def;
    return ok_ && nvs_get_u8(h_, key, &v) == ESP_OK ? v : def;
}
uint16_t Handle::getU16(const char *key, uint16_t def) const
{
    uint16_t v = def;
    return ok_ && nvs_get_u16(h_, key, &v) == ESP_OK ? v : def;
}
uint32_t Handle::getU32(const char *key, uint32_t def) const
{
    uint32_t v = def;
    return ok_ && nvs_get_u32(h_, key, &v) == ESP_OK ? v : def;
}
uint64_t Handle::getU64(const char *key, uint64_t def) const
{
    uint64_t v = def;
    return ok_ && nvs_get_u64(h_, key, &v) == ESP_OK ? v : def;
}

bool Handle::getStr(const char *key, char *out, size_t cap) const
{
    if (!ok_ || !out || !cap) return false;
    size_t len = 0;
    if (nvs_get_str(h_, key, nullptr, &len) != ESP_OK || len == 0) return false;
    if (len > cap) {   // Too long for the caller: read into a temporary and truncate
        char *tmp = (char *)malloc(len);
        if (!tmp) return false;
        bool ok = nvs_get_str(h_, key, tmp, &len) == ESP_OK;
        if (ok) { memcpy(out, tmp, cap - 1); out[cap - 1] = 0; }
        free(tmp);
        return ok;
    }
    return nvs_get_str(h_, key, out, &len) == ESP_OK;
}

size_t Handle::blobSize(const char *key) const
{
    size_t len = 0;
    return ok_ && nvs_get_blob(h_, key, nullptr, &len) == ESP_OK ? len : 0;
}

bool Handle::getBlob(const char *key, void *out, size_t len) const
{
    size_t have = blobSize(key);
    if (have != len) return false;
    return nvs_get_blob(h_, key, out, &have) == ESP_OK;
}

#define MYNVS_SET(expr)                 \
    if (!ok_ || !rw_) return false;     \
    bool ok = (expr) == ESP_OK;         \
    dirty_ |= ok;                       \
    return ok

bool Handle::setU8(const char *key, uint8_t v) { MYNVS_SET(nvs_set_u8(h_, key, v)); }
bool Handle::setU16(const char *key, uint16_t v) { MYNVS_SET(nvs_set_u16(h_, key, v)); }
bool Handle::setU32(const char *key, uint32_t v) { MYNVS_SET(nvs_set_u32(h_, key, v)); }
bool Handle::setU64(const char *key, uint64_t v) { MYNVS_SET(nvs_set_u64(h_, key, v)); }
bool Handle::setStr(const char *key, const char *v) { MYNVS_SET(nvs_set_str(h_, key, v ? v : "")); }
bool Handle::setBlob(const char *key, const void *d, size_t len) { MYNVS_SET(nvs_set_blob(h_, key, d, len)); }

bool Handle::erase(const char *key)
{
    if (!ok_ || !rw_) return false;
    esp_err_t e = nvs_erase_key(h_, key);
    dirty_ |= e == ESP_OK;
    return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
}

bool Handle::eraseAll() { MYNVS_SET(nvs_erase_all(h_)); }

bool Handle::commit()
{
    if (!ok_ || !rw_) return false;
    dirty_ = false;
    return nvs_commit(h_) == ESP_OK;
}

Usage usage()
{
    nvs_stats_t st{};
    Usage u{};
    if (nvs_get_stats(nullptr, &st) == ESP_OK) {
        u.used = st.used_entries;
        u.free = st.free_entries;
        u.total = st.total_entries;
    }
    return u;
}

}  // namespace MyNvs

namespace MyFat {

static bool s_mounted = false;
static char s_base[16] = "";
static wl_handle_t s_wl = WL_INVALID_HANDLE;

bool mount(const char *label, const char *base, bool formatIfFail, int maxFiles)
{
    if (s_mounted) return true;
    esp_vfs_fat_mount_config_t cfg = {};
    cfg.format_if_mount_failed = formatIfFail;
    cfg.max_files = maxFiles;
    cfg.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(base, label, &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAT mount '%s' failed: %s", label, esp_err_to_name(err));
        return false;
    }
    snprintf(s_base, sizeof(s_base), "%s", base);
    s_mounted = true;
    ESP_LOGI(TAG, "FAT '%s' mounted at %s, %llu/%llu kB used", label, base, usedBytes() >> 10, totalBytes() >> 10);
    return true;
}

bool mounted() { return s_mounted; }
const char *basePath() { return s_base; }

static bool info(uint64_t &total, uint64_t &freeB)
{
    return s_mounted && esp_vfs_fat_info(s_base, &total, &freeB) == ESP_OK;
}

uint64_t totalBytes()
{
    uint64_t t = 0, f = 0;
    return info(t, f) ? t : 0;
}

uint64_t usedBytes()
{
    uint64_t t = 0, f = 0;
    return info(t, f) ? t - f : 0;
}

void path(char *out, size_t cap, const char *rel) { snprintf(out, cap, "%s%s", s_base, rel ? rel : ""); }

bool exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

bool writeAtomic(const char *p, const void *head, size_t headLen, const void *body, size_t bodyLen)
{
    if (!s_mounted) return false;
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = (!headLen || fwrite(head, 1, headLen, f) == headLen) && (!bodyLen || fwrite(body, 1, bodyLen, f) == bodyLen);
    ok = fflush(f) == 0 && ok;
    fsync(fileno(f));
    fclose(f);
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(p);
    if (rename(tmp, p) != 0) {
        ESP_LOGE(TAG, "rename %s failed", tmp);
        return false;
    }
    return true;
}

}  // namespace MyFat
