// supernova_stream.cpp - see supernova_stream.h.
#include "supernova_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "my_web.h"
#include "my_wifi.h"
#include "supernova_avi.h"
#include "supernova_library.h"
#include "supernova_lyrics.h"
#include "web_util.h"

namespace SupernovaStream {

namespace {

constexpr size_t AUDIO_PART = 96 * 1024;   // The httpd task is shared; keep each audio response short
constexpr size_t VIDEO_PART = 256 * 1024;
constexpr size_t MAX_FRAME = 256 * 1024;

// The song the browser is playing. The httpd task serves one request at a time, the mutex is for safety.
struct Open {
    std::string folder;
    int variant = -1;
    FILE *avi = nullptr;          // Open MJPEG AVI, null when the song has no AVI video
    uint32_t aviSize = 0;
    SupernovaAvi::Info info;
    std::string audioPath;        // Separate MP3 file; empty = audio comes from the AVI
    uint32_t audioFileSize = 0;
    std::string lyricsPath;
    std::string mp4Path;          // Original video for the browser (played muted; audio comes from audioPath/AVI)
    uint32_t mp4Size = 0;
};
Open s_open;
std::mutex s_mutex;

// Where time goes while serving the browser: SD read vs. WiFi send, per response, since boot.
struct Stat {
    uint32_t n = 0, fail = 0;
    uint64_t bytes = 0;
    uint32_t readMs = 0, readMax = 0, sendMs = 0, sendMax = 0, lastRead = 0, lastSend = 0, lastLen = 0;
};
Stat s_audioStat, s_videoStat;

void note(Stat &s, uint32_t len, int64_t t0, int64_t t1, int64_t t2, bool ok)
{
    const uint32_t rd = (uint32_t)((t1 - t0) / 1000), sd = (uint32_t)((t2 - t1) / 1000);
    s.n++;
    if (!ok) s.fail++;
    s.bytes += len;
    s.readMs += rd;
    s.sendMs += sd;
    s.lastRead = rd;
    s.lastSend = sd;
    s.lastLen = len;
    s.readMax = std::max(s.readMax, rd);
    s.sendMax = std::max(s.sendMax, sd);
}

void appendStat(std::string &o, const char *name, const Stat &s)
{
    char b[256];
    snprintf(b, sizeof(b), "\"%s\":{\"n\":%u,\"fail\":%u,\"kb\":%u,\"read_ms\":%u,\"read_max\":%u,\"send_ms\":%u,\"send_max\":%u,\"last_read\":%u,\"last_send\":%u,\"last_kb\":%u}",
             name, (unsigned)s.n, (unsigned)s.fail, (unsigned)(s.bytes / 1024), (unsigned)s.readMs, (unsigned)s.readMax, (unsigned)s.sendMs, (unsigned)s.sendMax,
             (unsigned)s.lastRead, (unsigned)s.lastSend, (unsigned)(s.lastLen / 1024));
    o += b;
}

void *psAlloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

void closeOpen()
{
    if (s_open.avi) {
        BoardSd::Guard guard;
        fclose(s_open.avi);
    }
    s_open = Open();
}

bool safeFolder(const char *f)
{
    return f[0] && strlen(f) <= 200 && !strpbrk(f, "/\\:") && strcmp(f, ".") && strcmp(f, "..");
}

bool endsWith(const std::string &s, const char *ext)
{
    const size_t n = strlen(ext);
    return s.size() >= n && !strcasecmp(s.c_str() + s.size() - n, ext);
}

bool readAvi(uint32_t off, void *buf, size_t n)
{
    if (!s_open.avi || !BoardSd::transferHealthy()) return false;
    if (fseek(s_open.avi, (long)off, SEEK_SET) != 0) return false;
    return fread(buf, 1, n, s_open.avi) == n;
}

// Opens (or reuses) the song; true when the folder exists and has media.
bool openSong(const char *folder, int variant)
{
    if (s_open.variant == variant && s_open.folder == folder && s_open.variant >= 0) return true;
    closeOpen();
    int found = -1;
    SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) { found = lib.find(folder); });
    if (found < 0) return false;

