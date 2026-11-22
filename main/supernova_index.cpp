// supernova_index.cpp - see supernova_index.h
#include "supernova_index.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace SupernovaIndex {
namespace {

uint32_t fnv(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

void put8(std::vector<uint8_t> &o, uint8_t v) { o.push_back(v); }
void put16(std::vector<uint8_t> &o, uint16_t v)
{
    o.push_back((uint8_t)v);
    o.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t> &o, uint32_t v)
{
    for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}

struct Reader {
    const uint8_t *p, *end;
    bool ok = true;
    uint32_t num(int bytes)
    {
        if ((size_t)(end - p) < (size_t)bytes) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < bytes; i++) v |= (uint32_t)p[i] << (8 * i);
        p += bytes;
        return v;
    }
    std::string str(size_t n)
    {
        if ((size_t)(end - p) < n) {
            ok = false;
            return std::string();
        }
        std::string s((const char *)p, n);
        p += n;
        return s;
    }
};

bool validUtf8(const std::string &s)
{
    const uint8_t *p = (const uint8_t *)s.data(), *end = p + s.size();
    while (p < end) {
        const uint8_t c = *p++;
        int extra = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
        if (extra < 0 || end - p < extra) return false;
        for (int i = 0; i < extra; i++)
            if ((*p++ & 0xC0) != 0x80) return false;
    }
    return true;
}

bool iequals(const char *a, size_t n, const char *b)
{
    if (strlen(b) != n) return false;
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return false;
    }
    return true;
}

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

// "Pop, Rock;Dance" -> Pop / Rock / Dance
std::vector<std::string> listTokens(const char *s)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char *p = s;; p++) {
        if (*p == ',' || *p == ';' || !*p) {
            const std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
            if (!*p) break;
        } else {
            cur += *p;
        }
    }
    return out;
}

const char *artistKey(const char *artist, const char *title) { return *artist ? artist : title; }

std::vector<std::string> words(const std::string &text)
{
    std::vector<std::string> out;
    const std::string f = KaraIndex::fold(text.c_str());
    size_t i = 0;
    while (i < f.size()) {
        while (i < f.size() && f[i] == ' ') i++;
        size_t j = i;
        while (j < f.size() && f[j] != ' ') j++;
        if (j > i) out.push_back(f.substr(i, j - i));
        i = j;
    }
    return out;
}

}  // namespace

void parseTags(const char *data, size_t len, Tags &out)
{
    out = Tags();
    if (!data) return;
    size_t pos = 0;
    if (len >= 3 && (uint8_t)data[0] == 0xEF && (uint8_t)data[1] == 0xBB && (uint8_t)data[2] == 0xBF) pos = 3;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && data[eol] != '\n') eol++;
        size_t end = eol;
        if (end > pos && data[end - 1] == '\r') end--;
        const char *p = data + pos;
        const char *e = data + end;
        pos = eol + 1;
        while (p < e && (*p == ' ' || *p == '\t')) p++;
        if (p == e) continue;
        if (*p != '#') break;   // First note line: the header is over
        const char *colon = p + 1;
        while (colon < e && *colon != ':') colon++;
        if (colon == e) continue;
        const char *key = p + 1;
        const size_t keyLen = (size_t)(colon - key);
        std::string value = trim(std::string(colon + 1, e));
        if (value.empty() || !validUtf8(value)) continue;
        if (iequals(key, keyLen, "TITLE")) out.title = value;
        else if (iequals(key, keyLen, "ARTIST")) out.artist = value;
        else if (iequals(key, keyLen, "GENRE")) out.genre = value;
        else if (iequals(key, keyLen, "LANGUAGE")) out.language = value;
        else if (iequals(key, keyLen, "YEAR")) {
            unsigned y = 0;
            size_t digits = 0;
            for (char c : value) {
                if (c < '0' || c > '9') break;
                y = y * 10 + (unsigned)(c - '0');
                if (++digits == 4) break;
            }
            out.year = (digits == 4 && y >= 1900 && y <= 2100) ? (uint16_t)y : 0;
        }
    }
}

