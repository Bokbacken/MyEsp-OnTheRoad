// kara_songs_web.cpp - see kara_songs_web.h.
#include "kara_songs_web.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "karaoke.h"
#include "my_log.h"
#include "my_lvgl.h"
#include "my_web.h"
#include "web_util.h"

namespace KaraSongsWeb {

namespace {

constexpr long DEF_LIMIT = 200, MAX_LIMIT = 500;

// The library is loaded when the karaoke menu opens; the picker may be used before that.
void ensureLibrary()
{
    if (Karaoke::libraryLoaded() || Karaoke::scanning()) return;
    if (!Karaoke::loadIndex()) Karaoke::rescanAsync();
}

esp_err_t songsGet(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char dirBuf[KaraIndex::MAX_PATH + 1] = "", qBuf[96] = "";
    q.get("dir", dirBuf, sizeof(dirBuf));
    q.get("q", qBuf, sizeof(qBuf));
    long offset = 0, limit = DEF_LIMIT;
    bool bad = false;
    q.getInt("offset", 0, (long)KaraIndex::MAX_SONGS, offset, bad);
    q.getInt("limit", 1, MAX_LIMIT, limit, bad);
    if (bad) return MyWeb::sendError(r, 400, "bad offset/limit");
    std::string dir = dirBuf;
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    while (!dir.empty() && dir[0] == '/') dir.erase(0, 1);
    if (!dir.empty() && !KaraIndex::safeRel(dir.c_str())) return MyWeb::sendError(r, 400, "bad dir");
    ensureLibrary();

    const Karaoke::SongFile cur = Karaoke::current();
    std::string o;
    o.reserve(16384);
    o += "{\"ok\":true,\"scanning\":";
    o += Karaoke::scanning() ? "true" : "false";
    o += ",\"error\":";
    WebUtil::appendStr(o, Karaoke::libraryError() == Karaoke::Err::OK ? "" : Karaoke::errText(Karaoke::libraryError()));
    o += ",\"dir\":";
    WebUtil::appendStr(o, dir.c_str());
    o += ",\"q\":";
    WebUtil::appendStr(o, qBuf);
    Karaoke::withLibrary([&](const KaraIndex::Library &lib, uint32_t) {
        o += ",\"total\":";
        WebUtil::appendNum(o, lib.size());
        o += ",\"limitReached\":";
        o += lib.full() ? "true" : "false";
        o += ",\"dirs\":[";
        std::vector<uint16_t> songs;
        if (qBuf[0]) {
            songs = lib.search(qBuf);
        } else {
            bool first = true;
            for (const std::string &d : lib.subdirs(dir)) {
                const std::string p = dir.empty() ? d : dir + "/" + d;
                if (!first) o += ',';
                first = false;
                o += "{\"name\":";
                WebUtil::appendStr(o, d.c_str());
                o += ",\"path\":";
                WebUtil::appendStr(o, p.c_str());
                o += ",\"count\":";
                WebUtil::appendNum(o, lib.songsIn(p).size());
                o += '}';
            }
            songs = lib.songsIn(dir);
        }
        o += "],\"count\":";
        WebUtil::appendNum(o, songs.size());
        o += ",\"offset\":";
        WebUtil::appendNum(o, (unsigned long long)offset);
        o += ",\"songs\":[";
        const size_t to = std::min(songs.size(), (size_t)(offset + limit));
        for (size_t k = (size_t)offset; k < to; k++) {
            const uint16_t i = songs[k];
            if (k != (size_t)offset) o += ',';
            o += "{\"path\":";
            WebUtil::appendStr(o, lib.rel(i));
            o += ",\"title\":";
            WebUtil::appendStr(o, lib.title(i));
            o += ",\"artist\":";
            WebUtil::appendStr(o, lib.artist(i));
            o += ",\"size\":";
            WebUtil::appendNum(o, lib.fileSize(i));
            o += ",\"dir\":";
            WebUtil::appendStr(o, lib.dir(i).c_str());
            o += '}';
        }
        o += ']';
    });
    const std::string root = std::string(Karaoke::DIR_SONGS) + "/";
    o += ",\"current\":";
    WebUtil::appendStr(o, cur.path.compare(0, root.size(), root) == 0 ? cur.path.c_str() + root.size() : "");
    o += ",\"playing\":";
    o += Karaoke::playing() ? "true" : "false";
    o += '}';
    return WebUtil::sendJsonText(r, o);
}

esp_err_t openPost(httpd_req_t *r)
{
    char path[KaraIndex::MAX_PATH + 32] = "";
    if (cJSON *j = WebUtil::readJson(r, 1024)) {
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "path");
        if (cJSON_IsString(p) && p->valuestring) snprintf(path, sizeof(path), "%s", p->valuestring);
        cJSON_Delete(j);
    } else {
        MyWeb::Req q(r);   // Also accept ?path= / a form post
        q.get("path", path, sizeof(path));
    }
    if (!path[0]) return MyWeb::sendError(r, 400, "path missing");
    Karaoke::Err e;
    {
        // The device UI reads the open song from the LVGL task: open it while LVGL is paused
        LvLock l(3000);
        if (!l) return MyWeb::sendError(r, 503, "busy");
        e = Karaoke::openByPath(path);
    }
    if (e != Karaoke::Err::OK) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", false);
        cJSON_AddStringToObject(o, "error", Karaoke::errText(e));
        return MyWeb::sendJson(r, e == Karaoke::Err::BUSY ? 409 : e == Karaoke::Err::NO_FILE ? 404 : 422, o);
    }
    MyLog::event("karaoke", "web open %s", path);
    return MyWeb::sendOk(r);
}

esp_err_t rescanPost(httpd_req_t *r)
{
    Karaoke::rescanAsync();
    return MyWeb::sendOk(r);
}

}  // namespace

bool registerRoutes()
{
    bool ok = MyWeb::on("/api/karaoke/songs", HTTP_GET, songsGet);
    ok &= MyWeb::on("/api/karaoke/open", HTTP_POST, openPost);
    ok &= MyWeb::on("/api/karaoke/rescan", HTTP_POST, rescanPost);
    return ok;
}

}  // namespace KaraSongsWeb
