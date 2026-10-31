"""Exercise production HTTP-body accounting before async socket reuse."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "main/app_web_files.cpp").read_text()
body = source[source.index("struct UploadBody {"):source.index("esp_err_t sendUploadSkipped")]

HARNESS = r"""
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <deque>
#include <string>
constexpr int HTTPD_SOCK_ERR_TIMEOUT=-3;
constexpr int SHUT_RDWR=2;
int disconnected=0;
int shutdown(int fd,int mode) { assert(fd==7 && mode==SHUT_RDWR); ++disconnected; return 0; }
#define ESP_LOGW(...) ((void)0)
struct httpd_req_t {
    size_t content_len;
    std::string wire;
    size_t position=0;
    std::deque<int> results;
};
int httpd_req_to_sockfd(httpd_req_t *) { return 7; }
int httpd_req_recv(httpd_req_t *r,char *out,size_t n) {
    if(!r->results.empty()) {
        int result=r->results.front(); r->results.pop_front();
        if(result<=0) return result;
        n=std::min(n,size_t(result));
    }
    n=std::min(n,r->wire.size()-r->position);
    std::memcpy(out,r->wire.data()+r->position,n);
    r->position+=n;
    return int(n);
}
"""

CHECKS = r"""
int main() {
    const std::string midi="MThd"+std::string(8193,'x');
    const std::string next="GET /api/fs/list HTTP/1.1\r\n\r\n";
    for(int i=0;i<24;++i) {
        httpd_req_t r{midi.size(),midi+next,0,{}};
        UploadBody body(&r);
        r.results={HTTPD_SOCK_ERR_TIMEOUT,13,HTTPD_SOCK_ERR_TIMEOUT,25};
        assert(body.drain()); // Existing files: discard body, not the next HTTP request.
        assert(body.remaining==0 && r.wire.substr(r.position)==next);
        assert(body.drain() && r.position==midi.size());
    }
    httpd_req_t partial{midi.size(),midi+next,0,{}};
    UploadBody body(&partial);
    char buffer[512];
    assert(body.receive(buffer,sizeof(buffer))==512);
    assert(body.remaining==midi.size()-512);
    assert(body.drain() && partial.wire.substr(partial.position)==next);
    httpd_req_t empty{0,next,0,{}};
    UploadBody zero(&empty);
    assert(zero.drain() && empty.position==0);
    httpd_req_t timed{midi.size(),midi+next,0,{}};
    timed.results=std::deque<int>(5,HTTPD_SOCK_ERR_TIMEOUT);
    UploadBody timeout(&timed);
    assert(!timeout.drain() && timeout.failed && timed.position==0);
    assert(!timeout.drain()); // Must close, never reuse an incompletely drained socket.
    timeout.disconnect();
    assert(disconnected==1);
    httpd_req_t closed{10,"",0,{}};
    UploadBody eof(&closed);
    assert(!eof.drain() && eof.failed);
}
"""

with tempfile.TemporaryDirectory(prefix="otr-upload-body-") as directory:
    work = pathlib.Path(directory)
    cpp = work / "test.cpp"
    cpp.write_text(HARNESS + body + CHECKS)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Upload body: skipped/partial requests, keep-alive boundaries and failed drains passed")
