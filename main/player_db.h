// player_db.h - registry of up to 200 named players (pure logic, no Arduino; host-tested in
// tests/test_core.cpp). Persisted by board_players.cpp.
//
// - Every player has a stable numeric id (1..65535, never reused) so game history and
//   statistics survive renames.
// - Names are unique, compared case-insensitively (ASCII and the Latin-1 letters such as
//   å/Å, ä/Ä, ö/Ö), after trimming spaces. 1..23 UTF-8 bytes.
// - v0.5.6: optional personal 4-digit PIN per player (used to join a game from /play). The PIN is
//   never sent out by the web API, only `has_pin`.
#pragma once

#include <stdint.h>
#include <string.h>

constexpr int PLAYER_MAX = 200;
constexpr int PLAYER_NAME_CAP = 24;   // Same as YATZY_NAME_CAP
constexpr int PLAYER_PIN_LEN = 4;
constexpr int PLAYER_PIN_CAP = PLAYER_PIN_LEN + 1;

enum class PlayerErr : int8_t { OK = 0, INVALID = -1, DUPLICATE = -2, FULL = -3, NOT_FOUND = -4, BAD_PIN = -5 };

struct PlayerRec {
    uint16_t id;
    uint16_t flags;      // Reserved (bit 0: admin, later)
    uint32_t created;    // UTC epoch, 0 if the clock was unknown
    char name[PLAYER_NAME_CAP];
    char pin[PLAYER_PIN_CAP];   // "" = no PIN, else exactly 4 digits
    uint8_t reserved[3];
};
static_assert(sizeof(PlayerRec) == 40, "players.bin record layout");

// File format v1 (v0.4.x-v0.5.5): record without PIN. Only used to migrate old files.
struct PlayerRecV1 {
    uint16_t id;
    uint16_t flags;
    uint32_t created;
    char name[PLAYER_NAME_CAP];
};
struct PlayerDbV1 {
    uint16_t nextId;
    uint16_t count;
    PlayerRecV1 items[PLAYER_MAX];
};
static_assert(sizeof(PlayerRecV1) == 32 && sizeof(PlayerDbV1) == 6404, "players.bin v1 layout");

struct PlayerDb {
    uint16_t nextId;
    uint16_t count;
    PlayerRec items[PLAYER_MAX];

    void clear()
    {
        memset(this, 0, sizeof(*this));
        nextId = 1;
    }

