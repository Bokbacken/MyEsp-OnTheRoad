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
struct Descriptor { const uint8_t *data; };
struct LvBackend {
    void *image = nullptr;
    Descriptor descriptor{};
    esp_video_render_fb_t fb{};
    bool bound = false;
};
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
bool failAllocation = false;
int lockDepth = 0, binds = 0, invalidations = 0;
void *heap_caps_aligned_alloc(size_t, size_t size, int) {
    return failAllocation ? nullptr : malloc(size);
}
namespace MyLvgl {
bool lock(int) { ++lockDepth; return true; }
void unlock() { assert(lockDepth > 0); --lockDepth; }
}
void lv_obj_set_pos(void *, int, int) {}
void lv_image_set_src(void *, const Descriptor *) { ++binds; }
void lv_obj_invalidate(void *) { ++invalidations; }
'''
test = r'''
int main() {
    LvBackend backend;
    backend.fb.info = {1, 2, 2};
    backend.fb.size = 8;
    uint8_t pixels[] = {1, 2, 3, 4, 5, 6, 7, 8};
    esp_video_render_fb_t incoming = {backend.fb.info, 8, pixels};
    failAllocation = true;
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_NO_MEM);
    assert(!backend.fb.data && lockDepth == 0);
    failAllocation = false;
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(backend.fb.data && !memcmp(backend.fb.data, pixels, 8));
    assert(backend.descriptor.data == backend.fb.data && binds == 1 && lockDepth == 0);
    pixels[0] = 9;
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(backend.fb.data[0] == 9 && binds == 1 && invalidations == 1);
    incoming.info.width = 1;
    assert(backendWriteFb(&backend, &incoming, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_INVALID_ARG);
    esp_video_render_fb_t own{};
    assert(backendGetFb(&backend, &own) == ESP_VIDEO_RENDER_ERR_OK);
    assert(backendWriteFb(&backend, &own, nullptr, nullptr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(lockDepth == 0);
    free(backend.fb.data);
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
