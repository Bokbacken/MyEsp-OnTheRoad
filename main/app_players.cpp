#include "app_players.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "my_storage.h"
#include "my_time.h"

static const char *TAG = "players";

namespace Players {

static constexpr uint32_t MAGIC = 0x50525450;   // "PTRP"
static constexpr uint16_t VERSION = 2;   // v2 (v0.5.6): PlayerRec with PIN; v1 is migrated

struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t recSize;
    uint32_t dbSize;
};

static SemaphoreHandle_t s_lock = nullptr;
static PlayerDb *s_db = nullptr;   // PSRAM (~6.4 kB)
static bool s_fsOk = false;
static char s_path[48];

struct Lock {
    Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_lock); }
};

static bool s_loaded = false;
static std::unique_ptr<PlayerDb> copyDb() {
    return std::unique_ptr<PlayerDb>(new (std::nothrow) PlayerDb);
}

// Do not use the generic remove-before-rename helper for the profile registry.
// Keep the previous committed file until the replacement is completely written.
static bool saveLocked(const PlayerDb &db)
{
    if (!s_fsOk || !MyFat::mounted()) return false;
    char tmp[64], backup[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_path);
    snprintf(backup, sizeof(backup), "%s.bak", s_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { ESP_LOGE(TAG, "profile temp open failed errno=%d", errno); return false; }
    Header h = {MAGIC, VERSION, (uint16_t)sizeof(PlayerRec), (uint32_t)sizeof(PlayerDb)};
    bool good = fwrite(&h, 1, sizeof(h), f) == sizeof(h) && fwrite(&db, 1, sizeof(db), f) == sizeof(db);
    if (fflush(f) != 0) good = false;
    if (fsync(fileno(f)) != 0) good = false;
    if (fclose(f) != 0) good = false;
    if (!good) { ::remove(tmp); ESP_LOGE(TAG, "profile write/flush/close failed"); return false; }
    struct stat st{};
    bool hadPrimary = stat(s_path, &st) == 0;
    if (!hadPrimary && errno != ENOENT) { ::remove(tmp); return false; }
    if (hadPrimary) {
        if (::remove(backup) != 0 && errno != ENOENT) { ::remove(tmp); return false; }
        if (::rename(s_path, backup) != 0) { ::remove(tmp); return false; }
    }
    if (::rename(tmp, s_path) != 0) {
        const int reason = errno;
        if (hadPrimary && ::rename(backup, s_path) != 0) {
            s_fsOk = false; // Preserve .bak; further writes must not destroy it.
            ESP_LOGE(TAG, "profile rollback failed; backup retained, writes blocked");
        }
        ::remove(tmp);
        ESP_LOGE(TAG, "profile commit failed errno=%d", reason);
        return false;
    }
    ESP_LOGI(TAG, "profile commit count=%u", db.count);
    return true;
}

// Returns 2/current, 1/v1, 0/unreadable. The active registry is never changed here.
static int loadFile(const char *path, PlayerDb &out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    Header h = {};
    int res = 0;
    if (fread(&h, 1, sizeof(h), f) == sizeof(h) && h.magic == MAGIC) {
        if (h.version == VERSION && h.recSize == sizeof(PlayerRec) && h.dbSize == sizeof(PlayerDb)) {
            if (fread(&out, 1, sizeof(out), f) == sizeof(out) && out.count <= PLAYER_MAX) res = 2;
        } else if (h.version == 1 && h.recSize == sizeof(PlayerRecV1) && h.dbSize == sizeof(PlayerDbV1)) {
            auto old = std::unique_ptr<PlayerDbV1>(new (std::nothrow) PlayerDbV1);
            if (old && fread(old.get(), 1, sizeof(*old), f) == sizeof(*old) && out.loadV1(*old)) res = 1;
        }
    }
    if (ferror(f)) res = 0;
    if (fclose(f) != 0) res = 0;
    if (res) out.sanitize();
    return res;
}

bool begin()
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    if (!s_db) {
        s_db = (PlayerDb *)heap_caps_malloc(sizeof(PlayerDb), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_db) s_db = (PlayerDb *)malloc(sizeof(PlayerDb));
        if (!s_db) return false;
        s_db->clear();
    }
    Lock l;
    s_fsOk = false;
    if (!MyFat::mounted()) {
        ESP_LOGE(TAG, "filesystem not mounted; registry retained, writes blocked");
        return false;
    }
    MyFat::path(s_path, sizeof(s_path), "/players.bin");
    char backup[64], tmpNew[64], tmpOld[48];
    snprintf(backup, sizeof(backup), "%s.bak", s_path);
    snprintf(tmpNew, sizeof(tmpNew), "%s.tmp", s_path);
    MyFat::path(tmpOld, sizeof(tmpOld), "/players.tmp");
    const char *candidates[] = {s_path, backup, tmpNew, tmpOld};
    auto loaded = copyDb();
    if (!loaded) return false;
    bool readError = false;
    for (int i = 0; i < 4; ++i) {
        struct stat st{};
        if (stat(candidates[i], &st) != 0) {
            if (errno != ENOENT) { readError = true; ESP_LOGE(TAG, "profile stat slot=%d errno=%d", i, errno); }
            continue;
        }
        int version = loadFile(candidates[i], *loaded);
        if (version) {
            if (i == 0 || !s_loaded) *s_db = *loaded;
            s_loaded = true;
            // Recovery is visible, but no automatic rewrite of damaged files.
            s_fsOk = i == 0 && !readError;
            ESP_LOGI(TAG, "profile load slot=%d version=%d count=%u writable=%d", i, version, s_db->count, s_fsOk);
            return s_fsOk;
        }
        readError = true;
        ESP_LOGE(TAG, "profile unreadable slot=%d; no overwrite", i);
    }
    if (readError || s_loaded) {
        ESP_LOGE(TAG, "profile load failed; registry retained, writes blocked");
        return false;
    }
    // Only a genuinely missing registry on a mounted filesystem is a new empty DB.
    s_fsOk = s_loaded = true;
    ESP_LOGI(TAG, "profile registry absent; new empty registry");
    return true;
}

