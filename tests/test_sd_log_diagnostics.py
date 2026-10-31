"""Exercise production SD-log controls, RAM-only export and paused persistence."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "main/otr_log.cpp").read_text()
handlers = source[source.index("esp_err_t handleDownload("):source.index("}  // namespace")]
task = source[source.index("void sdTask("):source.index("// ---- SSE")]
harness = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, MALLOC_CAP_INTERNAL=1, portMAX_DELAY=-1;
constexpr int FILE_COUNT=3, RING_BYTES=65536, SD_PERIOD_MS=2000;
#define FW_VERSION "test"
#define ESP_LOGI(...) ((void)0)
enum esp_log_level_t { ESP_LOG_ERROR=1, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG };
std::atomic<int> s_logLevel{ESP_LOG_INFO};
std::map<std::string,int> levels;
void esp_log_level_set(const char *tag,esp_log_level_t level) { levels[tag]=level; }
#define pdMS_TO_TICKS(n) (n)
struct httpd_req_t {
    bool admin=true;
    int code=0;
    std::string body;
    std::map<std::string,std::string> query;
};
namespace MyWeb {
struct Req {
    httpd_req_t *r;
    explicit Req(httpd_req_t *req):r(req){}
    bool adminAllowed() { return r->admin; }
    bool get(const char *name, char *out, size_t cap) {
        auto it=r->query.find(name);
        if(it==r->query.end() || it->second.size()>=cap) return false;
        std::strcpy(out,it->second.c_str()); return true;
    }
    bool getBool(const char *name,bool &out) {
        char value[8]; if(!get(name,value,sizeof(value))) return false;
        out=std::strcmp(value,"1")==0; return true;
    }
};
int sendError(httpd_req_t *r,int code,const char *text) { r->code=code; r->body=text; return ESP_FAIL; }
int sendJsonStr(httpd_req_t *r,int code,const char *text) { r->code=code; r->body=text; return ESP_OK; }
}
bool s_ready=true;
std::atomic<bool> s_sdEnabled{true};
void *s_sdMtx=nullptr;
int mutexTakes=0, sdReads=0, appends=0, taskStep=0, mode=0;
size_t consumed=0;
std::vector<std::string> entries;
void *psAlloc(size_t n) { return std::malloc(n); }
int64_t esp_timer_get_time() { return 1000000; }
size_t esp_get_free_heap_size() { return 300000; }
size_t heap_caps_get_free_size(int) { return 300000; }
int esp_reset_reason() { return 1; }
namespace BoardSd {
bool supported=true, tracing=false;
bool mounted() { return true; }
bool transferHealthy() { return true; }
bool traceSupported() { return supported; }
bool traceEnabled() { return tracing; }
uint32_t traceRemainingSeconds() { return tracing?600:0; }
void setTrace(bool enabled) { tracing=enabled; }
}
uint64_t ringHead() { return 30; }
size_t ringRead(char *out,size_t cap,uint64_t *pos) {
    if(*pos>=30) return 0;
    const size_t n=cap<30-*pos?cap:30-*pos;
    std::memset(out,'x',n); *pos+=n; consumed+=n; return n;
}
void xSemaphoreTake(void *,int) { ++mutexTakes; }
void xSemaphoreGive(void *) {}
void httpd_resp_set_type(httpd_req_t *,const char *) {}
void httpd_resp_set_hdr(httpd_req_t *,const char *,const char *) {}
struct ZipOut {
    httpd_req_t *r=nullptr; bool ok=true; uint16_t t=0,d=0;
    void memEntry(const char *name,const void *,size_t) { entries.emplace_back(name); }
    void finish() {}
};
namespace OtrLogCore {
uint16_t dosTime(int,int,int) { return 0; }
uint16_t dosDate(int,int,int) { return 0; }
}
void filePath(char *out,size_t n,int i) { std::snprintf(out,n,"log%d",i); }
void zipFile(ZipOut &,const char *,const char *,char *,size_t) { ++sdReads; }
bool appendSd(const char *,size_t) {
    ++appends;
    return !(mode==1 && appends==2); // First backlog write fails, next cycle must retry it.
}
void vTaskDelay(int) {
    ++taskStep;
    if(mode==1) {
        s_sdEnabled=true;
        if(taskStep==2) assert(appends==2 && consumed==30);
        if(taskStep==3) throw std::runtime_error("stop");
        return;
    }
    if(taskStep==1) { s_sdEnabled=false; }
    if(taskStep==2) {
        assert(appends==0 && consumed==0);
        s_sdEnabled=true;
    }
    if(taskStep==3) throw std::runtime_error("stop");
}
'''
checks = r'''
int main() {
    httpd_req_t req;
    assert(handleSdLogStatus(&req)==ESP_OK && req.body=="{\"enabled\":true}");
    req.admin=false; req.query["enabled"]="0";
    assert(handleSdLogControl(&req)==ESP_FAIL && req.code==403 && s_sdEnabled);
    req.admin=true; req.query.clear();
    assert(handleSdLogControl(&req)==ESP_FAIL && req.code==400 && s_sdEnabled);
    for(const char *invalid:{"true","-1","garbage-too-long",""}) {
        req.query["enabled"]=invalid;
        assert(handleSdLogControl(&req)==ESP_FAIL && req.code==400 && s_sdEnabled);
    }
    req.query["enabled"]="0";
    assert(handleSdLogControl(&req)==ESP_OK && !s_sdEnabled);
    req.query["enabled"]="1";
    assert(handleSdLogControl(&req)==ESP_OK && s_sdEnabled);
    req.query.clear(); req.query["ram"]="1";
    assert(handleDownload(&req)==ESP_OK && sdReads==0 && mutexTakes==0);
    assert(entries==std::vector<std::string>({"info.txt","ram.log"}));
    entries.clear(); req.query.clear();
    assert(handleDownload(&req)==ESP_OK && sdReads==3 && mutexTakes==1);
    s_ready=false;
    assert(handleDownload(&req)==ESP_FAIL && req.code==503);
    consumed=0; appends=0;
    try { sdTask(nullptr); } catch(const std::runtime_error &) {}
    assert(taskStep==3 && appends==2 && consumed==30);
    mode=1; taskStep=0; appends=0; consumed=0;
    try { sdTask(nullptr); } catch(const std::runtime_error &) {}
    assert(appends==3 && consumed==60); // Available backlog was retried rather than skipped.
    req.query={{"level","debug"},{"sd_trace","1"}};
    req.admin=false;
    assert(handleLogConfigControl(&req)==ESP_FAIL && req.code==403 && s_logLevel==ESP_LOG_INFO);
    req.admin=true;
    req.query["level"]="verbose";
    assert(handleLogConfigControl(&req)==ESP_FAIL && req.code==400 && s_logLevel==ESP_LOG_INFO);
    req.query["level"]="debug"; req.query["sd_trace"]="true";
    assert(handleLogConfigControl(&req)==ESP_FAIL && req.code==400 && s_logLevel==ESP_LOG_INFO);
    req.query["sd_trace"]="1"; BoardSd::supported=false;
    assert(handleLogConfigControl(&req)==ESP_FAIL && req.code==400 && !BoardSd::tracing);
    BoardSd::supported=true;
    assert(handleLogConfigControl(&req)==ESP_OK && s_logLevel==ESP_LOG_DEBUG && BoardSd::tracing);
    assert(levels["*"]==ESP_LOG_DEBUG && levels["sdtrace"]==ESP_LOG_INFO);
    req.query["level"]="error";
    assert(handleLogConfigControl(&req)==ESP_OK && levels["*"]==ESP_LOG_ERROR);
    assert(levels["sdtrace"]==ESP_LOG_INFO);
    req.query["level"]="warning"; req.query["sd_trace"]="0";
    assert(handleLogConfigControl(&req)==ESP_OK && levels["*"]==ESP_LOG_WARN && !BoardSd::tracing);
    assert(levels["sdtrace"]==ESP_LOG_WARN);
    req.query["level"]="info";
    assert(handleLogConfigControl(&req)==ESP_OK && levels["*"]==ESP_LOG_INFO);
}
'''
with tempfile.TemporaryDirectory(prefix="otr-sd-log-diag-") as directory:
    work = pathlib.Path(directory)
    cpp = work / "test.cpp"
    cpp.write_text(harness + handlers + task + checks)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("SD log diagnostics: passed")
