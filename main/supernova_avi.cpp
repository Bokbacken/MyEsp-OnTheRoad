// supernova_avi.cpp - see supernova_avi.h.
#include "supernova_avi.h"

#include <string.h>

#include <algorithm>

namespace SupernovaAvi {

namespace {

uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
bool tag(const uint8_t *p, const char *t) { return memcmp(p, t, 4) == 0; }

struct Stream {
    char type[4] = {};   // "vids" / "auds"
    uint32_t scale = 0, rate = 0;
    uint16_t format = 0; // Audio wFormatTag (0x55 = MP3) or 0
    bool mjpeg = false;
};

// Reads the strl lists out of an in-memory hdrl list body; streams are numbered in order.
void parseHeaders(const uint8_t *p, size_t n, Info &info, std::vector<Stream> &streams)
{
    size_t i = 0;
    while (i + 8 <= n) {
        const uint32_t size = le32(p + i + 4);
        const uint8_t *body = p + i + 8;
        if (i + 8 + size > n) break;
        if (tag(p + i, "avih") && size >= 40) {
            const uint32_t usPerFrame = le32(body);
            if (usPerFrame) info.fps = 1000000.0 / usPerFrame;
            info.width = le32(body + 32);
            info.height = le32(body + 36);
        } else if (tag(p + i, "LIST") && size >= 4 && tag(body, "strl")) {
            Stream s;
            size_t j = 4;
            while (j + 8 <= size) {
                const uint32_t csize = le32(body + j + 4);
                const uint8_t *cb = body + j + 8;
                if (j + 8 + csize > size) break;
                if (tag(body + j, "strh") && csize >= 28) {
                    memcpy(s.type, cb, 4);
                    s.mjpeg = tag(cb + 4, "MJPG") || tag(cb + 4, "mjpg");
                    s.scale = le32(cb + 20);
                    s.rate = le32(cb + 24);
                } else if (tag(body + j, "strf") && csize >= 2 && !memcmp(s.type, "auds", 4)) {
                    s.format = le16(cb);
                } else if (tag(body + j, "strf") && csize >= 20 && !memcmp(s.type, "vids", 4)) {
                    if (tag(cb + 16, "MJPG") || tag(cb + 16, "mjpg")) s.mjpeg = true;
                    if (!info.width) { info.width = le32(cb + 4); info.height = le32(cb + 8); }
                }
                j += 8 + csize + (csize & 1);
            }
            streams.push_back(s);
        }
        i += 8 + size + (size & 1);
    }
}

}  // namespace

bool parse(const ReadFn &read, uint32_t fileSize, Info &out)
{
    out = Info();
    uint8_t head[12];
    if (fileSize < 12 || !read(0, head, 12) || !tag(head, "RIFF") || !tag(head + 8, "AVI ")) return false;

    std::vector<Stream> streams;
    uint32_t moviTagPos = 0;   // Offset of the "movi" fourcc; idx1 offsets are usually relative to it
    uint32_t idxPos = 0, idxSize = 0;
    uint32_t pos = 12;
    while (pos + 8 <= fileSize) {
        uint8_t h[12];
        if (!read(pos, h, 8)) return false;
        const uint32_t size = le32(h + 4);
        if (tag(h, "LIST")) {
            if (size < 4 || !read(pos + 8, h + 8, 4)) return false;
            if (tag(h + 8, "hdrl")) {
                if (size > MAX_HEADER_BYTES) return false;
                std::vector<uint8_t> buf(size - 4);
                if (!buf.empty() && !read(pos + 12, buf.data(), buf.size())) return false;
                parseHeaders(buf.data(), buf.size(), out, streams);
            } else if (tag(h + 8, "movi")) {
                moviTagPos = pos + 8;
            }
        } else if (tag(h, "idx1")) {
            idxPos = pos + 8;
            idxSize = size;
        }
        const uint64_t next = (uint64_t)pos + 8 + size + (size & 1);
        if (next > fileSize) {
            if (!tag(h, "LIST") && !tag(h, "idx1")) break;
            if (idxSize && idxPos + (uint64_t)idxSize > fileSize) idxSize = fileSize - idxPos;
            break;
        }
        pos = (uint32_t)next;
    }
    if (!moviTagPos || !idxPos || streams.empty()) return false;

    int videoNo = -1, audioNo = -1;
    for (size_t i = 0; i < streams.size(); i++) {
        if (videoNo < 0 && !memcmp(streams[i].type, "vids", 4) && streams[i].mjpeg) videoNo = (int)i;
        if (audioNo < 0 && !memcmp(streams[i].type, "auds", 4) && streams[i].format == 0x55) audioNo = (int)i;
    }
    if (videoNo < 0) return false;
    if (streams[videoNo].scale && streams[videoNo].rate) out.fps = (double)streams[videoNo].rate / streams[videoNo].scale;
    if (out.fps <= 0) return false;

    uint32_t base = UINT32_MAX;   // Added to the idx1 offset to get the chunk header position
    uint32_t audioSum = 0;
    for (uint32_t done = 0; done + 16 <= idxSize;) {
        uint8_t block[16 * 256];
        const size_t take = std::min<size_t>(sizeof(block), (idxSize - done) / 16 * 16);
        if (!read(idxPos + done, block, take)) return false;
        for (size_t k = 0; k < take; k += 16) {
            const uint8_t *e = block + k;
            const uint32_t size = le32(e + 12), off = le32(e + 8);
            if (base == UINT32_MAX) {
                uint8_t id[4];
                if (read(moviTagPos + off, id, 4) && !memcmp(id, e, 4)) base = moviTagPos;
                else if (read(off, id, 4) && !memcmp(id, e, 4)) base = 0;
                else return false;
            }
            const int stream = (e[0] >= '0' && e[0] <= '9' && e[1] >= '0' && e[1] <= '9') ? (e[0] - '0') * 10 + (e[1] - '0') : -1;
            const uint32_t dataOff = base + off + 8;
            if ((uint64_t)dataOff + size > fileSize) continue;
            if (stream == videoNo && (e[2] == 'd') && (e[3] == 'c' || e[3] == 'b')) {
                if (out.video.size() >= MAX_CHUNKS) return false;
                out.video.push_back({dataOff, size});
            } else if (stream == audioNo && e[2] == 'w' && e[3] == 'b' && size) {
                if (out.audio.size() >= MAX_CHUNKS) return false;
                out.audio.push_back({dataOff, size});
                audioSum += size;
                out.audioEnd.push_back(audioSum);
            }
        }
        done += (uint32_t)take;
    }
    return !out.video.empty();
}

size_t readAudio(const Info &info, const ReadFn &read, uint32_t pos, uint8_t *buf, size_t n)
{
    if (pos >= info.audioBytes()) return 0;
    size_t i = std::upper_bound(info.audioEnd.begin(), info.audioEnd.end(), pos) - info.audioEnd.begin();
    size_t copied = 0;
    while (copied < n && i < info.audio.size()) {
        const uint32_t chunkStart = i ? info.audioEnd[i - 1] : 0;
        const uint32_t skip = pos + (uint32_t)copied - chunkStart;
        const size_t take = std::min<size_t>(n - copied, info.audio[i].size - skip);
        if (!read(info.audio[i].off + skip, buf + copied, take)) return copied;
        copied += take;
        i++;
    }
    return copied;
}

}  // namespace SupernovaAvi
