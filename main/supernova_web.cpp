// supernova_web.cpp - see supernova_web.h.
#include "supernova_web.h"

#include <string.h>

#include <algorithm>
#include <memory>
#include <string>

#include "my_web.h"
#include "supernova_library.h"
#include "web_util.h"

namespace SupernovaWeb {

namespace {

constexpr long DEF_LIMIT = 100, MAX_LIMIT = 200;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using SupernovaPlaylists::Err;

const char *stringField(const cJSON *j, const char *name)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(j, name);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

const char *errText(Err e)
{
    switch (e) {
    case Err::BAD_NAME: return "bad name (1-48 characters)";
    case Err::BAD_FOLDER: return "bad song";
    case Err::NO_LIST: return "playlist missing";
    case Err::NO_SONG: return "song not in playlist";
    case Err::FULL: return "playlist limit reached";
    case Err::DUPLICATE_NAME: return "name already used";
    default: return "error";
    }
}

// The cache is loaded on first use; the page may be opened before the device menu.
void ensureLibrary()
{
    if (SupernovaLibrary::generation() == 0 && !SupernovaLibrary::scanning()) SupernovaLibrary::start(false);
}

esp_err_t songsGet(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char qBuf[96] = "", sortBuf[12] = "";
    q.get("q", qBuf, sizeof(qBuf));
    q.get("sort", sortBuf, sizeof(sortBuf));
    long offset = 0, limit = DEF_LIMIT;
    bool bad = false;
    q.getInt("offset", 0, (long)SupernovaIndex::MAX_SONGS, offset, bad);
    q.getInt("limit", 1, MAX_LIMIT, limit, bad);
    if (bad) return MyWeb::sendError(r, 400, "bad offset/limit");
    SupernovaIndex::Query query;
    query.text = qBuf;
    query.sort = !strcmp(sortBuf, "title") ? SupernovaIndex::Sort::TITLE :
                 !strcmp(sortBuf, "year") ? SupernovaIndex::Sort::YEAR : SupernovaIndex::Sort::ARTIST;
    ensureLibrary();

    std::string o;
    o.reserve(8192);
    o += "{\"ok\":true,\"scanning\":";
    o += SupernovaLibrary::scanning() ? "true" : "false";
    SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) {
        const std::vector<uint16_t> view = lib.view(query);
        o += ",\"total\":";
        WebUtil::appendNum(o, lib.size());
        o += ",\"count\":";
        WebUtil::appendNum(o, view.size());
        o += ",\"offset\":";
        WebUtil::appendNum(o, (unsigned long long)offset);
        o += ",\"songs\":[";
        const size_t to = std::min(view.size(), (size_t)(offset + limit));
        for (size_t k = (size_t)offset; k < to; k++) {
            const uint16_t i = view[k];
            if (k != (size_t)offset) o += ',';
            o += "{\"folder\":";
            WebUtil::appendStr(o, lib.folder(i));
            o += ",\"artist\":";
            WebUtil::appendStr(o, lib.artist(i));
            o += ",\"title\":";
            WebUtil::appendStr(o, lib.title(i));
            o += ",\"year\":";
            WebUtil::appendNum(o, lib.year(i));
            o += ",\"genre\":";
            WebUtil::appendStr(o, lib.genre(i));
            o += '}';
        }
        o += ']';
    });
    o += '}';
    return WebUtil::sendJsonText(r, o);
}

