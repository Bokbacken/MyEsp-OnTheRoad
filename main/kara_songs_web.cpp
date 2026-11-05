// kara_songs_web.cpp - see kara_songs_web.h.
#include "kara_songs_web.h"

#include <string.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "karaoke.h"
#include "my_log.h"
#include "my_lvgl.h"
#include "my_web.h"
#include "web_util.h"

namespace KaraSongsWeb {

namespace {

constexpr long DEF_LIMIT = 200, MAX_LIMIT = 500;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

const char *stringField(const cJSON *j, const char *name)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(j, name);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

bool numberField(const cJSON *j, const char *name, double max, uint32_t &out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(j, name);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < 0 || value->valuedouble > max ||
        std::floor(value->valuedouble) != value->valuedouble) return false;
    out = (uint32_t)value->valuedouble;
    return true;
}

esp_err_t result(httpd_req_t *r, Karaoke::Err err)
{
    if (err == Karaoke::Err::OK) return MyWeb::sendOk(r);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(o, "error", Karaoke::errText(err));
    if (err == Karaoke::Err::UNSAVED) {
        cJSON_AddStringToObject(o, "code", "unsaved");
        cJSON_AddStringToObject(o, "current", Karaoke::current().path.c_str());
    }
    return MyWeb::sendJson(r, err == Karaoke::Err::BUSY || err == Karaoke::Err::UNSAVED ? 409 :
                          err == Karaoke::Err::SAVE_FAILED || err == Karaoke::Err::NO_SD ? 503 :
                          err == Karaoke::Err::NO_FILE ? 404 : 400, o);
}

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
    char allBuf[8] = "";
    q.get("dir", dirBuf, sizeof(dirBuf));
    q.get("q", qBuf, sizeof(qBuf));
    q.get("all", allBuf, sizeof(allBuf));
    const bool all = strcmp(allBuf, "1") == 0;
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
            if (!all) {
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
            }
            songs = lib.songsIn(dir, all);
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
    o += ",\"dirty\":";
    o += Karaoke::songSettingsDirty() ? "true" : "false";
    o += '}';
    return WebUtil::sendJsonText(r, o);
}

esp_err_t openPost(httpd_req_t *r)
{
    char path[KaraIndex::MAX_PATH + 32] = "";
    std::string decision, expected;
    if (cJSON *j = WebUtil::readJson(r, 1024)) {
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "path");
        if (cJSON_IsString(p) && p->valuestring) snprintf(path, sizeof(path), "%s", p->valuestring);
        decision = stringField(j, "decision");
        expected = stringField(j, "current");
        cJSON_Delete(j);
    } else {
        MyWeb::Req q(r);   // Also accept ?path= / a form post
        if (!q.get("path", path, sizeof(path))) return MyWeb::sendError(r, 400, "bad path");
    }
    if (!path[0]) return MyWeb::sendError(r, 400, "path missing");
    Karaoke::Err e;
    {
        // The device UI reads the open song from the LVGL task: open it while LVGL is paused
        LvLock l(3000);
        if (!l) return MyWeb::sendError(r, 503, "busy");
        if (!decision.empty()) {
            if (decision != "save" && decision != "discard") return MyWeb::sendError(r, 400, "bad decision");
            if (expected != Karaoke::current().path) return MyWeb::sendError(r, 409, "song changed");
            if (Karaoke::fontState() == Karaoke::FontState::LOADING) return result(r, Karaoke::Err::BUSY);
            if (decision == "save") {
                e = Karaoke::saveSongSettings();
                if (e != Karaoke::Err::OK) return result(r, e);
            }
            Karaoke::close();
        }
        e = Karaoke::openByPath(path);
    }
    if (e != Karaoke::Err::OK) {
        return result(r, e);
    }
    MyLog::event("karaoke", "web open %s", path);
    return MyWeb::sendOk(r);
}

