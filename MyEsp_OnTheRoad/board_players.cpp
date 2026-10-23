#include "board_players.h"

#include <Arduino.h>
#include <FFat.h>

#include "board_clock.h"

namespace Players {

static constexpr uint32_t MAGIC = 0x50525450;   // "PTRP"
static constexpr uint16_t VERSION = 1;
static constexpr const char *PATH = "/players.bin";
static constexpr const char *TMP = "/players.tmp";

struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t recSize;
    uint32_t dbSize;
};

static SemaphoreHandle_t s_lock = nullptr;
static PlayerDb *s_db = nullptr;   // In PSRAM (~6.4 kB)
static bool s_fsOk = false;

struct Lock {
    Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_lock); }
};

static bool saveLocked()
{
    if (!s_fsOk) return false;
    File f = FFat.open(TMP, FILE_WRITE);
    if (!f) return false;
    Header h = {MAGIC, VERSION, (uint16_t)sizeof(PlayerRec), (uint32_t)sizeof(PlayerDb)};
    bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) &&
              f.write((const uint8_t *)s_db, sizeof(PlayerDb)) == sizeof(PlayerDb);
    f.close();
    if (!ok) {
        FFat.remove(TMP);
        return false;
    }
    FFat.remove(PATH);
    ok = FFat.rename(TMP, PATH);
    if (!ok) Serial.println("[players] rename failed");
    return ok;
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
    // formatOnFail: the first boot after flashing formats the 9 MB FAT partition (a few seconds).
    s_fsOk = FFat.begin(true);
    if (!s_fsOk) {
        Serial.println("[players] FFat mount FAILED");
        return false;
    }
    const char *path = FFat.exists(PATH) ? PATH : (FFat.exists(TMP) ? TMP : nullptr);   // TMP: cut during rename
    if (path) {
        File f = FFat.open(path, FILE_READ);
        Header h = {};
        bool ok = f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == MAGIC && h.version == VERSION &&
                  h.recSize == sizeof(PlayerRec) && h.dbSize == sizeof(PlayerDb) &&
                  f.read((uint8_t *)s_db, sizeof(PlayerDb)) == sizeof(PlayerDb);
        if (f) f.close();
        if (!ok || s_db->count > PLAYER_MAX) {
            Serial.printf("[players] %s unreadable, starting empty\n", path);
            s_db->clear();
        } else {
            for (int i = 0; i < s_db->count; i++) s_db->items[i].name[PLAYER_NAME_CAP - 1] = 0;
            if (path == TMP) saveLocked();
        }
    }
    Serial.printf("[players] %u player(s), FFat %lu/%lu kB used\n", s_db->count,
                  (unsigned long)(FFat.usedBytes() >> 10), (unsigned long)(FFat.totalBytes() >> 10));
    return true;
}

bool ok() { return s_fsOk; }

void snapshot(PlayerDb &out)
{
    if (!s_db) { out.clear(); return; }
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
    int r = s_db->add(name, (uint32_t)Clock::now());
    if (r > 0 && !saveLocked()) Serial.println("[players] save failed");
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

uint32_t fsTotalBytes() { return s_fsOk ? FFat.totalBytes() : 0; }
uint32_t fsUsedBytes() { return s_fsOk ? FFat.usedBytes() : 0; }

}  // namespace Players