esp_err_t playlistsGet(httpd_req_t *r)
{
    MyWeb::Req q(r);
    long id = 0, offset = 0, limit = DEF_LIMIT;
    bool bad = false;
    q.getInt("id", 1, UINT16_MAX, id, bad);
    q.getInt("offset", 0, (long)SupernovaPlaylists::MAX_SONGS, offset, bad);
    q.getInt("limit", 1, MAX_LIMIT, limit, bad);
    if (bad) return MyWeb::sendError(r, 400, "bad id/offset");
    ensureLibrary();

    std::string o = "{\"ok\":true,\"playlists\":[";
    bool found = id == 0;
    SupernovaLibrary::readPlaylists([&](const SupernovaPlaylists::Store &store) {
        bool first = true;
        for (const SupernovaPlaylists::Playlist &p : store.lists()) {
            if (id && p.id != id) continue;
            found = true;
            if (!first) o += ',';
            first = false;
            o += "{\"id\":";
            WebUtil::appendNum(o, p.id);
            o += ",\"name\":";
            WebUtil::appendStr(o, p.name.c_str());
            o += ",\"count\":";
            WebUtil::appendNum(o, p.folders.size());
            if (id) {
                o += ",\"songs\":[";
                const size_t to = std::min(p.folders.size(), (size_t)(offset + limit));
                SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) {
                    for (size_t k = (size_t)offset; k < to; k++) {
                        const int i = lib.find(p.folders[k].c_str());
                        if (k != (size_t)offset) o += ',';
                        o += "{\"folder\":";
                        WebUtil::appendStr(o, p.folders[k].c_str());
                        o += ",\"artist\":";
                        WebUtil::appendStr(o, i >= 0 ? lib.artist((size_t)i) : "");
                        o += ",\"title\":";
                        WebUtil::appendStr(o, i >= 0 ? lib.title((size_t)i) : p.folders[k].c_str());
                        o += ",\"missing\":";
                        o += i >= 0 ? "false" : "true";
                        o += '}';
                    }
                });
                o += ']';
            }
            o += '}';
        }
    });
    o += "]}";
    if (!found) return MyWeb::sendError(r, 404, "playlist missing");
    return WebUtil::sendJsonText(r, o);
}

esp_err_t playlistsPost(httpd_req_t *r)
{
    Json j(WebUtil::readJson(r, 2048), cJSON_Delete);
    if (!j || !cJSON_IsObject(j.get())) return MyWeb::sendError(r, 400, "bad JSON");
    const std::string action = stringField(j.get(), "action");
    const cJSON *idItem = cJSON_GetObjectItemCaseSensitive(j.get(), "id");
    const double idValue = cJSON_IsNumber(idItem) ? idItem->valuedouble : 0;
    const uint16_t listId = idValue >= 1 && idValue <= UINT16_MAX ? (uint16_t)idValue : 0;
    const std::string name = stringField(j.get(), "name"), folder = stringField(j.get(), "folder");
    static const char *const ACTIONS[] = {"create", "rename", "delete", "add", "remove", "up", "down"};
    if (std::none_of(std::begin(ACTIONS), std::end(ACTIONS), [&](const char *a) { return action == a; }))
        return MyWeb::sendError(r, 400, "bad action");
    if (action != "create" && !listId) return MyWeb::sendError(r, 400, "bad id");

    Err err = Err::OK;
    uint16_t resultId = listId;
    bool saved = true;
    SupernovaLibrary::editPlaylists([&](SupernovaPlaylists::Store &s) {
        if (action == "create") err = s.create(name, resultId);
        else if (action == "rename") err = s.rename(listId, name);
        else if (action == "delete") err = s.remove(listId);
        else if (action == "add") err = s.add(listId, folder);
        else if (action == "remove") err = s.removeSong(listId, folder);
        else err = s.move(listId, folder, action == "up" ? -1 : 1);
        return err == Err::OK;
    }, saved);
    if (err != Err::OK) return MyWeb::sendError(r, err == Err::NO_LIST ? 404 : 400, errText(err));
    if (!saved) return MyWeb::sendError(r, 503, "could not save to the SD card");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "id", resultId);
    return MyWeb::sendJson(r, 200, o);
}

}  // namespace

bool registerRoutes()
{
    bool ok = MyWeb::on("/api/supernova/songs", HTTP_GET, songsGet);
    ok &= MyWeb::on("/api/supernova/playlists", HTTP_GET, playlistsGet);
    ok &= MyWeb::on("/api/supernova/playlists", HTTP_POST, playlistsPost);
    return ok;
}

}  // namespace SupernovaWeb