    SupernovaLibrary::Media media;
    BoardSd::Guard guard;
    if (!BoardSd::mounted() || !SupernovaLibrary::findMedia(SupernovaLibrary::folderPath(folder), media)) return false;
    Open next;
    next.folder = folder;
    next.variant = variant;
    next.lyricsPath = media.lyrics;
    next.audioPath = variant == 1 && !media.instrumental.empty() ? media.instrumental : media.audio;
    if (!next.audioPath.empty()) {
        FILE *f = fopen(next.audioPath.c_str(), "rb");
        if (f && fseek(f, 0, SEEK_END) == 0) {
            const long size = ftell(f);
            if (size > 0) next.audioFileSize = (uint32_t)size;
        }
        if (f) fclose(f);
        if (!next.audioFileSize) next.audioPath.clear();
    }
    if (!media.mp4.empty()) {
        FILE *f = fopen(media.mp4.c_str(), "rb");
        if (f && fseek(f, 0, SEEK_END) == 0) {
            const long size = ftell(f);
            if (size > 0) {
                next.mp4Path = media.mp4;
                next.mp4Size = (uint32_t)size;
            }
        }
        if (f) fclose(f);
    }
    if (endsWith(media.video, ".avi")) {
        FILE *f = fopen(media.video.c_str(), "rb");
        if (f) {
            long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
            s_open = std::move(next);
            s_open.avi = f;
            s_open.aviSize = size > 0 ? (uint32_t)size : 0;
            if (!SupernovaAvi::parse(readAvi, s_open.aviSize, s_open.info)) {
                fclose(f);
                s_open.avi = nullptr;
                s_open.info = SupernovaAvi::Info();
            }
            return true;
        }
    }
    s_open = std::move(next);
    return true;
}

bool songArgs(httpd_req_t *r, char *folder, size_t folderSize, int &variant)
{
    MyWeb::Req q(r);
    char v[4] = "0";
    folder[0] = 0;
    q.get("f", folder, folderSize);
    q.get("v", v, sizeof(v));
    variant = v[0] == '1' ? 1 : 0;
    return safeFolder(folder);
}

esp_err_t mediaGet(httpd_req_t *r)
{
    char folder[208];
    int variant;
    if (!songArgs(r, folder, sizeof(folder), variant)) return MyWeb::sendError(r, 400, "bad song");
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!openSong(folder, variant)) return MyWeb::sendError(r, 404, "song not found");
    const bool video = s_open.avi && !s_open.info.video.empty();
    const bool audio = !s_open.audioPath.empty() || s_open.info.audioBytes() > 0;
    std::string o = "{\"ok\":true,\"video\":";
    o += video ? "true" : "false";
    o += ",\"audio\":";
    o += audio ? "true" : "false";
    o += ",\"mp4\":";
    o += s_open.mp4Size ? "true" : "false";
    o += ",\"hasPlain\":";
    SupernovaLibrary::Media media;
    bool plain = false;
    {
        BoardSd::Guard guard;
        if (BoardSd::mounted() && SupernovaLibrary::findMedia(SupernovaLibrary::folderPath(folder), media))
            plain = !media.instrumental.empty() && !media.audio.empty();
    }
    o += plain ? "true" : "false";
    if (video) {
        char buf[160];
        snprintf(buf, sizeof(buf), ",\"fps\":%.3f,\"frames\":%u,\"w\":%u,\"h\":%u,\"durationMs\":%u",
                 s_open.info.fps, (unsigned)s_open.info.video.size(), (unsigned)s_open.info.width,
                 (unsigned)s_open.info.height, (unsigned)s_open.info.durationMs());
        o += buf;
    }
    o += '}';
    return WebUtil::sendJsonText(r, o);
}

