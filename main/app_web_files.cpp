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
//   POST /api/sd/format   pin, confirm=FORMAT-SD    explicitly format the SD card as FAT
//   v0.13.7: /api/files (paginated list), /api/files/mkdir, /api/files/bulk (DELETE), /api/files/move
#include "app_web_files.h"

#include "app_players.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <string>

#include "board_audio.h"
#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "files_page.h"
#include "fs_path.h"
#include "fs_upload.h"
#include "kara_index.h"
#include "lang.h"
#include "my_log.h"
#include "my_storage.h"
#include "my_web.h"
#include "nvs.h"
#include "web_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "webfs";

namespace AppWebFiles {

static constexpr size_t PATH_CAP = 300;
static constexpr size_t CHUNK = 32768;
static constexpr size_t DOWNLOAD_CHUNK = 8192;
static constexpr size_t WRITE_CACHE = 32768;
static constexpr int MAX_ENTRIES = 400;
static constexpr int MAX_UPLOADS = 1;
static std::atomic<int> s_activeUploads{0};
static std::atomic<int> s_activeDownloads{0};
static std::atomic<bool> s_formatting{false};
static std::atomic<int> s_formatStatus{0};

namespace {

struct SdUse {
    bool active = false;
    explicit SdUse(bool sd)
    {
        if (!sd || s_formatting.load()) return;
        s_activeDownloads.fetch_add(1);
        if (s_formatting.load()) s_activeDownloads.fetch_sub(1);
        else active = true;
    }
    ~SdUse() { if (active) s_activeDownloads.fetch_sub(1); }
};

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

struct UploadChunk {
    uint8_t index;
    size_t length;
};

struct UploadPipeline {
    Target *target;
    FILE *file;
    uint8_t *buffers[2];
    QueueHandle_t ready;
    QueueHandle_t available;
    SemaphoreHandle_t finished;
    std::atomic<int> error{0};
    int64_t bufferWaitUs = 0;
    int64_t lockUs = 0;
    int64_t writeUs = 0;
};

void uploadWriterTask(void *arg)
{
    auto *pipeline = static_cast<UploadPipeline *>(arg);
    UploadChunk chunk{};
    while (xQueueReceive(pipeline->ready, &chunk, portMAX_DELAY) == pdTRUE && chunk.length) {
        int error = pipeline->error.load();
        if (!error) {
            const int64_t lockStart = esp_timer_get_time();
            if (pipeline->target->sd) BoardSd::lock();
            pipeline->lockUs += esp_timer_get_time() - lockStart;
            const int64_t writeStart = esp_timer_get_time();
            const bool wrote = fwrite(pipeline->buffers[chunk.index], 1, chunk.length, pipeline->file) == chunk.length;
            pipeline->writeUs += esp_timer_get_time() - writeStart;
            if (pipeline->target->sd) BoardSd::unlock();
            if (!wrote) {
                error = errno == ENOSPC ? ENOSPC : EIO;
                pipeline->error.store(error);
            }
        }
        xQueueSend(pipeline->available, &chunk.index, portMAX_DELAY);
    }
    xSemaphoreGive(pipeline->finished);
    vTaskDelete(nullptr);
}

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
    if (err == EEXIST || err == EISDIR) return MyWeb::sendError(r, 409, tr(Str::WEB_ERR_FS_EXISTS));
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

esp_err_t sendUploadSkipped(httpd_req_t *r)
{
    return MyWeb::sendJsonStr(r, 200, "{\"ok\":true,\"skipped\":true}");
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
    long maxSize = 0;
    bool badLimit = false;
    const bool hasSizeLimit = q.has("max_size");
    if (hasSizeLimit && !q.getInt("max_size", 1, 16384, maxSize, badLimit))
        return MyWeb::sendError(r, 400, "Ogiltig max_size");
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    SdUse sdUse(t.sd);
    if (t.sd && !sdUse.active) return MyWeb::sendError(r, 503, "SD-kortet formateras");
    FILE *f;
    struct stat st = {};
    {
        Lock lk(t.sd);
        f = (stat(t.path, &st) == 0 && !S_ISDIR(st.st_mode)) ? fopen(t.path, "rb") : nullptr;
    }
    if (!f) return MyWeb::sendError(r, 404, tr(Str::WEB_ERR_FS_NOTFOUND));
    if (hasSizeLimit && st.st_size > maxSize) {
        {
            Lock lk(t.sd);
            fclose(f);
        }
        return MyWeb::sendError(r, 413, "Filen är större än max_size");
    }
    char *buf = (char *)psAlloc(DOWNLOAD_CHUNK);
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
            got = fread(buf, 1, DOWNLOAD_CHUNK, f);
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

static esp_err_t handleUploadRequest(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    const size_t queryLength = httpd_req_get_url_query_len(r);
    if (!FsUpload::requestUriFits(sizeof("/api/fs/upload") - 1, queryLength, CONFIG_HTTPD_MAX_URI_LEN))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    Target t;
    esp_err_t res = ESP_OK;
    if (!target(q, t, res)) return res;
    if (FsPath::isRoot(t.root, t.path) || !FsPath::validName(baseName(t.path)))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    bool overwrite = false;
    bool skipExisting = false;
    q.getBool("overwrite", overwrite);
    q.getBool("skip", skipExisting);
    if (overwrite && skipExisting) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    const size_t total = r->content_len;
    char tmp[PATH_CAP], backup[PATH_CAP];
    FILE *f = nullptr;
    int err = 0;
    bool sdIoFailed = false;
    const int64_t uploadStart = esp_timer_get_time();
    {
        Lock lk(t.sd);
        struct stat st = {};
        if (stat(t.path, &st) == 0) {
            if (skipExisting && !overwrite) return sendUploadSkipped(r);
            if (S_ISDIR(st.st_mode) || !overwrite)
                return MyWeb::sendError(r, 409, tr(Str::WEB_ERR_FS_EXISTS));
        } else if (errno != ENOENT) {
            return ioError(r, errno ? errno : EIO);
        }
        if (t.sd && total > BoardSd::info().freeBytes) return MyWeb::sendError(r, 507, tr(Str::WEB_ERR_FS_FULL));
        for (int attempt = 0; attempt < 8; attempt++) {
            const uint32_t nonce = esp_random();
            if (!FsUpload::siblingPath(t.path, nonce, false, tmp, sizeof(tmp)) ||
                !FsUpload::siblingPath(t.path, nonce, true, backup, sizeof(backup)))
                return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
            const int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (fd >= 0) {
                f = fdopen(fd, "wb");
                if (!f) {
                    err = errno ? errno : EIO;
                    close(fd);
                    unlink(tmp);
                }
                break;
            }
            err = errno ? errno : EIO;
            if (err != EEXIST) break;
        }
    }
    if (!f) return ioError(r, err ? err : EEXIST);
    UploadPipeline pipeline{};
    pipeline.target = &t;
    pipeline.file = f;
    pipeline.buffers[0] = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pipeline.buffers[1] = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *writeCache = t.sd
                           ? (char *)heap_caps_malloc(WRITE_CACHE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                           : nullptr;
    const bool dmaCache = writeCache != nullptr;
    if (!writeCache) writeCache = (char *)psAlloc(WRITE_CACHE);
    const bool buffered = writeCache && setvbuf(f, writeCache, _IOFBF, WRITE_CACHE) == 0;
    if (!buffered) {
        free(writeCache);
        writeCache = nullptr;
    }
    pipeline.ready = xQueueCreate(2, sizeof(UploadChunk));
    pipeline.available = xQueueCreate(2, sizeof(uint8_t));
    pipeline.finished = xSemaphoreCreateBinary();
    bool ok = pipeline.buffers[0] && pipeline.buffers[1] && pipeline.ready && pipeline.available &&
              pipeline.finished;
    err = ok ? 0 : ENOMEM;
    TaskHandle_t writer = nullptr;
    if (ok) {
        const uint8_t first = 0, second = 1;
        ok = xQueueSend(pipeline.available, &first, 0) == pdTRUE &&
             xQueueSend(pipeline.available, &second, 0) == pdTRUE &&
             xTaskCreate(uploadWriterTask, "fs_upload_sd", 4096, &pipeline, 3, &writer) == pdPASS;
        if (!ok) err = ENOMEM;
    }
    if (!ok) {
        Lock lk(t.sd);
        fclose(f);
        f = nullptr;
        FsUpload::discard(tmp);
    }
    size_t left = total;
    bool drainBody = false;
    int timeouts = 0;
    int64_t recvUs = 0, flushUs = 0, closeUs = 0;
    while (ok && left > 0) {
        if (pipeline.error.load()) {
            err = pipeline.error.load();
            sdIoFailed = t.sd && err == EIO;
            ok = false;
            drainBody = true;   // After SD recovery, so a broken card is not left busy for the whole body
            break;
        }
        uint8_t index = 0;
        const int64_t waitStart = esp_timer_get_time();
        if (xQueueReceive(pipeline.available, &index, pdMS_TO_TICKS(100)) != pdTRUE) {
            pipeline.bufferWaitUs += esp_timer_get_time() - waitStart;
            continue;
        }
        pipeline.bufferWaitUs += esp_timer_get_time() - waitStart;
        const int64_t recvStart = esp_timer_get_time();
        int n = httpd_req_recv(r, (char *)pipeline.buffers[index], left < CHUNK ? left : CHUNK);
        recvUs += esp_timer_get_time() - recvStart;
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) {
            xQueueSend(pipeline.available, &index, portMAX_DELAY);
            continue;
        }
        if (n <= 0) {
            ok = false;
            err = EIO;
            xQueueSend(pipeline.available, &index, portMAX_DELAY);
            break;
        }
        timeouts = 0;
        const UploadChunk chunk{index, (size_t)n};
        if (xQueueSend(pipeline.ready, &chunk, portMAX_DELAY) != pdTRUE) {
            ok = false;
            err = EIO;
            xQueueSend(pipeline.available, &index, portMAX_DELAY);
            break;
        }
        left -= (size_t)n;
    }
    if (writer) {
        const UploadChunk done{0, 0};
        xQueueSend(pipeline.ready, &done, portMAX_DELAY);
        xSemaphoreTake(pipeline.finished, portMAX_DELAY);
        if (pipeline.error.load()) {
            ok = false;
            err = pipeline.error.load();
            sdIoFailed = t.sd && err == EIO;
        }
    }
    bool skipped = false;
    if (f) {
        Lock lk(t.sd);
        if (ok) {
            const int64_t flushStart = esp_timer_get_time();
            if (fflush(f) != 0) {
                ok = false;
                err = errno == ENOSPC ? ENOSPC : EIO;
                sdIoFailed = t.sd && err == EIO;
            }
            flushUs = esp_timer_get_time() - flushStart;
        }
        const int64_t closeStart = esp_timer_get_time();
        const int closeResult = fclose(f);
        closeUs = esp_timer_get_time() - closeStart;
        if (closeResult != 0 && ok) {
            ok = false;
            err = EIO;
            sdIoFailed = t.sd;
        }
        if (ok) {
            const FsUpload::InstallResult result = FsUpload::install(tmp, t.path, backup, overwrite, skipExisting);
            skipped = result == FsUpload::InstallResult::SKIPPED;
            if (result == FsUpload::InstallResult::FAILED) {
                ok = false;
                err = errno ? errno : EIO;
                sdIoFailed = t.sd && err == EIO;
            }
        }
        if (!ok || skipped) FsUpload::discard(tmp);
    }
    free(pipeline.buffers[0]);
    free(pipeline.buffers[1]);
    free(writeCache);
    if (pipeline.ready) vQueueDelete(pipeline.ready);
    if (pipeline.available) vQueueDelete(pipeline.available);
    if (pipeline.finished) vSemaphoreDelete(pipeline.finished);
    if (ok && !skipped) changed(t);
    ESP_LOGI(TAG, "upload %s %s: %u B, total %llu ms, recv %llu ms, buffer wait %llu ms, SD lock wait %llu ms, "
                  "write %llu ms, flush %llu ms, close %llu ms, cache %u B (%s)", t.path,
             skipped ? "skipped" : ok ? "complete" : "failed", (unsigned)total,
             (unsigned long long)((esp_timer_get_time() - uploadStart) / 1000),
             (unsigned long long)(recvUs / 1000), (unsigned long long)(pipeline.bufferWaitUs / 1000),
             (unsigned long long)(pipeline.lockUs / 1000), (unsigned long long)(pipeline.writeUs / 1000),
             (unsigned long long)(flushUs / 1000), (unsigned long long)(closeUs / 1000),
             (unsigned)(buffered ? WRITE_CACHE : 0),
             buffered ? dmaCache ? "DMA" : "non-DMA" : "default");
    if (skipped) return sendUploadSkipped(r);
    if (!ok) {
        ESP_LOGW(TAG, "upload %s failed (%d)", t.path, err);
        if (sdIoFailed) {
            ESP_LOGW(TAG, "recovering SD card after upload I/O error");
            if (BoardSd::remount()) {
                Lock lk(true);
                FsUpload::discard(tmp);   // The first discard may have hit the failed card
            } else {
                ESP_LOGW(TAG, "SD recovery after upload failure did not mount the card");
            }
        }
        char drain[512];
        while (drainBody && left > 0) {
            const int n = httpd_req_recv(r, drain, left < sizeof(drain) ? left : sizeof(drain));
            if (n <= 0) break;
            left -= (size_t)n;
        }
        if (err == ENOMEM) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
        return ioError(r, err);
    }
    MyLog::event("files", "uploaded %s (%u B)", t.path, (unsigned)total);
    return MyWeb::sendOk(r);
}

static void uploadTask(void *arg)
{
    auto *r = static_cast<httpd_req_t *>(arg);
    handleUploadRequest(r);
    httpd_req_async_handler_complete(r);
    s_activeUploads.fetch_sub(1);
    vTaskDelete(nullptr);
}

static bool isInternalUpload(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char root[8] = "sd";
    return q.has("root") && q.get("root", root, sizeof(root)) && !strcmp(root, "ffat");
}

static esp_err_t handleUpload(httpd_req_t *r)
{
    if (isInternalUpload(r)) return handleUploadRequest(r);
    if (s_formatting.load()) return MyWeb::sendError(r, 503, "SD-kortet formateras");
    if (s_activeUploads.fetch_add(1) >= MAX_UPLOADS) {
        s_activeUploads.fetch_sub(1);
        return MyWeb::sendError(r, 503, "För många samtidiga uppladdningar");
    }
    if (s_formatting.load()) {
        s_activeUploads.fetch_sub(1);
        return MyWeb::sendError(r, 503, "SD-kortet formateras");
    }
    httpd_req_t *copy = nullptr;
    if (httpd_req_async_handler_begin(r, &copy) != ESP_OK) {
        s_activeUploads.fetch_sub(1);
        return MyWeb::sendError(r, 503, "Uppladdningstjänsten är upptagen");
    }
    if (xTaskCreate(uploadTask, "fs_upload", 8192, copy, 3, nullptr) != pdPASS) {
        const esp_err_t response = MyWeb::sendError(r, 503, tr(Str::WEB_ERR_MEMORY));
        httpd_req_async_handler_complete(copy);
        s_activeUploads.fetch_sub(1);
        return response;
    }
    return ESP_OK;
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
    char name[PATH_CAP];
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

static void sdFormatTask(void *)
{
    const bool ok = BoardSd::format();
    Audio::invalidateFiles();
    s_formatStatus.store(ok ? 2 : 3);
    s_formatting.store(false);
    if (ok) {
        MyLog::event("files", "SD card formatted as FAT");
        ESP_LOGI(TAG, "SD card format completed");
    } else {
        ESP_LOGE(TAG, "SD card format failed");
    }
    vTaskDelete(nullptr);
}

static esp_err_t handleSdFormat(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    char root[8] = "sd", confirm[24] = "";
    if ((q.has("root") && (!q.get("root", root, sizeof(root)) || strcmp(root, "sd"))) ||
        !q.get("confirm", confirm, sizeof(confirm)) || strcmp(confirm, "FORMAT-SD"))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));

    bool expected = false;
    if (!s_formatting.compare_exchange_strong(expected, true))
        return MyWeb::sendError(r, 409, "SD-kortet används redan");
    if (s_activeUploads.load() || s_activeDownloads.load()) {
        s_formatting.store(false);
        return MyWeb::sendError(r, 409, "Vänta tills filöverföringarna är klara");
    }

    s_formatStatus.store(1);
    if (xTaskCreate(sdFormatTask, "sd_format", 6144, nullptr, 3, nullptr) != pdPASS) {
        s_formatStatus.store(0);
        s_formatting.store(false);
        return MyWeb::sendError(r, 503, "Kunde inte starta formateringen");
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "formatting", true);
    return MyWeb::sendJson(r, 202, o);
}

static esp_err_t handleSdFormatStatus(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    const int status = s_formatStatus.load();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "formatting", status == 1);
    cJSON_AddBoolToObject(o, "formatted", status == 2);
    if (status == 3)
        cJSON_AddStringToObject(o, "error", "Formatering misslyckades; SD-kortet återmonterades om möjligt");
    return MyWeb::sendJson(r, 200, o);
}

// ----------------------------------------------------------------------------------------
// v0.13.7 file manager API (JSON bodies; changing requests need the admin PIN as "pin" query
// parameter or X-Admin-Pin header):
//   GET    /api/files?root=&dir=&page=1&limit=50&sort=name|size|mtime&order=asc|desc
//   POST   /api/files/mkdir  {"root","path"}               creates missing parents too
//   DELETE /api/files/bulk   {"root","paths":[..]}         files and folders (recursive)
//   POST   /api/files/move   {"root","paths":[..],"dest"}  dest is created when missing
// ----------------------------------------------------------------------------------------
namespace {

constexpr int LIST_MAX = 4000;      // Entries read from one directory
constexpr int BULK_MAX = 500;       // Paths per bulk request
constexpr size_t BODY_MAX = 64 * 1024;
constexpr int TREE_DEPTH = 16;

// Resolves a root name + user path from a JSON request. Returns an HTTP code (0 = ok).
int resolveTarget(const char *root, const char *user, Target &t)
{
    if (!root || !*root || !strcmp(root, "sd")) t.sd = true;
    else if (!strcmp(root, "ffat")) t.sd = false;
    else return 400;
    t.root = t.sd ? BoardSd::MOUNT : MyFat::basePath();
    if (!user || !FsPath::resolve(t.root, user, t.path, sizeof(t.path))) return 400;
    if (!rootMounted(t.sd)) return 503;
    return 0;
}

esp_err_t codeError(httpd_req_t *r, int code)
{
    if (code == 503) return MyWeb::sendError(r, 503, tr(Str::WEB_ERR_FS_NOCARD));
    return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
}

const char *jsonStr(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : nullptr;
}

// mkdir -p below the root (each step takes the SD lock separately)
int mkdirs(const Target &t)
{
    char p[PATH_CAP];
    snprintf(p, sizeof(p), "%s", t.path);
    const size_t rootLen = strlen(t.root);
    for (size_t i = rootLen + 1;; i++) {
        const char c = p[i];
        if (c != '/' && c != 0) continue;
        p[i] = 0;
        {
            Lock lk(t.sd);
            struct stat st = {};
            if (stat(p, &st) == 0) {
                if (!S_ISDIR(st.st_mode)) return EEXIST;
            } else if (mkdir(p, 0775) != 0 && errno != EEXIST) {
                return errno ? errno : EIO;
            }
        }
        if (c == 0) break;
        p[i] = '/';
    }
    return 0;
}

// Recursive delete. Child names are collected before recursing so no directory is modified
// while it is being read; the SD lock is held per step so audio streaming keeps going.
int removeTree(bool sd, const char *path, int depth)
{
    struct stat st = {};
    {
        Lock lk(sd);
        if (stat(path, &st) != 0) return ENOENT;
        if (!S_ISDIR(st.st_mode)) return unlink(path) == 0 ? 0 : (errno ? errno : EIO);
    }
    if (depth > TREE_DEPTH) return ENOTEMPTY;
    for (;;) {
        std::string names;   // '\0'-separated batch
        int batch = 0;
        {
            Lock lk(sd);
            DIR *d = opendir(path);
            if (!d) return errno ? errno : EIO;
            struct dirent *e;
            while (batch < 64 && (e = readdir(d)) != nullptr) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                names.append(e->d_name);
                names += '\0';
                batch++;
            }
            closedir(d);
        }
        if (!batch) break;
        char *child = (char *)psAlloc(PATH_CAP + 260);
        if (!child) return ENOMEM;
        int err = 0;
        for (size_t i = 0; i < names.size() && !err; i += strlen(names.c_str() + i) + 1) {
            if ((size_t)snprintf(child, PATH_CAP + 260, "%s/%s", path, names.c_str() + i) >= PATH_CAP + 260) {
                err = ENAMETOOLONG;
                break;
            }
            err = removeTree(sd, child, depth + 1);
        }
        free(child);
        if (err) return err;
    }
    Lock lk(sd);
    return rmdir(path) == 0 ? 0 : (errno ? errno : EIO);
}

const char *errName(int err)
{
    switch (err) {
    case ENOENT: return tr(Str::WEB_ERR_FS_NOTFOUND);
    case EEXIST: return tr(Str::WEB_ERR_FS_EXISTS);
    case ENOTEMPTY: return tr(Str::WEB_ERR_FS_NOTEMPTY);
    case ENOSPC: return tr(Str::WEB_ERR_FS_FULL);
    case EINVAL: return tr(Str::WEB_ERR_FS_PATH);
    default: return tr(Str::WEB_ERR_FS_IO);
    }
}

// Answer {ok, done, failed:[{path,error}]}
esp_err_t bulkAnswer(httpd_req_t *r, int done, cJSON *failed)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", cJSON_GetArraySize(failed) == 0);
    cJSON_AddNumberToObject(o, "done", done);
    cJSON_AddItemToObject(o, "failed", failed);
    return MyWeb::sendJson(r, 200, o);
}