bool ok() { return s_fsOk; }

void snapshot(PlayerDb &out)
{
    if (!s_db) {
        out.clear();
        return;
    }
    Lock l;
    out = *s_db;
}

int count()
{
    if (!s_db) return 0;
    Lock l;
    return s_db->count;
}

// Apply changes to a private candidate; publish only after a successful durable write.
template<class Mutate> static int change(Mutate mutate) {
    if (!s_db) return (int)PlayerErr::STORAGE;
    Lock l;
    if (!s_fsOk || !MyFat::mounted()) return (int)PlayerErr::STORAGE;
    auto candidate = copyDb();
    if (!candidate) return (int)PlayerErr::STORAGE;
    *candidate = *s_db;
    const int result = mutate(*candidate);
    if (result < 0) return result;
    if (!saveLocked(*candidate)) {
        ESP_LOGE(TAG, "profile mutation not committed; memory retained");
        return (int)PlayerErr::STORAGE;
    }
    *s_db = *candidate;
    return result;
}
int add(const char *name, const char *pin) {
    return change([&](PlayerDb &db) { return db.add(name, (uint32_t)MyTime::now(), pin); });
}
int rename(uint16_t id, const char *name) {
    return change([&](PlayerDb &db) { return (int)db.rename(id, name); });
}
int remove(uint16_t id) {
    return change([&](PlayerDb &db) { return (int)db.remove(id); });
}
int setPin(uint16_t id, const char *pin) {
    return change([&](PlayerDb &db) { return (int)db.setPin(id, pin); });
}

bool hasPin(uint16_t id)
{
    if (!s_db) return false;
    Lock l;
    return s_db->hasPin(id);
}

bool checkPin(uint16_t id, const char *pin)
{
    if (!s_db) return false;
    Lock l;
    return s_db->checkPin(id, pin);
}

bool name(uint16_t id, char *out, size_t cap)
{
    if (!s_db || !out || !cap) return false;
    Lock l;
    const PlayerRec *r = s_db->get(id);
    if (!r) return false;
    snprintf(out, cap, "%s", r->name);
    return true;
}

int findByName(const char *n)
{
    if (!s_db) return 0;
    Lock l;
    int i = s_db->indexOfName(n);
    return i < 0 ? 0 : s_db->items[i].id;
}

}  // namespace Players
