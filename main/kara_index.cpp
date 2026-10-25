// kara_index.cpp - see kara_index.h.
#include "kara_index.h"

#include <string.h>
#include <strings.h>

#include <algorithm>

namespace KaraIndex {

namespace {

// Latin-1 letter (lower or upper case, 0xC0..0xFF) -> rank
uint16_t latin1Rank(uint8_t c)
{
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c = (uint8_t)(c + 0x20);   // Upper -> lower
    switch (c) {
    case 0xE5: return 56;                      // å
    case 0xE4: case 0xE6: return 57;           // ä æ
    case 0xF6: case 0xF8: return 58;           // ö ø
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: return 30;            // à á â ã
    case 0xE7: return 32;                                             // ç
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 34;            // è é ê ë
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 38;            // ì í î ï
    case 0xF1: return 43;                                             // ñ
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: return 44;            // ò ó ô õ
    case 0xF9: case 0xFA: case 0xFB: return 50;                       // ù ú û
    case 0xFC: case 0xFD: case 0xFF: return 54;                       // ü ý ÿ (ü sorts as y)
    case 0xDF: return 48;                                             // ß
    default: return 1;
    }
}

void put8(std::vector<uint8_t> &o, uint8_t v) { o.push_back(v); }
void put16(std::vector<uint8_t> &o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
void put32(std::vector<uint8_t> &o, uint32_t v)
{
    for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}
void put64(std::vector<uint8_t> &o, uint64_t v)
{
    for (int i = 0; i < 8; i++) o.push_back((uint8_t)(v >> (8 * i)));
}

uint32_t fnv(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

struct Reader {
    const uint8_t *p, *end;
    bool ok = true;
    bool need(size_t n)
    {
        if ((size_t)(end - p) < n) ok = false;
        return ok;
    }
    uint64_t num(int bytes)
    {
        if (!need((size_t)bytes)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < bytes; i++) v |= (uint64_t)p[i] << (8 * i);
        p += bytes;
        return v;
    }
    std::string str(size_t n)
    {
        if (!need(n)) return std::string();
        std::string s((const char *)p, n);
        p += n;
        return s;
    }
};

}  // namespace

uint16_t rankAt(const char *&p)
{
    const uint8_t c = (uint8_t)*p;
    if (!c) return 0;
    p++;
    if (c >= '0' && c <= '9') return (uint16_t)(10 + (c - '0'));
    if (c >= 'a' && c <= 'z') return (uint16_t)(30 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (uint16_t)(30 + (c - 'A'));
    if (c < 0x80) return 1;
    if ((c == 0xC3 || c == 0xC2) && ((uint8_t)*p & 0xC0) == 0x80) {   // Latin-1 range in UTF-8
        const uint8_t c2 = (uint8_t)*p++;
        return c == 0xC3 ? latin1Rank((uint8_t)(0xC0 | (c2 & 0x3F))) : 1;
    }
    while (((uint8_t)*p & 0xC0) == 0x80) p++;   // Skip the rest of any other sequence
    return 1;
}

uint16_t firstRank(const char *s)
{
    // Skip leading symbols/spaces ("'Til Tuesday" sorts under T)
    const char *p = s;
    while (*p) {
        const uint16_t r = rankAt(p);
        if (r != 1) return r;
    }
    return s && *s ? 1 : 0;
}

int collate(const char *a, const char *b)
{
    const char *pa = a, *pb = b;
    for (;;) {
        const uint16_t ra = rankAt(pa), rb = rankAt(pb);
        if (ra != rb) return ra < rb ? -1 : 1;
        if (!ra) break;
    }
    return strcasecmp(a, b);   // Equal by rank (e.g. é vs e): stable tie-break
}

std::string fold(const char *s)
{
    std::string o;
    for (const uint8_t *p = (const uint8_t *)s; p && *p; p++) {
        uint8_t c = *p;
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
        else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97) {   // Latin-1 upper case
            o += (char)c;
            o += (char)(p[1] + 0x20);
            p++;
            continue;
        }
        o += (char)c;
    }
    return o;
}

bool safeRel(const char *rel)
{
    if (!rel || !*rel || *rel == '/' || strlen(rel) > MAX_PATH || strchr(rel, '\\')) return false;
    const char *part = rel;
    for (const char *p = rel;; p++) {
        if (*p == '/' || !*p) {
            const size_t n = (size_t)(p - part);
            if (n == 0) return false;                                  // "a//b" or trailing '/'
            if ((n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
            if (!*p) break;
            part = p + 1;
        }
    }
    return true;
}

bool songExt(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : nullptr;
    if (!dot) return false;
    return !strcasecmp(dot, ".mid") || !strcasecmp(dot, ".kar") || !strcasecmp(dot, ".midi") ||
           !strcasecmp(dot, ".rmi");
}

void Library::clear()
{
    m_pool.clear();
    m_recs.clear();
}

uint32_t Library::put(const char *s, size_t n)
{
    const uint32_t off = (uint32_t)m_pool.size();
    m_pool.insert(m_pool.end(), s, s + n);
    m_pool.push_back(0);
    return off;
}

bool Library::add(const char *rel, const char *title, const char *artist, uint32_t size)
{
    if (full() || !safeRel(rel)) return false;
    const char *slash = strrchr(rel, '/');
    Rec r;
    r.dirLen = (uint16_t)(slash ? slash - rel : 0);
    r.rel = put(rel, strlen(rel));
    r.title = put(title ? title : "", title ? strnlen(title, 255) : 0);
    r.artist = put(artist ? artist : "", artist ? strnlen(artist, 255) : 0);
    r.size = size;
    m_recs.push_back(r);
    return true;
}

void Library::sort()
{
    std::stable_sort(m_recs.begin(), m_recs.end(), [this](const Rec &a, const Rec &b) {
        const char *aa = &m_pool[a.artist], *ba = &m_pool[b.artist];
        const char *at = &m_pool[a.title], *bt = &m_pool[b.title];
        int c = collate(*aa ? aa : at, *ba ? ba : bt);
        if (!c) c = collate(at, bt);
        if (!c) c = strcmp(&m_pool[a.rel], &m_pool[b.rel]);
        return c < 0;
    });
}

std::vector<std::string> Library::subdirs(const std::string &dir) const
{
    std::vector<std::string> out;
    const size_t n = dir.size();
    for (const Rec &r : m_recs) {
        if (r.dirLen <= n) continue;
        const char *d = &m_pool[r.rel];
        size_t start = 0;
        if (n) {
            if (memcmp(d, dir.data(), n) != 0 || d[n] != '/') continue;
            start = n + 1;
        }
        size_t end = start;
        while (end < r.dirLen && d[end] != '/') end++;
        std::string name(d + start, end - start);
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
    }
    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) { return collate(a.c_str(), b.c_str()) < 0; });
    return out;
}

std::vector<uint16_t> Library::songsIn(const std::string &dir) const
{
    std::vector<uint16_t> out;
    const size_t n = dir.size();
    for (size_t i = 0; i < m_recs.size(); i++) {
        const Rec &r = m_recs[i];
        if (n) {
            const char *d = &m_pool[r.rel];
            if (r.dirLen < n || memcmp(d, dir.data(), n) != 0 || (r.dirLen > n && d[n] != '/')) continue;
        }
        out.push_back((uint16_t)i);
    }
    return out;
}

std::vector<uint16_t> Library::search(const char *q) const
{
    std::vector<uint16_t> out;
    std::vector<std::string> words;
    const std::string fq = fold(q);
    size_t i = 0;
    while (i < fq.size()) {
        while (i < fq.size() && fq[i] == ' ') i++;
        size_t j = i;
        while (j < fq.size() && fq[j] != ' ') j++;
        if (j > i) words.push_back(fq.substr(i, j - i));
        i = j;
    }
    if (words.empty()) return out;
    for (size_t k = 0; k < m_recs.size(); k++) {
        const std::string hay = fold(artist(k)) + " " + fold(title(k));
        bool all = true;
        for (const std::string &w : words)
            if (hay.find(w) == std::string::npos) { all = false; break; }
        if (all) out.push_back((uint16_t)k);
    }
    return out;
}

int Library::find(const char *rel) const
{
    for (size_t i = 0; i < m_recs.size(); i++)
        if (!strcmp(&m_pool[m_recs[i].rel], rel)) return (int)i;
    return -1;
}

void Library::encode(const Header &h, std::vector<uint8_t> &o) const
{
    o.clear();
    o.reserve(32 + m_pool.size() + m_recs.size() * 8);
    o.insert(o.end(), {'O', 'T', 'R', 'K'});
    put8(o, VERSION);
    put32(o, h.scanTime);
    put64(o, h.freeBytes);
    put32(o, h.rootMtime);
    put32(o, (uint32_t)m_recs.size());
    for (size_t i = 0; i < m_recs.size(); i++) {
        const size_t pl = strlen(rel(i)), tl = strlen(title(i)), al = strlen(artist(i));
        put16(o, (uint16_t)pl);
        o.insert(o.end(), rel(i), rel(i) + pl);
        put8(o, (uint8_t)tl);
        o.insert(o.end(), title(i), title(i) + tl);
        put8(o, (uint8_t)al);
        o.insert(o.end(), artist(i), artist(i) + al);
        put32(o, m_recs[i].size);
    }
    put32(o, fnv(o.data(), o.size()));
}

bool Library::decode(const uint8_t *data, size_t len, Header &h)
{
    clear();
    if (!data || len < 4 + 1 + 4 + 8 + 4 + 4 + 4 || memcmp(data, "OTRK", 4) != 0) return false;
    const uint32_t sum = (uint32_t)data[len - 4] | (uint32_t)data[len - 3] << 8 | (uint32_t)data[len - 2] << 16 |
                         (uint32_t)data[len - 1] << 24;
    if (fnv(data, len - 4) != sum) return false;
    Reader r{data + 4, data + len - 4};
    h.version = (uint8_t)r.num(1);
    if (h.version != VERSION) return false;
    h.scanTime = (uint32_t)r.num(4);
    h.freeBytes = r.num(8);
    h.rootMtime = (uint32_t)r.num(4);
    h.count = (uint32_t)r.num(4);
    if (!r.ok || h.count > MAX_SONGS) return false;
    for (uint32_t i = 0; i < h.count && r.ok; i++) {
        const std::string path = r.str((size_t)r.num(2));
        const std::string title = r.str((size_t)r.num(1));
        const std::string artist = r.str((size_t)r.num(1));
        const uint32_t size = (uint32_t)r.num(4);
        if (r.ok && !add(path.c_str(), title.c_str(), artist.c_str(), size)) r.ok = false;
    }
    if (!r.ok || r.p != r.end) {
        clear();
        return false;
    }
    return true;
}

size_t jumpIndex(const Library &lib, const std::vector<uint16_t> &view, uint16_t r)
{
    for (size_t i = 0; i < view.size(); i++)
        if (lib.rank(view[i]) >= r) return i;
    return view.size();
}

}  // namespace KaraIndex