void addFail(cJSON *failed, const char *path, int err)
{
    cJSON *f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "path", path ? path : "");
    cJSON_AddStringToObject(f, "error", errName(err));
    cJSON_AddItemToArray(failed, f);
}

}  // namespace

static esp_err_t handleFiles(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char root[8] = "sd", dir[PATH_CAP] = "/", sortS[8] = "name", order[8] = "asc";
    if (q.has("root")) q.get("root", root, sizeof(root));
    if (q.has("dir") && !q.get("dir", dir, sizeof(dir))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    if (q.has("sort")) q.get("sort", sortS, sizeof(sortS));
    if (q.has("order")) q.get("order", order, sizeof(order));
    long page = 1, limit = 50;
    bool bad = false;
    q.getInt("page", 1, 100000, page, bad);
    q.getInt("limit", 1, 500, limit, bad);
    if (bad) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    const bool sd = strcmp(root, "ffat") != 0;
    if (!rootMounted(sd)) {
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
    const int code = resolveTarget(root, dir, *t);
    if (code) {
        free(t);
        return codeError(r, code);
    }
    KaraIndex::PsVector<FilesPage::Item> items;
    KaraIndex::PsVector<char> pool;
    int err = 0;
    bool truncated = false;
    {
        char *full = (char *)psAlloc(PATH_CAP + 260);
        Lock lk(t->sd);
        DIR *d = full ? opendir(t->path) : nullptr;
        if (!full) err = ENOMEM;
        else if (!d) err = errno ? errno : ENOENT;
        else {
            struct dirent *e;
            while ((e = readdir(d)) != nullptr) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if ((int)items.size() >= LIST_MAX) {
                    truncated = true;
                    break;
                }
                snprintf(full, PATH_CAP + 260, "%s/%s", t->path, e->d_name);
                struct stat st = {};
                const bool okStat = stat(full, &st) == 0;
                FilesPage::Item it;
                it.name = (uint32_t)pool.size();
                pool.insert(pool.end(), e->d_name, e->d_name + strlen(e->d_name) + 1);
                it.dir = okStat ? S_ISDIR(st.st_mode) : e->d_type == DT_DIR;
                it.size = it.dir ? 0 : (uint64_t)st.st_size;
                it.mtime = okStat ? (int64_t)st.st_mtime : 0;
                items.push_back(it);
            }
            closedir(d);
        }
        free(full);
    }
    if (err) {
        free(t);
        return ioError(r, err);
    }
    FilesPage::sortItems(items, pool.data(), FilesPage::parseSort(sortS), !strcmp(order, "desc"));
    const FilesPage::Page pg = FilesPage::pageOf((int)items.size(), (int)page, (int)limit);
    int dirs = 0;
    uint64_t bytes = 0;
    for (const FilesPage::Item &it : items) {
        dirs += it.dir;
        bytes += it.size;
    }
    std::string o;
    o.reserve(256 + (size_t)pg.count * 96);
    o += "{\"ok\":true,\"root\":";
    WebUtil::appendStr(o, t->sd ? "sd" : "ffat");
    o += ",\"path\":";
    WebUtil::appendStr(o, relPath(*t));
    o += ",\"mounted\":true";
    uint64_t total, freeB;
    if (t->sd) {
        const BoardSd::Info i = BoardSd::info();
        total = i.totalBytes;
        freeB = i.freeBytes;
    } else {
        total = MyFat::totalBytes();
        const uint64_t used = MyFat::usedBytes();
        freeB = total > used ? total - used : 0;
    }
    o += ",\"total\":"; WebUtil::appendNum(o, total);
    o += ",\"free\":"; WebUtil::appendNum(o, freeB);
    o += ",\"count\":"; WebUtil::appendNum(o, items.size());
    o += ",\"dirs\":"; WebUtil::appendNum(o, (unsigned)dirs);
    o += ",\"bytes\":"; WebUtil::appendNum(o, bytes);
    o += ",\"page\":"; WebUtil::appendNum(o, (unsigned)pg.page);
    o += ",\"pages\":"; WebUtil::appendNum(o, (unsigned)pg.pages);
    o += ",\"limit\":"; WebUtil::appendNum(o, (unsigned)limit);
    o += ",\"truncated\":";
    o += truncated ? "true" : "false";
    o += ",\"entries\":[";
    for (int i = 0; i < pg.count; i++) {
        const FilesPage::Item &it = items[(size_t)(pg.first + i)];
        if (i) o += ',';
        o += "{\"name\":";
        WebUtil::appendStr(o, pool.data() + it.name);
        o += it.dir ? ",\"dir\":true" : ",\"dir\":false";
        o += ",\"size\":"; WebUtil::appendNum(o, it.size);
        o += ",\"mtime\":"; WebUtil::appendNum(o, it.mtime > 0 ? (unsigned long long)it.mtime : 0);
        o += '}';
    }
    o += "]}";
    free(t);
    return WebUtil::sendJsonText(r, o);
}

static esp_err_t handleFilesMkdir(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    cJSON *j = WebUtil::readJson(r, 2048);
    if (!j) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    Target t;
    const int code = resolveTarget(jsonStr(j, "root"), jsonStr(j, "path"), t);
    cJSON_Delete(j);
    if (code) return codeError(r, code);
    if (FsPath::isRoot(t.root, t.path) || !FsPath::validName(baseName(t.path)))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    {
        Lock lk(t.sd);
        struct stat st = {};
        if (stat(t.path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) return MyWeb::sendOk(r);
            return ioError(r, EEXIST);
        }
    }
    const int err = mkdirs(t);
    if (err) return ioError(r, err);
    changed(t);
    MyLog::event("files", "mkdir %s", t.path);
    return MyWeb::sendOk(r);
}

static esp_err_t handleFilesBulkDelete(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    cJSON *j = WebUtil::readJson(r, BODY_MAX);
    const cJSON *paths = j ? cJSON_GetObjectItemCaseSensitive(j, "paths") : nullptr;
    if (!cJSON_IsArray(paths) || cJSON_GetArraySize(paths) < 1 || cJSON_GetArraySize(paths) > BULK_MAX) {
        cJSON_Delete(j);
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    }
    const char *root = jsonStr(j, "root");
    Target *t = (Target *)psAlloc(sizeof(Target));
    if (!t) {
        cJSON_Delete(j);
        return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    }
    cJSON *failed = cJSON_CreateArray();
    int done = 0;
    bool any = false, sd = true;
    const cJSON *p;
    cJSON_ArrayForEach(p, paths)
    {
        const char *user = cJSON_IsString(p) ? p->valuestring : nullptr;
        const int code = resolveTarget(root, user, *t);
        if (code == 503) {
            addFail(failed, user, EIO);
            break;
        }
        if (code || FsPath::isRoot(t->root, t->path)) {
            addFail(failed, user, EINVAL);
            continue;
        }
        sd = t->sd;
        const int err = removeTree(t->sd, t->path, 0);
        if (err) {
            addFail(failed, user, err);
            ESP_LOGW(TAG, "bulk delete %s: %d", t->path, err);
        } else {
            done++;
            any = true;
            MyLog::event("files", "deleted %s", t->path);
        }
    }
    if (any && sd) {
        BoardSd::refreshUsage();
        Audio::invalidateFiles();
    }
    free(t);
    cJSON_Delete(j);
    return bulkAnswer(r, done, failed);
}

static esp_err_t handleFilesMove(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    cJSON *j = WebUtil::readJson(r, BODY_MAX);
    const cJSON *paths = j ? cJSON_GetObjectItemCaseSensitive(j, "paths") : nullptr;
    if (!cJSON_IsArray(paths) || cJSON_GetArraySize(paths) < 1 || cJSON_GetArraySize(paths) > BULK_MAX) {
        cJSON_Delete(j);
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    }
    const char *root = jsonStr(j, "root");
    Target *dest = (Target *)psAlloc(sizeof(Target));
    Target *src = (Target *)psAlloc(sizeof(Target));
    char *to = (char *)psAlloc(PATH_CAP);
    if (!dest || !src || !to) {
        free(dest);
        free(src);
        free(to);
        cJSON_Delete(j);
        return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    }
    esp_err_t res;
    const int code = resolveTarget(root, jsonStr(j, "dest"), *dest);
    int err = code ? 0 : (FsPath::isRoot(dest->root, dest->path) ? 0 : mkdirs(*dest));
    if (code) res = codeError(r, code);
    else if (err) res = ioError(r, err);
    else {
        cJSON *failed = cJSON_CreateArray();
        int done = 0;
        const cJSON *p;
        cJSON_ArrayForEach(p, paths)
        {
            const char *user = cJSON_IsString(p) ? p->valuestring : nullptr;
            if (resolveTarget(root, user, *src) || FsPath::isRoot(src->root, src->path) ||
                FilesPage::isInside(dest->path, src->path)) {   // Also refuses moving a folder into itself
                addFail(failed, user, EINVAL);
                continue;
            }
            const char *name = baseName(src->path);
            if ((size_t)snprintf(to, PATH_CAP, "%s/%s", dest->path, name) >= PATH_CAP) {
                addFail(failed, user, EINVAL);
                continue;
            }
            const size_t parentLen = (size_t)(name - src->path) - 1;
            if (parentLen == strlen(dest->path) && !strncasecmp(src->path, dest->path, parentLen)) {
                done++;   // Already there
                continue;
            }
            err = 0;
            {
                Lock lk(src->sd);
                struct stat st = {};
                if (stat(src->path, &st) != 0) err = ENOENT;
                else if (stat(to, &st) == 0) err = EEXIST;
                else if (rename(src->path, to) != 0) err = errno ? errno : EIO;
            }
            if (err) addFail(failed, user, err);
            else {
                done++;
                MyLog::event("files", "moved %s -> %s", src->path, to);
            }
        }
        if (done) {
            changed(*dest);
            if (dest->sd) Audio::invalidateFiles();   // The source may have been below /sdcard/otr/
        }
        res = bulkAnswer(r, done, failed);
    }
    free(dest);
    free(src);
    free(to);
    cJSON_Delete(j);
    return res;
}

// ----------------------------------------------------------------------------------------
// v0.18.6 "Repair internal flash": admin-only, FFat only. Backs up the readable important files
// to the SD card first (if a card is present), then reformats FFat and restores them. Formatting
// is allowed even without an SD card (reported in the answer); SD is never made mandatory.
// ----------------------------------------------------------------------------------------
namespace {

bool copyFile(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[2048];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    ok = (fflush(out) == 0) && ok;
    fsync(fileno(out));
    fclose(out);
    fclose(in);
    if (!ok) remove(to);
    return ok;
}

struct RepairFile {
    const char *ffat;   // Full path on FFat
    const char *name;   // Base name (used under the backup dir and when restoring)
};
constexpr RepairFile kRepairFiles[] = {
    {"/ffat/players.bin", "players.bin"},
    {"/ffat/events.log", "events.log"},
};
constexpr const char *kBackupDir = "/sdcard/otr/backup";

}  // namespace

static esp_err_t handleFsRepair(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    // Internal flash only - never the SD card.
    char root[8] = "ffat";
    if (q.has("root") && !q.get("root", root, sizeof(root))) root[0] = 0;
    if (strcmp(root, "ffat") != 0) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_FS_PATH));
    const bool sdMounted = BoardSd::mounted();
    bool sdBackup = false;
    cJSON *o = cJSON_CreateObject();
    cJSON *backed = cJSON_AddArrayToObject(o, "backed");

    // (a) Back up the readable important files to the SD card first (best effort).
    if (sdMounted) {
        BoardSd::Guard g;
        if (BoardSd::ensureDir(kBackupDir)) {
            for (const RepairFile &f : kRepairFiles) {
                struct stat st = {};
                if (stat(f.ffat, &st) != 0) continue;   // Skip a file that is not there
                char dest[96];
                snprintf(dest, sizeof(dest), "%s/%s", kBackupDir, f.name);
                if (copyFile(f.ffat, dest)) {
                    cJSON_AddItemToArray(backed, cJSON_CreateString(f.name));
                    sdBackup = true;
                }
            }
        }
    }

    // (b) Stop writing events to FFat, then reformat it in place.
    MyLog::setEventFile(nullptr);
    const bool formatted = MyFat::format();

    // (c) Restore the backed-up files onto the fresh filesystem.
    bool restored = false;
    if (formatted && sdBackup) {
        BoardSd::Guard g;
        for (const RepairFile &f : kRepairFiles) {
            char src[96];
            snprintf(src, sizeof(src), "%s/%s", kBackupDir, f.name);
            struct stat st = {};
            if (stat(src, &st) != 0) continue;
            if (copyFile(src, f.ffat)) restored = true;
        }
    }

    // (d) Re-arm the event log and reload the player registry from the fresh filesystem.
    if (MyFat::mounted()) MyLog::setEventFile("/ffat/events.log");
    const bool playersReloaded = formatted ? Players::begin() : false;

    MyLog::event("storage", "repair ffat: formatted=%d sdBackup=%d restored=%d players=%d",
                 formatted, sdBackup, restored, playersReloaded);

    if (!formatted) {
        cJSON_Delete(o);
        return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_FS_REPAIR));
    }
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "message", tr(Str::WEB_FS_REPAIR_DONE));
    cJSON_AddBoolToObject(o, "sd", sdMounted);
    cJSON_AddBoolToObject(o, "sdBackup", sdBackup);
    cJSON_AddBoolToObject(o, "formatted", formatted);
    cJSON_AddBoolToObject(o, "restored", restored);
    cJSON_AddBoolToObject(o, "playersReloaded", playersReloaded);
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handleNvsInfo(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    nvs_stats_t stats{};
    if (!partition || nvs_get_stats("nvs", &stats) != ESP_OK) {
        return MyWeb::sendError(r, 500, "Could not read NVS information");
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "bytes", partition->size);
    cJSON_AddNumberToObject(o, "used", stats.used_entries);
    cJSON_AddNumberToObject(o, "free", stats.free_entries);
    cJSON_AddNumberToObject(o, "total", stats.total_entries);
    cJSON_AddNumberToObject(o, "namespaces", stats.namespace_count);
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handleNvsBackup(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
#ifdef CONFIG_NVS_ENCRYPTION
    return MyWeb::sendError(r, 409, "Encrypted NVS requires a backup of its encryption keys");
#else
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!partition) return MyWeb::sendError(r, 500, "NVS partition not found");
    uint8_t *image = static_cast<uint8_t *>(heap_caps_malloc(
        partition->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!image) return MyWeb::sendError(r, 503, "Not enough memory for NVS backup");
    const esp_err_t read = esp_partition_read(partition, 0, image, partition->size);
    if (read != ESP_OK) {
        free(image);
        ESP_LOGE(TAG, "NVS backup read failed: %s", esp_err_to_name(read));
        return MyWeb::sendError(r, 500, "Could not read NVS backup");
    }
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=\"otr-nvs.bin\"");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_send(r, reinterpret_cast<const char *>(image), partition->size);
    free(image);
    return result;
#endif
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
    ok &= MyWeb::on("/api/sd/format", HTTP_POST, handleSdFormat);
    ok &= MyWeb::on("/api/sd/format/status", HTTP_GET, handleSdFormatStatus);
    ok &= MyWeb::on("/api/fs/repair", HTTP_POST, handleFsRepair);
    ok &= MyWeb::on("/api/nvs", HTTP_GET, handleNvsInfo);
    ok &= MyWeb::on("/api/nvs/backup", HTTP_GET, handleNvsBackup);
    ok &= MyWeb::on("/api/files", HTTP_GET, handleFiles);
    ok &= MyWeb::on("/api/files/mkdir", HTTP_POST, handleFilesMkdir);
    ok &= MyWeb::on("/api/files/bulk", HTTP_DELETE, handleFilesBulkDelete);
    ok &= MyWeb::on("/api/files/move", HTTP_POST, handleFilesMove);
    return ok;
}

}  // namespace AppWebFiles
