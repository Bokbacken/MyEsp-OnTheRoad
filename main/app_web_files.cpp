// app_web_files.cpp - /api/fs/* : file browser for the microSD card (/sdcard) and the internal
// Flash-FAT (/ffat). Every path goes through FsPath::resolve(); changing requests need the admin
// PIN. SD access takes the BoardSd lock per operation / per chunk so music streaming keeps going.
//
//   GET  /api/fs/list?root=sd|ffat&path=/dir        {ok, root, path, mounted, total, free, entries[]}
//   GET  /api/fs/download?root=&path=/dir/file      file as attachment
//   POST /api/fs/upload?root=&path=/dir/file[&overwrite=1]&pin=   raw body (streamed, tmp + rename)
//   POST /api/fs/delete   root, path, pin           file or empty directory
//   POST /api/fs/rename   root, path, name, pin     new name in the same directory
//   POST /api/fs/mkdir    root, path, pin           creates one directory
//   POST /api/sd/remount  pin                       unmount + mount the card again
#include "app_web_files.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_audio.h"
#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fs_path.h"
#include "lang.h"
#include "my_log.h"
#include "my_storage.h"
#include "my_web.h"

static const char *TAG = "webfs";

namespace AppWebFiles {

static constexpr size_t PATH_CAP = 300;
static constexpr size_t CHUNK = 8192;
static constexpr int MAX_ENTRIES = 400;

namespace {

// Optional SD lock: only the card is shared with the audio mixer.
struct Lock {
    bool sd;
    explicit Lock(bool isSd) : sd(isSd) { if (sd) BoardSd::lock(); }
    ~Lock() { if (sd) BoardSd::unlock(); }
};

struct Target {
    bool sd;
    const char *root;
    char path[PATH_CAP];
};

void *psAlloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

bool rootMounted(bool sd) { return sd ? BoardSd::mounted() : MyFat::mounted(); }

// Parses root + path. Sends the error answer itself and returns false on failure.
bool target(const MyWeb::Req &q, Target &t, esp_err_t &res, const char *param = "path")
{
    char root[8] = "sd";
    if (q.has("root") && !q.get("root", root, sizeof(root))) root[0] = 0;
    if (!strcmp(root, "sd")) t.sd = true;
    else if (!strcmp(root, "ffat")) t.sd = false;
    else {
        res = MyWeb::sendError(q.raw(), 400, tr(Str::WEB_ERR_FS_PATH));
        return false;
    }
    t.root = t.sd ? BoardSd::MOUNT : MyFat::basePath();
    char user[PATH_CAP] = "";
    if (q.has(param) && !q.get(param, user, sizeof(user))) {
        res = MyWeb::sendError(q.raw(), 400, tr(Str::WEB_ERR_FS_PATH));
        return false;
    }
    if (!FsPath::resolve(t.root, user, t.path, sizeof(t.path))) {
        res = MyWeb::sendError(q.raw(), 400, tr(Str::WEB_ERR_FS_PATH));
        return false;
    }
    if (!rootMounted(t.sd)) {
        res = MyWeb::sendError(q.raw(), 503, tr(Str::WEB_ERR_FS_NOCARD));
        return false;
    }
    return true;
}

// Path relative to the root for answers ("/" for the root itself).
const char *relPath(const Target &t)
{
    const char *p = t.path + strlen(t.root);
    return *p ? p : "/";
}

const char *baseName(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

void changed(const Target &t)
{
    if (!t.sd) return;
    BoardSd::refreshUsage();
    if (!strncmp(t.path, "/sdcard/otr/", 12)) Audio::invalidateFiles();
}

esp_err_t ioError(httpd_req_t *r, int err)
{
    if (err == ENOENT) return MyWeb::sendError(r, 404, tr(Str::WEB_ERR_FS_NOTFOUND));
    if (err == EEXIST) return MyWeb::sendError(r, 409, tr(Str::WEB_ERR_FS_EXISTS));
    if (err == ENOTEMPTY) return MyWeb::sendError(r, 409, tr(Str::WEB_ERR_FS_NOTEMPTY));
    if (err == ENOSPC) return MyWeb::sendError(r, 507, tr(Str::WEB_ERR_FS_FULL));
    return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_FS_IO));
}

void addUsage(cJSON *o, bool sd)
{
    if (sd) {
        const BoardSd::Info i = BoardSd::info();
        cJSON_AddNumberToObject(o, "total", (double)i.totalBytes);
        cJSON_AddNumberToObject(o, "free", (double)i.freeBytes);
    } else {
        const uint64_t tot = MyFat::totalBytes(), used = MyFat::usedBytes();
        cJSON_AddNumberToObject(o, "total", (double)tot);
        cJSON_AddNumberToObject(o, "free", (double)(tot > used ? tot - used : 0));
    }
}

}  // namespace