esp_err_t frameGet(httpd_req_t *r)
{
    char folder[208];
    int variant;
    if (!songArgs(r, folder, sizeof(folder), variant)) return MyWeb::sendError(r, 400, "bad song");
    MyWeb::Req q(r);
    long n = 0;
    bool bad = false;
    q.getInt("n", 0, (long)SupernovaAvi::MAX_CHUNKS, n, bad);
    if (bad) return MyWeb::sendError(r, 400, "bad frame");
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!openSong(folder, variant) || !s_open.avi) return MyWeb::sendError(r, 404, "no video");
    if ((size_t)n >= s_open.info.video.size()) return MyWeb::sendError(r, 404, "no such frame");
    const SupernovaAvi::Chunk c = s_open.info.video[(size_t)n];
    if (!c.size || c.size > MAX_FRAME) return MyWeb::sendError(r, 500, "bad frame");
    uint8_t *buf = (uint8_t *)psAlloc(c.size);
    if (!buf) return MyWeb::sendError(r, 500, "out of memory");
    bool ok;
    {
        BoardSd::Guard guard;
        ok = readAvi(c.off, buf, c.size);
    }
    if (!ok) {
        free(buf);
        closeOpen();
        return MyWeb::sendError(r, 503, "SD card read failed");
    }
    httpd_resp_set_type(r, "image/jpeg");
    httpd_resp_set_hdr(r, "Cache-Control", "private, max-age=600");
    const esp_err_t e = httpd_resp_send(r, (const char *)buf, c.size);
    free(buf);
    return e;
}

bool parseRange(const char *h, uint32_t total, uint32_t &from, uint32_t &to)
{
    if (strncmp(h, "bytes=", 6)) return false;
    const char *p = h + 6;
    if (*p == '-') return false;   // Suffix ranges are not needed by browsers for media
    char *end;
    const unsigned long a = strtoul(p, &end, 10);
    if (*end != '-') return false;
    unsigned long b = total - 1;
    if (end[1]) b = strtoul(end + 1, nullptr, 10);
    if (a >= total || b < a) return false;
    from = (uint32_t)a;
    to = (uint32_t)std::min<unsigned long>(b, total - 1);
    return true;
}

esp_err_t audioGet(httpd_req_t *r)
{
    char folder[208];
    int variant;
    if (!songArgs(r, folder, sizeof(folder), variant)) return MyWeb::sendError(r, 400, "bad song");
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!openSong(folder, variant)) return MyWeb::sendError(r, 404, "song not found");
    const bool separate = !s_open.audioPath.empty();
    const uint32_t total = separate ? s_open.audioFileSize : s_open.info.audioBytes();
    if (!total) return MyWeb::sendError(r, 404, "no audio");

    uint32_t from = 0, to = total - 1;
    char hdr[64] = "";
    const bool ranged = httpd_req_get_hdr_value_str(r, "Range", hdr, sizeof(hdr)) == ESP_OK;
    if (ranged && !parseRange(hdr, total, from, to)) {
        char cr[40];
        snprintf(cr, sizeof(cr), "bytes */%u", (unsigned)total);
        httpd_resp_set_status(r, "416 Range Not Satisfiable");
        httpd_resp_set_hdr(r, "Content-Range", cr);
        return httpd_resp_send(r, nullptr, 0);
    }
    to = std::min<uint32_t>(to, from + (uint32_t)AUDIO_PART - 1);
    const size_t len = (size_t)(to - from + 1);
    uint8_t *buf = (uint8_t *)psAlloc(len);
    if (!buf) return MyWeb::sendError(r, 500, "out of memory");
    size_t got = 0;
    const int64_t t0 = esp_timer_get_time();
    {
        BoardSd::Guard guard;
        if (BoardSd::transferHealthy()) {
            if (separate) {
                FILE *f = fopen(s_open.audioPath.c_str(), "rb");
                if (f) {
                    if (fseek(f, (long)from, SEEK_SET) == 0) got = fread(buf, 1, len, f);
                    fclose(f);
                }
            } else {
                got = SupernovaAvi::readAudio(s_open.info, readAvi, from, buf, len);
            }
        }
    }
    const int64_t t1 = esp_timer_get_time();
    if (got != len) {
        note(s_audioStat, 0, t0, t1, t1, false);
        free(buf);
        return MyWeb::sendError(r, 503, "SD card read failed");
    }
    httpd_resp_set_type(r, "audio/mpeg");
    httpd_resp_set_hdr(r, "Accept-Ranges", "bytes");
    if (ranged) {
        char cr[64];
        snprintf(cr, sizeof(cr), "bytes %u-%u/%u", (unsigned)from, (unsigned)to, (unsigned)total);
        httpd_resp_set_status(r, "206 Partial Content");
        httpd_resp_set_hdr(r, "Content-Range", cr);
    }
    const esp_err_t e = httpd_resp_send(r, (const char *)buf, (ssize_t)len);
    note(s_audioStat, (uint32_t)len, t0, t1, esp_timer_get_time(), e == ESP_OK);
    free(buf);
    return e;
}

