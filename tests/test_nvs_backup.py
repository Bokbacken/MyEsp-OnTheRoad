"""Exercise production NVS handlers with synthetic partition/HTTP dependencies."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NvsBackupTest(unittest.TestCase):
    def test_handlers(self):
        source = (ROOT / "main/app_web_files.cpp").read_text()
        handlers = source[source.index("static esp_err_t handleNvsInfo"):
                          source.index("bool registerRoutes()")]
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_NVS=2;
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
struct httpd_req_t { bool admin=true; int code=0; std::string body;
    std::map<std::string,std::string> headers; };
struct esp_partition_t { size_t size=20480; } partition;
struct nvs_stats_t { size_t used_entries=30,free_entries=96,total_entries=126,namespace_count=2; };
struct cJSON { std::map<std::string,double> numbers; };
bool present=true, memory=true, readable=true, statsOk=true;
int reads=0, allocations=0;
enum class Str { WEB_ERR_PIN };
const char *tr(Str) { return "PIN"; }
const char *TAG="test";
#define ESP_LOGE(...) ((void)0)
const char *esp_err_to_name(int) { return "test failure"; }
const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label) {
    assert(type==ESP_PARTITION_TYPE_DATA && sub==ESP_PARTITION_SUBTYPE_DATA_NVS);
    assert(!strcmp(label,"nvs")); return present ? &partition : nullptr;
}
int nvs_get_stats(const char *label,nvs_stats_t *s) {
    assert(!strcmp(label,"nvs")); *s=nvs_stats_t{}; return statsOk ? 0 : -1;
}
void *heap_caps_malloc(size_t n,int flags) {
    assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); ++allocations;
    return memory ? malloc(n) : nullptr;
}
int esp_partition_read(const esp_partition_t *p,size_t offset,void *dst,size_t n) {
    ++reads; assert(p==&partition && offset==0 && n==partition.size);
    memset(dst,0x42,n); return readable ? 0 : -1;
}
cJSON *cJSON_CreateObject() { return new cJSON; }
void cJSON_AddBoolToObject(cJSON *,const char *,bool) {}
void cJSON_AddNumberToObject(cJSON *o,const char *k,size_t n) { o->numbers[k]=n; }
int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) { r->headers[k]=v;return 0; }
int httpd_resp_set_type(httpd_req_t *r,const char *v) { r->headers["type"]=v;return 0; }
int httpd_resp_send(httpd_req_t *r,const char *b,size_t n) { r->code=200;r->body.assign(b,n);return 0; }
namespace MyWeb {
struct Req { httpd_req_t *r; Req(httpd_req_t *r):r(r){} bool adminAllowed(){return r->admin;} };
int sendError(httpd_req_t *r,int code,const char *text) {r->code=code;r->body=text;return -1;}
int sendJson(httpd_req_t *r,int code,cJSON *o) {
    r->code=code; assert(o->numbers["bytes"]==20480 && o->numbers["used"]==30);
    assert(o->numbers["free"]==96 && o->numbers["total"]==126 && o->numbers["namespaces"]==2);
    delete o;return 0;
}
}
'''
        checks = r'''
int main() {
    httpd_req_t r;
    r.admin=false; handleNvsInfo(&r); assert(r.code==403);
    handleNvsBackup(&r); assert(r.code==403 && reads==0 && allocations==0);
    r.admin=true; handleNvsInfo(&r); assert(r.code==200 && r.headers["Cache-Control"]=="no-store");
    present=false; handleNvsInfo(&r); assert(r.code==500);
    present=true; statsOk=false; handleNvsInfo(&r); assert(r.code==500); statsOk=true;
#ifdef CONFIG_NVS_ENCRYPTION
    handleNvsBackup(&r); assert(r.code==409 && reads==0 && allocations==0);
#else
    present=false; handleNvsBackup(&r); assert(r.code==500); present=true;
    memory=false; handleNvsBackup(&r); assert(r.code==503); memory=true;
    readable=false; handleNvsBackup(&r); assert(r.code==500); readable=true;
    handleNvsBackup(&r); assert(r.code==200 && r.body==std::string(20480,0x42));
    assert(r.headers["Content-Disposition"]=="attachment; filename=\"otr-nvs.bin\"");
    assert(r.headers["Cache-Control"]=="no-store" && r.headers["type"]=="application/octet-stream");
#endif
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / "nvs.cpp"
            exe = pathlib.Path(directory) / "nvs"
            cpp.write_text(harness + handlers + checks)
            for flags in ([], ["-DCONFIG_NVS_ENCRYPTION=1"]):
                subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                                *flags, str(cpp), "-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