// ----------------------------------------------------------------------------------------
static esp_err_t handleList(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char root[8] = "sd";
    if (q.has("root")) q.get("root", root, sizeof(root));
    const bool sd = strcmp(root, "ffat") != 0;
    if (!rootMounted(sd)) {
        // Not an error for the page: it shows "no card" and the remount button.
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", true);
        cJSON_AddStringToObject(o, "root", sd ? "sd" : "ffat");
        cJSON_AddStringToObject(o, "path", "/");
        cJSON_AddBoolToObject(o, "mounted", false);
        cJSON_AddArrayToObject(o, "entries");
        return MyWeb::sendJson(r, 200, o);
    }
    Target *t = (Target *)psAlloc(sizeof(Target));
    if (!t) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    esp_err_t res = ESP_OK;
    if (!target(q, *t, res)) {
        free(t);
        return res;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "root", t->sd ? "sd" : "ffat");
    cJSON_AddStringToObject(o, "path", relPath(*t));
    cJSON_AddBoolToObject(o, "mounted", true);
    addUsage(o, t->sd);
    cJSON *arr = cJSON_AddArrayToObject(o, "entries");
    int err = 0, n = 0;
    bool truncated = false;
    {
        Lock lk(t->sd);
        DIR *d = opendir(t->path);
        if (!d) err = errno ? errno : ENOENT;
        else {
            char *full = (char *)psAlloc(PATH_CAP + 260);
            struct dirent *e;
            while (full && (e = readdir(d)) != nullptr) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (n >= MAX_ENTRIES) {
                    truncated = true;
                    break;
                }
                snprintf(full, PATH_CAP + 260, "%s/%s", t->path, e->d_name);
                struct stat st = {};
                const bool okStat = stat(full, &st) == 0;
                const bool dir = okStat ? S_ISDIR(st.st_mode) : e->d_type == DT_DIR;
                cJSON *it = cJSON_CreateObject();
                cJSON_AddStringToObject(it, "name", e->d_name);
                cJSON_AddBoolToObject(it, "dir", dir);
                cJSON_AddNumberToObject(it, "size", dir ? 0 : (double)st.st_size);
                cJSON_AddNumberToObject(it, "mtime", okStat ? (double)st.st_mtime : 0);
                cJSON_AddItemToArray(arr, it);
                n++;
            }
            free(full);
            closedir(d);
        }
    }
    cJSON_AddBoolToObject(o, "truncated", truncated);
    free(t);
    if (err) {
        cJSON_Delete(o);
        return ioError(r, err);
    }
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handleDownload(httpd_req_t *r)
{
    MyWeb::Req q(r);
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    FILE *f;
    struct stat st = {};
    {
        Lock lk(t.sd);
        f = (stat(t.path, &st) == 0 && !S_ISDIR(st.st_mode)) ? fopen(t.path, "rb") : nullptr;
    }
    if (!f) return MyWeb::sendError(r, 404, tr(Str::WEB_ERR_FS_NOTFOUND));
    char *buf = (char *)psAlloc(CHUNK);
    if (!buf) {
        fclose(f);
        return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    }
    char disp[300];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", baseName(t.path));
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Content-Disposition", disp);
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = ESP_OK;
    for (;;) {
        size_t got;
        {
            Lock lk(t.sd);
            got = fread(buf, 1, CHUNK, f);
        }
        if (!got) break;
        if ((e = httpd_resp_send_chunk(r, buf, got)) != ESP_OK) break;
    }
    {
        Lock lk(t.sd);
        fclose(f);
    }
    free(buf);
    if (e != ESP_OK) return e;
    return httpd_resp_send_chunk(r, nullptr, 0);
}