esp_err_t videoGet(httpd_req_t *r)
{
    char folder[208];
    int variant;
    if (!songArgs(r, folder, sizeof(folder), variant)) return MyWeb::sendError(r, 400, "bad song");
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!openSong(folder, variant)) return MyWeb::sendError(r, 404, "song not found");
    const uint32_t total = s_open.mp4Size;
    if (!total) return MyWeb::sendError(r, 404, "no mp4");

    uint32_t from = 0, to = total - 1;
    char hdr[64] = "";
    const bool ranged = httpd_req_get_hdr_value_str(r, "Range", hdr, sizeof(hdr)) == ESP_OK;
    if (ranged && !parseRange(hdr, total, from, to)) {
        char cr[40];
        snprintf(cr, sizeof(cr), "bytes */%u", (unsigned)total);
        httpd_resp_set_status(r, "416 Range Not Satisfiable");
        httpd_resp_set_hdr(r, "Content-Range", cr);
        return httpd_resp_send(r, nullptr, 0);
    }
    to = std::min<uint32_t>(to, from + (uint32_t)VIDEO_PART - 1);
    const size_t len = (size_t)(to - from + 1);
    uint8_t *buf = (uint8_t *)psAlloc(len);
    if (!buf) return MyWeb::sendError(r, 500, "out of memory");
    size_t got = 0;
    const int64_t t0 = esp_timer_get_time();
    {
        BoardSd::Guard guard;
        if (BoardSd::transferHealthy()) {
            FILE *f = fopen(s_open.mp4Path.c_str(), "rb");
            if (f) {
                if (fseek(f, (long)from, SEEK_SET) == 0) got = fread(buf, 1, len, f);
                fclose(f);
            }
        }
    }
    const int64_t t1 = esp_timer_get_time();
    if (got != len) {
        note(s_videoStat, 0, t0, t1, t1, false);
        free(buf);
        return MyWeb::sendError(r, 503, "SD card read failed");
    }
    httpd_resp_set_type(r, "video/mp4");
    httpd_resp_set_hdr(r, "Accept-Ranges", "bytes");
    if (ranged) {
        char cr[64];
        snprintf(cr, sizeof(cr), "bytes %u-%u/%u", (unsigned)from, (unsigned)to, (unsigned)total);
        httpd_resp_set_status(r, "206 Partial Content");
        httpd_resp_set_hdr(r, "Content-Range", cr);
    }
    const esp_err_t e = httpd_resp_send(r, (const char *)buf, (ssize_t)len);
    note(s_videoStat, (uint32_t)len, t0, t1, esp_timer_get_time(), e == ESP_OK);
    free(buf);
    return e;
}

esp_err_t diagGet(httpd_req_t *r)
{
    std::string o = "{";
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        appendStat(o, "audio", s_audioStat);
        o += ',';
        appendStat(o, "video", s_videoStat);
        o += ",\"mp4_kb\":" + std::to_string(s_open.mp4Size / 1024);
    }
    const MyWifi::Info wi = MyWifi::info();
    char b[160];
    snprintf(b, sizeof(b), ",\"rssi\":%d,\"channel\":%u,\"wifi_up_s\":%u,\"disconnects\":%u,\"roams\":%u,\"sd_khz\":%u}", wi.rssi,
             (unsigned)wi.channel, (unsigned)wi.uptimeS, (unsigned)wi.disconnects, (unsigned)wi.roams, (unsigned)BoardSd::info().freqKhz);
    o += b;
    return WebUtil::sendJsonText(r, o);
}