    // Lower-cases one character at p (ASCII or 2-byte Latin-1 C3 80..9E) into a code point
    // and returns its byte length.
    static int foldChar(const char *p, uint32_t &cp)
    {
        uint8_t c = (uint8_t)p[0];
        if (c < 0x80) {
            cp = (c >= 'A' && c <= 'Z') ? c + 32 : c;
            return 1;
        }
        if ((c & 0xE0) == 0xC0 && p[1]) {
            cp = ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)p[1] & 0x3F);
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 0x20;
            return 2;
        }
        int n = (c & 0xF0) == 0xE0 ? 3 : 4;
        cp = c;
        for (int i = 1; i < n && p[i]; i++) cp = (cp << 8) | (uint8_t)p[i];
        int len = 1;
        while (len < n && p[len]) len++;
        return len;
    }

    static bool sameName(const char *a, const char *b)
    {
        while (*a && *b) {
            uint32_t ca, cb;
            a += foldChar(a, ca);
            b += foldChar(b, cb);
            if (ca != cb) return false;
        }
        return *a == *b;
    }

    // Case-insensitive ordering for sorted lists (<0, 0, >0). å ä ö sort after z like in Swedish.
    static int compareNames(const char *a, const char *b)
    {
        while (*a && *b) {
            uint32_t ca, cb;
            a += foldChar(a, ca);
            b += foldChar(b, cb);
            ca = swedishKey(ca);
            cb = swedishKey(cb);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        return (*a ? 1 : 0) - (*b ? 1 : 0);
    }

    static uint32_t swedishKey(uint32_t cp)
    {
        switch (cp) {
        case 0xE5: return 'z' + 1;   // å
        case 0xE4: return 'z' + 2;   // ä
        case 0xE6: return 'z' + 2;   // æ
        case 0xF6: return 'z' + 3;   // ö
        case 0xF8: return 'z' + 3;   // ø
        default: return cp;
        }
    }

    // Copies `in` trimmed into out (cap PLAYER_NAME_CAP). False if empty / too long / control chars.
    static bool normalize(const char *in, char *out)
    {
        if (!in) return false;
        while (*in == ' ') in++;
        size_t n = strlen(in);
        while (n && in[n - 1] == ' ') n--;
        if (n == 0 || n >= (size_t)PLAYER_NAME_CAP) return false;
        for (size_t i = 0; i < n; i++)
            if ((uint8_t)in[i] < 0x20 || in[i] == 0x7F || in[i] == '"' || in[i] == '\\') return false;
        memcpy(out, in, n);
        out[n] = 0;
        return true;
    }

    // True for exactly 4 ASCII digits (or "" when allowEmpty).
    static bool validPin(const char *pin, bool allowEmpty = false)
    {
        if (!pin || !*pin) return allowEmpty;
        for (int i = 0; i < PLAYER_PIN_LEN; i++)
            if (pin[i] < '0' || pin[i] > '9') return false;
        return pin[PLAYER_PIN_LEN] == 0;
    }

    // Converts a v1 file body; every player gets an empty PIN. False if the data is inconsistent.
    bool loadV1(const PlayerDbV1 &old)
    {
        clear();
        if (old.count > PLAYER_MAX) return false;
        nextId = old.nextId ? old.nextId : 1;
        count = old.count;
        for (int i = 0; i < count; i++) {
            items[i].id = old.items[i].id;
            items[i].flags = old.items[i].flags;
            items[i].created = old.items[i].created;
            memcpy(items[i].name, old.items[i].name, PLAYER_NAME_CAP);
            items[i].name[PLAYER_NAME_CAP - 1] = 0;
        }
        return true;
    }

    // Terminates strings and drops PINs that are not valid (defensive after loading a file).
    void sanitize()
    {
        for (int i = 0; i < count; i++) {
            items[i].name[PLAYER_NAME_CAP - 1] = 0;
            items[i].pin[PLAYER_PIN_LEN] = 0;
            if (!validPin(items[i].pin, true)) memset(items[i].pin, 0, PLAYER_PIN_CAP);
        }
    }

    bool hasPin(uint16_t id) const
    {
        const PlayerRec *r = get(id);
        return r && r->pin[0];
    }

    // Constant-time compare (does not leak how many digits matched). False if no PIN is set.
    bool checkPin(uint16_t id, const char *pin) const
    {
        const PlayerRec *r = get(id);
        if (!r || !r->pin[0] || !validPin(pin)) return false;
        uint8_t diff = 0;
        for (int i = 0; i < PLAYER_PIN_LEN; i++) diff |= (uint8_t)(r->pin[i] ^ pin[i]);
        return diff == 0;
    }

    // "" or nullptr clears the PIN.
    PlayerErr setPin(uint16_t id, const char *pin)
    {
        int i = indexOf(id);
        if (i < 0) return PlayerErr::NOT_FOUND;
        if (!validPin(pin, true)) return PlayerErr::BAD_PIN;
        memset(items[i].pin, 0, PLAYER_PIN_CAP);
        if (pin && *pin) memcpy(items[i].pin, pin, PLAYER_PIN_LEN);
        return PlayerErr::OK;
    }

    int indexOf(uint16_t id) const
    {
        if (!id) return -1;
        for (int i = 0; i < count; i++)
            if (items[i].id == id) return i;
        return -1;
    }

    int indexOfName(const char *name) const
    {
        char n[PLAYER_NAME_CAP];
        if (!normalize(name, n)) return -1;
        for (int i = 0; i < count; i++)
            if (sameName(items[i].name, n)) return i;
        return -1;
    }

    const PlayerRec *get(uint16_t id) const
    {
        int i = indexOf(id);
        return i < 0 ? nullptr : &items[i];
    }

    // Returns the new id (> 0) or a negative PlayerErr.
    int add(const char *name, uint32_t now, const char *pin = nullptr)
    {
        char n[PLAYER_NAME_CAP];
        if (!normalize(name, n)) return (int)PlayerErr::INVALID;
        if (!validPin(pin, true)) return (int)PlayerErr::BAD_PIN;
        if (indexOfName(n) >= 0) return (int)PlayerErr::DUPLICATE;
        if (count >= PLAYER_MAX) return (int)PlayerErr::FULL;
        if (nextId == 0) nextId = 1;
        // Ids are never reused; skip any still taken after a 65535 wrap.
        for (int guard = 0; indexOf(nextId) >= 0 && guard < 65536; guard++) nextId = nextId == 65535 ? 1 : nextId + 1;
        PlayerRec &r = items[count++];
        memset(&r, 0, sizeof(r));
        r.id = nextId;
        r.created = now;
        memcpy(r.name, n, sizeof(n));
        if (pin && *pin) memcpy(r.pin, pin, PLAYER_PIN_LEN);
        nextId = nextId == 65535 ? 1 : nextId + 1;
        return r.id;
    }

    PlayerErr rename(uint16_t id, const char *name)
    {
        int i = indexOf(id);
        if (i < 0) return PlayerErr::NOT_FOUND;
        char n[PLAYER_NAME_CAP];
        if (!normalize(name, n)) return PlayerErr::INVALID;
        int other = indexOfName(n);
        if (other >= 0 && other != i) return PlayerErr::DUPLICATE;
        memcpy(items[i].name, n, sizeof(n));
        return PlayerErr::OK;
    }

    PlayerErr remove(uint16_t id)
    {
        int i = indexOf(id);
        if (i < 0) return PlayerErr::NOT_FOUND;
        memmove(&items[i], &items[i + 1], sizeof(PlayerRec) * (count - i - 1));
        count--;
        memset(&items[count], 0, sizeof(PlayerRec));
        return PlayerErr::OK;
    }

    // Fills idx[0..count) with indices sorted by name. Returns count.
    int sortedIndices(uint8_t *idx) const
    {
        for (int i = 0; i < count; i++) idx[i] = (uint8_t)i;
        for (int i = 1; i < count; i++) {   // Insertion sort, n <= 200
            uint8_t t = idx[i];
            int j = i - 1;
            while (j >= 0 && compareNames(items[idx[j]].name, items[t].name) > 0) { idx[j + 1] = idx[j]; j--; }
            idx[j + 1] = t;
        }
        return count;
    }
};

inline const char *playerErrText(int e)
{
    switch ((PlayerErr)e) {
    case PlayerErr::INVALID: return "Ogiltigt namn (1-23 tecken)";
    case PlayerErr::DUPLICATE: return "Namnet finns redan";
    case PlayerErr::FULL: return "Max 200 spelare";
    case PlayerErr::NOT_FOUND: return "Spelaren finns inte";
    case PlayerErr::BAD_PIN: return "PIN måste vara 4 siffror";
    default: return "";
    }
}
