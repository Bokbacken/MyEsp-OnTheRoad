#!/usr/bin/env python3
"""Exercise the actual framebuffer callbacks without ESP-IDF or LVGL."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "main/supernova_player.cpp").read_text()
start = source.index("esp_video_render_err_t backendGetFb(")
end = source.index("esp_video_render_err_t backendDeinit(", start)
callbacks = source[start:end]

stubs = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using esp_video_render_backend_handle_t = void *;
enum esp_video_render_err_t {
    ESP_VIDEO_RENDER_ERR_OK, ESP_VIDEO_RENDER_ERR_INVALID_ARG,
    ESP_VIDEO_RENDER_ERR_NO_MEM, ESP_VIDEO_RENDER_ERR_TIMEOUT
};
struct Info { int format; uint16_t width, height; };
struct esp_video_render_fb_t { Info info; uint32_t size; uint8_t *data; };
struct esp_video_render_rect_t {};
struct esp_video_render_pos_t { int x, y; };
struct Area { int x = 0, y = 0, w = 0, h = 0; };
Area s_videoRect;
int overlays = 0;
void blendOverlay(uint8_t *, int, const Area &) { ++overlays; }
constexpr size_t PCM_CAPACITY = 8192;
struct { void takeStats(uint32_t &u, size_t &m) { u = 0; m = PCM_CAPACITY; } } s_audioRing;
struct LvBackend {
    esp_video_render_fb_t fb{};
    int64_t statsStartUs = 0, previousFrameUs = 0;
    uint64_t writeUs = 0;
    uint32_t frameCount = 0, maxWriteUs = 0, maxGapUs = 0, failCount = 0;
};
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
bool failAllocation = false;
int blits = 0, reports = 0;
bool blitResult = true;
int64_t mockTime = 0;
int64_t esp_timer_get_time() { return ++mockTime; }
const char *TAG = "test";
template <typename... Args>
void logInfo(const char *, const char *, Args...) { ++reports; }
#define ESP_LOGI(...) logInfo(__VA_ARGS__)
void *heap_caps_aligned_alloc(size_t, size_t size, int) {
    return failAllocation ? nullptr : malloc(size);
}
namespace Display {
bool videoBlit(const void *, int, int, int, int, int, int) { ++blits; return blitResult; }
}
'''
test = r'''
int main() {
    LvBackend backend;
    backend.fb.info = {1, 2, 2};
    backend.fb.size = 8;
    uint8_t pixels[] = {1, 2, 3, 4, 5, 6, 7, 8};
    esp_video_render_fb_t incoming = {backend.fb.info, 8, pixels};
    failAllocation = true;
    esp_video_render_fb_t own{};
    assert(backendGetFb(&backend, &own) == ESP_VIDEO_RENDER_ERR_NO_MEM);
    failAllocation = false;
    assert(backendGetFb(&backend, &own) == ESP_VIDEO_RENDER_ERR_OK && own.data);
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(blits == 0);
    s_videoRect = {0, 0, 2, 2};
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(blits == 1 && overlays == 1 && backend.failCount == 0);
    blitResult = false;
    assert(backendWriteFb(&backend, &own, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(backend.failCount == 1);
    incoming.info.width = 1;
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_INVALID_ARG);
    assert(backendLockFb(&backend, &own, true) == ESP_VIDEO_RENDER_ERR_OK);
    assert(backendLockFb(&backend, &own, false) == ESP_VIDEO_RENDER_ERR_OK);
    mockTime += 5000000;
    assert(backendWriteFb(&backend, &own, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(reports == 1 && backend.frameCount == 0);
    free(own.data);
}
'''

with tempfile.TemporaryDirectory(prefix="supernova-video-backend-") as tmp:
    work = Path(tmp)
    cpp = work / "test.cpp"
    cpp.write_text(stubs + callbacks + test)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Supernova framebuffer callbacks passed")
