// otr_log_core.h - v0.13.7 pure parts of the log module (host-tested in tests/test_otrlog.cpp):
// a byte ring with absolute offsets (readers keep their own position, like a tail -f), CRC-32 and
// the headers of an uncompressed ("store") ZIP archive.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

namespace OtrLogCore {

// Ring of cap bytes. head() = total bytes ever written; bytes [head-cap, head) are available.
class Ring {
public:
    void attach(char *buf, size_t cap)
    {
        m_buf = buf;
        m_cap = cap;
        m_head = 0;
    }
    size_t cap() const { return m_cap; }
    uint64_t head() const { return m_head; }
    uint64_t oldest() const { return m_head > m_cap ? m_head - m_cap : 0; }
    void write(const char *s, size_t n)
    {
        if (!m_buf || !m_cap) return;
        if (n > m_cap) {   // Only the tail fits
            s += n - m_cap;
            m_head += n - m_cap;
            n = m_cap;
        }
        size_t pos = (size_t)(m_head % m_cap);
        const size_t first = n < m_cap - pos ? n : m_cap - pos;
        memcpy(m_buf + pos, s, first);
        memcpy(m_buf, s + first, n - first);
        m_head += n;
    }
    // Copies up to max bytes starting at *from (moved forward to the oldest byte if it was
    // overwritten); advances *from. Returns the byte count.
    size_t read(char *out, size_t max, uint64_t *from) const
    {
        if (!m_buf || !from) return 0;
        if (*from < oldest()) *from = oldest();
        if (*from > m_head) *from = m_head;
        size_t n = (size_t)(m_head - *from);
        if (n > max) n = max;
        size_t pos = (size_t)(*from % m_cap);
        const size_t first = n < m_cap - pos ? n : m_cap - pos;
        memcpy(out, m_buf + pos, first);
        memcpy(out + first, m_buf, n - first);
        *from += n;
        return n;
    }

private:
    char *m_buf = nullptr;
    size_t m_cap = 0;
    uint64_t m_head = 0;
};

// Copies a formatted log line without ANSI colour sequences ("\033[0;32m") and '\r'; makes sure
// the result ends with exactly one '\n'. out needs n + 1 bytes. Returns the output length
// (0 for an empty line).
inline size_t cleanLine(const char *in, size_t n, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        const char c = in[i];
        if (c == '\033') {
            while (i < n && in[i] != 'm') i++;
            continue;
        }
        if (c == '\r') continue;
        out[o++] = c;
    }
    while (o > 0 && out[o - 1] == '\n') o--;
    if (o == 0) return 0;
    out[o++] = '\n';
    return o;
}

// Log level letter of a line "E (123) tag: ..." ('?' when the line has another shape)
inline char lineLevel(const char *s, size_t n)
{
    if (n >= 3 && s[1] == ' ' && s[2] == '(' && strchr("EWIDV", s[0])) return s[0];
    return '?';
}

inline uint32_t crc32Update(uint32_t crc, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// ---- ZIP (store) -----------------------------------------------------------------------------
struct ZipEntry {
    std::string name;
    uint32_t crc, size, offset;   // offset of the local header
};

inline void le16(std::vector<uint8_t> &o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
inline void le32(std::vector<uint8_t> &o, uint32_t v)
{
    for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}

// DOS time/date from broken-down local time (year >= 1980)
inline uint16_t dosTime(int h, int m, int s) { return (uint16_t)((h << 11) | (m << 5) | (s / 2)); }
inline uint16_t dosDate(int year, int mon, int day)
{
    if (year < 1980) return (uint16_t)((0 << 9) | (1 << 5) | 1);
    return (uint16_t)(((year - 1980) << 9) | (mon << 5) | day);
}

inline void zipLocal(std::vector<uint8_t> &o, const ZipEntry &e, uint16_t t, uint16_t d)
{
    o.clear();
    le32(o, 0x04034b50);
    le16(o, 10);   // Version needed: 1.0 (store)
    le16(o, 0);    // Flags
    le16(o, 0);    // Method: store
    le16(o, t);
    le16(o, d);
    le32(o, e.crc);
    le32(o, e.size);
    le32(o, e.size);
    le16(o, (uint16_t)e.name.size());
    le16(o, 0);
    o.insert(o.end(), e.name.begin(), e.name.end());
}

// Central directory + end record for all entries; cdOffset = where the directory starts
inline void zipDirectory(std::vector<uint8_t> &o, const std::vector<ZipEntry> &es, uint32_t cdOffset, uint16_t t,
                         uint16_t d)
{
    o.clear();
    for (const ZipEntry &e : es) {
        le32(o, 0x02014b50);
        le16(o, 20);   // Made by
        le16(o, 10);   // Needed
        le16(o, 0);
        le16(o, 0);
        le16(o, t);
        le16(o, d);
        le32(o, e.crc);
        le32(o, e.size);
        le32(o, e.size);
        le16(o, (uint16_t)e.name.size());
        le16(o, 0);    // Extra
        le16(o, 0);    // Comment
        le16(o, 0);    // Disk
        le16(o, 0);    // Internal attributes
        le32(o, 0);    // External attributes
        le32(o, e.offset);
        o.insert(o.end(), e.name.begin(), e.name.end());
    }
    const uint32_t cdSize = (uint32_t)o.size();
    le32(o, 0x06054b50);
    le16(o, 0);
    le16(o, 0);
    le16(o, (uint16_t)es.size());
    le16(o, (uint16_t)es.size());
    le32(o, cdSize);
    le32(o, cdOffset);
    le16(o, 0);
}

inline size_t zipLocalSize(const std::string &name) { return 30 + name.size(); }

}  // namespace OtrLogCore
