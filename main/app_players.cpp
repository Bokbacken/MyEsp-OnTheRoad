#include "app_players.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "my_storage.h"
#include "my_time.h"

static const char *TAG = "players";

namespace Players {

static constexpr uint32_t MAGIC = 0x50525450;   // "PTRP"
static constexpr uint16_t VERSION = 1;

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

static bool saveLocked()
{
    if (!s_fsOk) return false;
    Header h = {MAGIC, VERSION, (uint16_t)sizeof(PlayerRec), (uint32_t)sizeof(PlayerDb)};
    bool ok = MyFat::writeAtomic(s_path, &h, sizeof(h), s_db, sizeof(PlayerDb));
    if (!ok) ESP_LOGE(TAG, "save failed");
    return ok;
}

static bool loadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Header h = {};
    bool ok = fread(&h, 1, sizeof(h), f) == sizeof(h) && h.magic == MAGIC && h.version == VERSION &&
              h.recSize == sizeof(PlayerRec) && h.dbSize == sizeof(PlayerDb) &&
              fread(s_db, 1, sizeof(PlayerDb), f) == sizeof(PlayerDb);
    fclose(f);
    if (!ok || s_db->count > PLAYER_MAX) {
        s_db->clear();
        return false;
    }
    for (int i = 0; i < s_db->count; i++) s_db->items[i].name[PLAYER_NAME_CAP - 1] = 0;
    return true;
}

bool begin()
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_db) {
        s_db = (PlayerDb *)heap_caps_malloc(sizeof(PlayerDb), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_db) s_db = (PlayerDb *)malloc(sizeof(PlayerDb));
        if (!s_db) return false;
    }
    Lock l;
    s_db->clear();
    s_fsOk = MyFat::mounted();
    if (!s_fsOk) {
        ESP_LOGE(TAG, "filesystem not mounted");
        return false;
    }
    MyFat::path(s_path, sizeof(s_path), "/players.bin");
    // Fallbacks for a power cut during the rename: new temp name, Arduino 0.3.x temp name.
    char tmpNew[56], tmpOld[48];
    snprintf(tmpNew, sizeof(tmpNew), "%s.tmp", s_path);
    MyFat::path(tmpOld, sizeof(tmpOld), "/players.tmp");
    const char *candidates[] = {s_path, tmpNew, tmpOld};
    for (int i = 0; i < 3; i++) {
        if (!MyFat::exists(candidates[i])) continue;
        if (loadFile(candidates[i])) {
            if (i > 0) saveLocked();
            break;
        }
        ESP_LOGW(TAG, "%s unreadable", candidates[i]);
    }
    ESP_LOGI(TAG, "%u player(s)", s_db->count);
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

int add(const char *name)
{
    if (!s_db) return (int)PlayerErr::INVALID;
    Lock l;
    int r = s_db->add(name, (uint32_t)MyTime::now());
    if (r > 0) saveLocked();
    return r;
}

int rename(uint16_t id, const char *name)
{
    if (!s_db) return (int)PlayerErr::NOT_FOUND;
    Lock l;
    PlayerErr e = s_db->rename(id, name);
    if (e == PlayerErr::OK) saveLocked();
    return (int)e;
}

int remove(uint16_t id)
{
    if (!s_db) return (int)PlayerErr::NOT_FOUND;
    Lock l;
    PlayerErr e = s_db->remove(id);
    if (e == PlayerErr::OK) saveLocked();
    return (int)e;
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