esp_err_t lyricsGet(httpd_req_t *r)
{
    char folder[208];
    int variant;
    if (!songArgs(r, folder, sizeof(folder), variant)) return MyWeb::sendError(r, 400, "bad song");
    std::string path;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!openSong(folder, variant)) return MyWeb::sendError(r, 404, "song not found");
        path = s_open.lyricsPath;
    }
    std::vector<char> data;
    {
        BoardSd::Guard guard;
        FILE *f = BoardSd::mounted() ? fopen(path.c_str(), "rb") : nullptr;
        if (!f) return MyWeb::sendError(r, 404, "no lyrics");
        if (fseek(f, 0, SEEK_END) == 0) {
            const long size = ftell(f);
            if (size > 0 && (size_t)size <= SupernovaLyrics::MAX_FILE_BYTES && fseek(f, 0, SEEK_SET) == 0) {
                data.resize((size_t)size);
                if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
            }
        }
        fclose(f);
    }
    SupernovaLyrics::Document doc;
    if (data.empty() || SupernovaLyrics::parse(data.data(), data.size(), doc) != SupernovaLyrics::Err::OK)
        return MyWeb::sendError(r, 422, "bad lyrics");

    std::string o;
    o.reserve(16 * 1024);
    o += "{\"ok\":true,\"title\":";
    WebUtil::appendStr(o, doc.title.c_str());
    o += ",\"artist\":";
    WebUtil::appendStr(o, doc.artist.c_str());
    o += ",\"start\":";
    WebUtil::appendNum(o, doc.startMs);
    o += ",\"end\":";
    WebUtil::appendNum(o, doc.endMs);
    o += ",\"vgap\":";
    o += std::to_string(doc.videoGapMs);
    if (doc.duet) {
        o += ",\"p1\":";
        WebUtil::appendStr(o, doc.p1.c_str());
        o += ",\"p2\":";
        WebUtil::appendStr(o, doc.p2.c_str());
    }
    o += ",\"phrases\":[";
    for (size_t i = 0; i < doc.phrases.size(); i++) {
        const SupernovaLyrics::Phrase &p = doc.phrases[i];
        if (i) o += ',';
        o += "{\"s\":";
        WebUtil::appendNum(o, p.startMs);
        o += ",\"e\":";
        WebUtil::appendNum(o, p.endMs);
        if (p.singer) {
            o += ",\"p\":";
            WebUtil::appendNum(o, p.singer);
        }
        o += ",\"n\":[";
        for (size_t k = 0; k < p.notes.size(); k++) {
            if (k) o += ',';
            o += '[';
            WebUtil::appendNum(o, p.notes[k].startMs);
            o += ',';
            WebUtil::appendStr(o, SupernovaLyrics::toDisplayText(p.notes[k].text).c_str());
            o += ']';
        }
        o += "]}";
    }
    o += "]}";
    return WebUtil::sendJsonText(r, o);
}

}  // namespace

bool registerRoutes()
{
    bool ok = MyWeb::on("/api/supernova/media", HTTP_GET, mediaGet);
    ok &= MyWeb::on("/api/supernova/frame", HTTP_GET, frameGet);
    ok &= MyWeb::on("/api/supernova/audio", HTTP_GET, audioGet);
    ok &= MyWeb::on("/api/supernova/video", HTTP_GET, videoGet);
    ok &= MyWeb::on("/api/supernova/lyrics", HTTP_GET, lyricsGet);
    ok &= MyWeb::on("/api/supernova/diag", HTTP_GET, diagGet);
    return ok;
}

void release()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    closeOpen();
}

}  // namespace SupernovaStream