static esp_err_t handleUpload(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    if (FsPath::isRoot(t.root, t.path) || !FsPath::validName(baseName(t.path)))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    bool overwrite = false;
    q.getBool("overwrite", overwrite);
    const size_t total = r->content_len;
    char tmp[PATH_CAP + 8];
    snprintf(tmp, sizeof(tmp), "%s.part", t.path);
    FILE *f = nullptr;
    {
        Lock lk(t.sd);
        struct stat st = {};
        if (stat(t.path, &st) == 0 && (S_ISDIR(st.st_mode) || !overwrite))
            return MyWeb::sendError(r, 409, tr(Str::WEB_ERR_FS_EXISTS));
        if (t.sd && total > BoardSd::info().freeBytes) return MyWeb::sendError(r, 507, tr(Str::WEB_ERR_FS_FULL));
        f = fopen(tmp, "wb");
    }
    if (!f) return ioError(r, errno == ENOENT ? ENOENT : EIO);
    char *buf = (char *)psAlloc(CHUNK);
    bool ok = buf != nullptr;
    int err = ok ? 0 : ENOMEM;
    size_t left = total;
    int timeouts = 0;
    while (ok && left > 0) {
        int n = httpd_req_recv(r, buf, left < CHUNK ? left : CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0) {
            ok = false;
            err = EIO;
            break;
        }
        timeouts = 0;
        Lock lk(t.sd);
        if (fwrite(buf, 1, n, f) != (size_t)n) {
            ok = false;
            err = errno == ENOSPC ? ENOSPC : EIO;
        }
        left -= n;
    }
    free(buf);
    {
        Lock lk(t.sd);
        if (fclose(f) != 0 && ok) {
            ok = false;
            err = EIO;
        }
        if (ok) {
            unlink(t.path);   // FAT rename() does not replace an existing file
            if (rename(tmp, t.path) != 0) {
                ok = false;
                err = EIO;
            }
        }
        if (!ok) unlink(tmp);
    }
    changed(t);
    if (!ok) {
        ESP_LOGW(TAG, "upload %s failed (%d)", t.path, err);
        if (err == ENOMEM) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
        return ioError(r, err);
    }
    MyLog::event("files", "uploaded %s (%u B)", t.path, (unsigned)total);
    return MyWeb::sendOk(r);
}

static esp_err_t handleDelete(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    if (FsPath::isRoot(t.root, t.path)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_ROOT));
    int err = 0;
    {
        Lock lk(t.sd);
        struct stat st = {};
        if (stat(t.path, &st) != 0) err = ENOENT;
        else if (S_ISDIR(st.st_mode)) {
            DIR *d = opendir(t.path);
            bool empty = true;
            if (d) {
                struct dirent *e;
                while ((e = readdir(d)) != nullptr)
                    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                        empty = false;
                        break;
                    }
                closedir(d);
            }
            if (!empty) err = ENOTEMPTY;
            else if (rmdir(t.path) != 0) err = errno ? errno : EIO;
        } else if (unlink(t.path) != 0) err = errno ? errno : EIO;
    }
    if (err) return ioError(r, err);
    changed(t);
    MyLog::event("files", "deleted %s", t.path);
    return MyWeb::sendOk(r);
}

static esp_err_t handleRename(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    char name[256];
    if (FsPath::isRoot(t.root, t.path)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_ROOT));
    if (!q.get("name", name, sizeof(name)) || !FsPath::validName(name))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    char dest[PATH_CAP];
    const size_t dirLen = (size_t)(baseName(t.path) - t.path);   // Includes the trailing '/'
    if (dirLen + strlen(name) + 1 > sizeof(dest)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    memcpy(dest, t.path, dirLen);
    strcpy(dest + dirLen, name);
    int err = 0;
    {
        Lock lk(t.sd);
        struct stat st = {};
        if (stat(t.path, &st) != 0) err = ENOENT;
        else if (strcasecmp(dest, t.path) && stat(dest, &st) == 0) err = EEXIST;   // FAT is case-insensitive
        else if (rename(t.path, dest) != 0) err = errno ? errno : EIO;
    }
    if (err) return ioError(r, err);
    changed(t);
    MyLog::event("files", "renamed %s -> %s", t.path, name);
    return MyWeb::sendOk(r);
}

static esp_err_t handleMkdir(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    if (FsPath::isRoot(t.root, t.path) || !FsPath::validName(baseName(t.path)))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    int err = 0;
    {
        Lock lk(t.sd);
        struct stat st = {};
        if (stat(t.path, &st) == 0) err = EEXIST;
        else if (mkdir(t.path, 0775) != 0) err = errno ? errno : EIO;
    }
    if (err) return ioError(r, err);
    changed(t);
    return MyWeb::sendOk(r);
}

static esp_err_t handleRemount(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    const bool ok = BoardSd::remount();
    Audio::invalidateFiles();
    MyLog::event("files", "SD remount from web: %s", ok ? "ok" : "no card");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "mounted", ok);
    return MyWeb::sendJson(r, 200, o);
}

bool registerRoutes()
{
    bool ok = MyWeb::on("/api/fs/list", HTTP_GET, handleList);
    ok &= MyWeb::on("/api/fs/download", HTTP_GET, handleDownload);
    ok &= MyWeb::on("/api/fs/upload", HTTP_POST, handleUpload);
    ok &= MyWeb::on("/api/fs/delete", HTTP_POST, handleDelete);
    ok &= MyWeb::on("/api/fs/rename", HTTP_POST, handleRename);
    ok &= MyWeb::on("/api/fs/mkdir", HTTP_POST, handleMkdir);
    ok &= MyWeb::on("/api/sd/remount", HTTP_POST, handleRemount);
    return ok;
}

}  // namespace AppWebFiles
