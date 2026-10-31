"""Exercise production upload-writer aborts across concurrent SD sessions."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "main/app_web_files.cpp").read_text()
writer = source[source.index("struct UploadChunk"):source.index("void *psAlloc")]

HARNESS = r"""
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>
struct Target { bool sd; };
struct Queue { std::deque<std::vector<uint8_t>> items; };
using QueueHandle_t=Queue *;
using SemaphoreHandle_t=bool *;
constexpr int portMAX_DELAY=-1, pdTRUE=1;
inline int64_t esp_timer_get_time() { return 1; }
namespace BoardSd {
bool healthy=true; int depth=0, recoveryCalls=0;
void lock() { ++depth; }
void unlock() { --depth; }
bool transferHealthy() { assert(depth==1); return healthy; }
void requestRecovery() { assert(depth==1); healthy=false; ++recoveryCalls; }
}
int writes=0, failAt=0, failure=EIO;
size_t fakeWrite(const void *,size_t,size_t n,FILE *) {
    ++writes;
    if(writes==failAt) { errno=failure; return 0; }
    return n;
}
#define fwrite fakeWrite
bool finished=false;
void xSemaphoreGive(bool *s) { *s=true; }
void vTaskDelete(void *) {}
template<typename T> int xQueueReceive(Queue *q,T *out,int) {
    if(q->items.empty()) return 0;
    assert(q->items.front().size()==sizeof(T));
    const auto bytes=q->items.front(); q->items.pop_front();
    auto *dest=reinterpret_cast<uint8_t *>(out);
    for(size_t i=0;i<sizeof(T);++i) dest[i]=bytes[i];
    return pdTRUE;
}
template<typename T> int xQueueSend(Queue *q,const T *value,int) {
    const auto *bytes=reinterpret_cast<const uint8_t *>(value);
    q->items.emplace_back(bytes,bytes+sizeof(T)); return pdTRUE;
}
"""

CHECKS = r"""
void run(bool sd,int fail,bool initiallyHealthy,int expectedError,int expectedWrites) {
    writes=0; failAt=fail; finished=false;
    BoardSd::healthy=initiallyHealthy; BoardSd::recoveryCalls=0;
    Target target{sd}; Queue ready, available;
    UploadPipeline pipeline{};
    pipeline.target=&target; pipeline.ready=&ready; pipeline.available=&available;
    pipeline.finished=&finished;
    uint8_t buffer[4]{}; pipeline.buffers[0]=buffer; pipeline.buffers[1]=buffer;
    for(uint8_t i=0;i<3;++i) {
        UploadChunk chunk{uint8_t(i%2),4};
        xQueueSend(&ready,&chunk,0);
    }
    UploadChunk done{0,0}; xQueueSend(&ready,&done,0);
    errno=ENOSPC; // Stale errno must not turn a peer's recovery into a disk-full error.
    uploadWriterTask(&pipeline);
    assert(finished && pipeline.error==expectedError && writes==expectedWrites);
    assert(available.items.size()==3 && BoardSd::depth==0);
    assert(BoardSd::recoveryCalls==(sd && expectedError==EIO ? 1:0));
}
int main() {
    run(true,0,true,0,3);
    failure=EIO; run(true,2,true,EIO,2);
    run(true,0,false,EIO,0); // A peer failed: do not write another queued chunk.
    failure=ENOSPC; run(true,2,true,ENOSPC,2); // Full disk does not power-cycle the card.
    failure=EIO; run(false,2,true,EIO,2); // Internal flash does not recover SD.
}
"""

with tempfile.TemporaryDirectory(prefix="otr-upload-pipeline-") as directory:
    work = pathlib.Path(directory)
    cpp = work / "test.cpp"
    cpp.write_text(HARNESS + writer + CHECKS)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Upload pipeline: concurrent-session aborts and explicit I/O errors passed")