void namesFromFolder(const std::string &folder, std::string &artist, std::string &title)
{
    const size_t dash = folder.find(" - ");
    if (dash == std::string::npos) {
        artist.clear();
        title = folder;
    } else {
        artist = folder.substr(0, dash);
        title = folder.substr(dash + 3);
    }
}

void Library::clear()
{
    m_pool.clear();
    m_recs.clear();
}

uint32_t Library::put(const char *s, size_t maxLen)
{
    const size_t full = s ? strlen(s) : 0;
    size_t n = std::min(full, maxLen);
    while (n > 0 && n < full && ((uint8_t)s[n] & 0xC0) == 0x80) n--;   // Do not cut a UTF-8 sequence
    const uint32_t off = (uint32_t)m_pool.size();
    if (n) m_pool.insert(m_pool.end(), s, s + n);
    m_pool.push_back(0);
    return off;
}

bool Library::add(const char *folder, const char *artist, const char *title, const char *genre,
                  const char *language, uint16_t year, uint32_t mtime)
{
    if (full() || !folder || !*folder || strlen(folder) > MAX_FOLDER || strchr(folder, '/') || strchr(folder, '\\'))
        return false;
    Rec r;
    r.folder = put(folder, MAX_FOLDER);
    r.artist = put(artist, MAX_FIELD);
    r.title = put(title, MAX_FIELD);
    r.genre = put(genre, MAX_FIELD);
    r.language = put(language, MAX_FIELD);
    r.year = year;
    r.mtime = mtime;
    m_recs.push_back(r);
    return true;
}

int Library::find(const char *folderName) const
{
    for (size_t i = 0; i < m_recs.size(); i++)
        if (!strcmp(folder(i), folderName)) return (int)i;
    return -1;
}

std::vector<uint16_t> Library::view(const Query &q) const
{
    std::vector<uint16_t> out;
    const std::vector<std::string> wanted = words(q.text);
    const std::string foldedValue = KaraIndex::fold(q.value.c_str());
    const int decade = q.kind == FilterKind::DECADE ? atoi(q.value.c_str()) : 0;
    for (size_t i = 0; i < m_recs.size(); i++) {
        if (q.kind == FilterKind::GENRE || q.kind == FilterKind::LANGUAGE) {
            bool hit = false;
            for (const std::string &t : listTokens(q.kind == FilterKind::GENRE ? genre(i) : language(i)))
                if (KaraIndex::fold(t.c_str()) == foldedValue) hit = true;
            if (!hit) continue;
        } else if (q.kind == FilterKind::DECADE) {
            if (!year(i) || year(i) / 10 * 10 != decade) continue;
        }
        if (!wanted.empty()) {
            const std::string hay = KaraIndex::fold(artist(i)) + " " + KaraIndex::fold(title(i));
            bool all = true;
            for (const std::string &w : wanted)
                if (hay.find(w) == std::string::npos) { all = false; break; }
            if (!all) continue;
        }
        out.push_back((uint16_t)i);
    }
    std::stable_sort(out.begin(), out.end(), [&](uint16_t a, uint16_t b) {
        int c = 0;
        switch (q.sort) {
        case Sort::ARTIST:
            c = KaraIndex::collate(artistKey(artist(a), title(a)), artistKey(artist(b), title(b)));
            if (!c) c = KaraIndex::collate(title(a), title(b));
            break;
        case Sort::TITLE:
            c = KaraIndex::collate(title(a), title(b));
            if (!c) c = KaraIndex::collate(artist(a), artist(b));
            break;
        case Sort::YEAR: {
            const int ya = year(a) ? year(a) : 9999, yb = year(b) ? year(b) : 9999;   // Unknown last
            c = ya - yb;
            if (!c) c = KaraIndex::collate(artistKey(artist(a), title(a)), artistKey(artist(b), title(b)));
            if (!c) c = KaraIndex::collate(title(a), title(b));
            break;
        }
        }
        if (!c) c = strcmp(folder(a), folder(b));
        return c < 0;
    });
    return out;
}