esp_err_t playlistsGet(httpd_req_t *r)
{
    MyWeb::Req q(r);
    long id = 0, offset = 0;
    bool bad = false;
    q.getInt("id", 1, UINT16_MAX, id, bad);
    q.getInt("offset", 0, UINT16_MAX, offset, bad);
    if (bad) return MyWeb::sendError(r, 400, "bad id/offset");
    std::vector<Karaoke::PlaylistView> lists;
    const auto err = Karaoke::playlists(lists, (uint16_t)id, (size_t)offset);
    if (err != Karaoke::Err::OK) return result(r, err);
    if (id && lists.empty()) return MyWeb::sendError(r, 404, "playlist missing");
    std::string o = "{\"ok\":true,\"playlists\":[";
    bool firstList = true;
    for (const auto &list : lists) {
        if (!firstList) o += ',';
        firstList = false;
        o += "{\"id\":";
        WebUtil::appendNum(o, list.id);
        o += ",\"name\":";
        WebUtil::appendStr(o, list.name.c_str());
        o += ",\"count\":";
        WebUtil::appendNum(o, list.count);
        o += ",\"songs\":[";
        bool firstSong = true;
        for (const auto &song : list.songs) {
            if (!firstSong) o += ',';
            firstSong = false;
            o += "{\"crc\":";
            WebUtil::appendNum(o, song.crc);
            o += ",\"size\":";
            WebUtil::appendNum(o, song.size);
            o += ",\"path\":";
            WebUtil::appendStr(o, song.path.c_str());
            std::string title, artist;
            Midi::nameFromFile(song.path, title, artist);
            o += ",\"title\":";
            WebUtil::appendStr(o, title.c_str());
            o += ",\"artist\":";
            WebUtil::appendStr(o, artist.c_str());
            o += '}';
        }
        o += "]}";
    }
    o += "]}";
    return WebUtil::sendJsonText(r, o);
}

esp_err_t playlistsPost(httpd_req_t *r)
{
    Json j(WebUtil::readJson(r, 2048), cJSON_Delete);
    if (!j || !cJSON_IsObject(j.get())) return MyWeb::sendError(r, 400, "bad JSON");
    const char *action = stringField(j.get(), "action");
    uint32_t id = 0, crc = 0, size = 0;
    if (strcmp(action, "create") && (!numberField(j.get(), "id", UINT16_MAX, id) || !id))
        return MyWeb::sendError(r, 400, "bad id");
    if ((!strcmp(action, "remove") || !strcmp(action, "up") || !strcmp(action, "down")) &&
        (!numberField(j.get(), "crc", UINT32_MAX, crc) || !numberField(j.get(), "size", Midi::MAX_FILE, size)))
        return MyWeb::sendError(r, 400, "bad song key");
    uint16_t listId = (uint16_t)id;
    const auto err = Karaoke::editPlaylist(action, listId, stringField(j.get(), "name"),
        stringField(j.get(), "path"), crc, size);
    if (err != Karaoke::Err::OK) return result(r, err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "id", listId);
    return MyWeb::sendJson(r, 200, o);
}

esp_err_t songGet(httpd_req_t *r)
{
    char path[KaraIndex::MAX_PATH + 1] = "";
    MyWeb::Req q(r);
    if (!q.get("path", path, sizeof(path))) return MyWeb::sendError(r, 400, "bad path");
    std::string font;
    const auto err = Karaoke::songFontByPath(path, font);
    if (err != Karaoke::Err::OK) return result(r, err);
    std::vector<Karaoke::FontFile> fonts;
    const auto fontErr = Karaoke::scanFonts(fonts);
    if (fontErr != Karaoke::Err::OK) return result(r, fontErr);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "sf2", font.c_str());
    cJSON *array = cJSON_AddArrayToObject(o, "fonts");
    for (const auto &f : fonts)
        if (f.err == Karaoke::Err::OK) cJSON_AddItemToArray(array, cJSON_CreateString(f.name.c_str()));
    return MyWeb::sendJson(r, 200, o);
}

esp_err_t songPost(httpd_req_t *r)
{
    Json j(WebUtil::readJson(r, 2048), cJSON_Delete);
    if (!j || !cJSON_IsObject(j.get())) return MyWeb::sendError(r, 400, "bad JSON");
    const char *action = stringField(j.get(), "action");
    LvLock lock(3000);
    if (!lock) return MyWeb::sendError(r, 503, "busy");
    if (!strcmp(action, "font")) {
        const cJSON *font = cJSON_GetObjectItemCaseSensitive(j.get(), "sf2");
        if (!cJSON_IsString(font)) return MyWeb::sendError(r, 400, "sf2 missing");
        return result(r, Karaoke::saveSongFontByPath(stringField(j.get(), "path"), font->valuestring));
    }
    if (Karaoke::current().path != stringField(j.get(), "current"))
        return MyWeb::sendError(r, 409, "song changed");
    if (!strcmp(action, "save")) return result(r, Karaoke::saveSongSettings());
    if (!strcmp(action, "discard")) return result(r, Karaoke::discardSongSettings());
    return MyWeb::sendError(r, 400, "bad action");
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
    ok &= MyWeb::on("/api/karaoke/playlists", HTTP_GET, playlistsGet);
    ok &= MyWeb::on("/api/karaoke/playlists", HTTP_POST, playlistsPost);
    ok &= MyWeb::on("/api/karaoke/song", HTTP_GET, songGet);
    ok &= MyWeb::on("/api/karaoke/song", HTTP_POST, songPost);
    return ok;
}

}  // namespace KaraSongsWeb