std::vector<Value> Library::values(FilterKind kind) const
{
    std::vector<Value> out;
    std::vector<std::string> keys;   // Folded key per out entry
    auto bump = [&](const std::string &name, const std::string &key, const std::string &fkey) {
        for (size_t k = 0; k < keys.size(); k++)
            if (keys[k] == fkey) {
                out[k].count++;
                return;
            }
        keys.push_back(fkey);
        out.push_back(Value{name, key, 1});
    };
    for (size_t i = 0; i < m_recs.size(); i++) {
        if (kind == FilterKind::DECADE) {
            if (!year(i)) continue;
            const std::string d = std::to_string(year(i) / 10 * 10);
            bump(d + "s", d, d);
        } else if (kind != FilterKind::NONE) {
            std::vector<std::string> seen;
            for (const std::string &t : listTokens(kind == FilterKind::GENRE ? genre(i) : language(i))) {
                const std::string f = KaraIndex::fold(t.c_str());
                if (std::find(seen.begin(), seen.end(), f) != seen.end()) continue;
                seen.push_back(f);
                bump(t, t, f);
            }
        }
    }
    if (kind == FilterKind::DECADE)
        std::sort(out.begin(), out.end(), [](const Value &a, const Value &b) { return a.key < b.key; });
    else
        std::sort(out.begin(), out.end(), [](const Value &a, const Value &b) {
            return KaraIndex::collate(a.name.c_str(), b.name.c_str()) < 0;
        });
    return out;
}

uint16_t Library::rank(size_t i, Sort sort) const
{
    switch (sort) {
    case Sort::ARTIST: return KaraIndex::firstRank(artistKey(artist(i), title(i)));
    case Sort::TITLE: return KaraIndex::firstRank(title(i));
    default: return 0;
    }
}

void Library::encode(std::vector<uint8_t> &o) const
{
    o.clear();
    o.reserve(16 + m_pool.size() + m_recs.size() * 12);
    o.insert(o.end(), {'O', 'T', 'R', 'S'});
    put8(o, VERSION);
    put32(o, (uint32_t)m_recs.size());
    for (size_t i = 0; i < m_recs.size(); i++) {
        const size_t fl = strlen(folder(i));
        put16(o, (uint16_t)fl);
        o.insert(o.end(), folder(i), folder(i) + fl);
        for (const char *s : {artist(i), title(i), genre(i), language(i)}) {
            const size_t n = strlen(s);
            put8(o, (uint8_t)n);
            o.insert(o.end(), s, s + n);
        }
        put16(o, year(i));
        put32(o, mtime(i));
    }
    put32(o, fnv(o.data(), o.size()));
}

bool Library::decode(const uint8_t *data, size_t len)
{
    clear();
    if (!data || len < 4 + 1 + 4 + 4 || memcmp(data, "OTRS", 4) != 0) return false;
    const uint32_t sum = (uint32_t)data[len - 4] | (uint32_t)data[len - 3] << 8 | (uint32_t)data[len - 2] << 16 |
                         (uint32_t)data[len - 1] << 24;
    if (fnv(data, len - 4) != sum) return false;
    Reader r{data + 4, data + len - 4, true};
    if (r.num(1) != VERSION) return false;
    const uint32_t count = r.num(4);
    if (!r.ok || count > MAX_SONGS) return false;
    for (uint32_t i = 0; i < count && r.ok; i++) {
        const std::string f = r.str(r.num(2));
        const std::string a = r.str(r.num(1)), t = r.str(r.num(1)), g = r.str(r.num(1)), l = r.str(r.num(1));
        const uint16_t y = (uint16_t)r.num(2);
        const uint32_t m = r.num(4);
        if (r.ok && !add(f.c_str(), a.c_str(), t.c_str(), g.c_str(), l.c_str(), y, m)) r.ok = false;
    }
    if (!r.ok || r.p != r.end) {
        clear();
        return false;
    }
    return true;
}

size_t jumpIndex(const Library &lib, const std::vector<uint16_t> &view, Sort sort, uint16_t r)
{
    for (size_t i = 0; i < view.size(); i++)
        if (lib.rank(view[i], sort) >= r) return i;
    return view.size();
}

}  // namespace SupernovaIndex
