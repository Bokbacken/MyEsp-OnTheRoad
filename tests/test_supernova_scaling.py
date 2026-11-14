#!/usr/bin/env python3
"""Exercise the player stream wrapper, including scaling failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "main/supernova_player.cpp").read_text()
start = source.index('extern "C" esp_video_render_err_t __real_esp_video_render_stream_open(')
end = source.index("\n#else", start)
wrapper = source[start:end]

stubs = r'''
#include <cassert>
#include "supernova_video_geometry.h"
using esp_video_render_handle_t = void *;
using esp_video_render_stream_handle_t = void *;
enum esp_video_render_err_t {
    ESP_VIDEO_RENDER_ERR_OK, ESP_VIDEO_RENDER_ERR_INVALID_ARG, ESP_VIDEO_RENDER_ERR_NO_MEM
};
struct Info { unsigned short width, height; };
struct esp_video_render_stream_info_t { Info info; };
struct esp_video_render_disp_info_t { unsigned short width, height; };
struct esp_video_render_rect_t { unsigned short x, y, width, height; };
namespace SupernovaPlayer { void *s_videoRender = reinterpret_cast<void *>(1); }
esp_video_render_rect_t configured{};
int closed = 0, calls = 0;
bool failOpen = false, failScale = false;
extern "C" esp_video_render_err_t __real_esp_video_render_stream_open(
    esp_video_render_handle_t, esp_video_render_stream_info_t *, esp_video_render_stream_handle_t *stream)
{
    if (failOpen) return ESP_VIDEO_RENDER_ERR_NO_MEM;
    *stream = reinterpret_cast<void *>(2);
    return ESP_VIDEO_RENDER_ERR_OK;
}
esp_video_render_err_t esp_video_render_get_display_info(void *, esp_video_render_disp_info_t *disp)
{
    *disp = {800, 480};
    return ESP_VIDEO_RENDER_ERR_OK;
}
esp_video_render_err_t esp_video_render_stream_set_disp_rect(void *, esp_video_render_rect_t *rect)
{
    ++calls;
    configured = *rect;
    return failScale ? ESP_VIDEO_RENDER_ERR_NO_MEM : ESP_VIDEO_RENDER_ERR_OK;
}
esp_video_render_err_t esp_video_render_stream_close(void *) { ++closed; return ESP_VIDEO_RENDER_ERR_OK; }
template <typename... Args> void logIgnore(Args...) {}
#define ESP_LOGI(...) logIgnore(__VA_ARGS__)
#define ESP_LOGE(...) logIgnore(__VA_ARGS__)
'''
test = r'''
int main() {
    esp_video_render_stream_info_t info{{400, 240}};
    esp_video_render_stream_handle_t stream = nullptr;
    assert(__wrap_esp_video_render_stream_open(SupernovaPlayer::s_videoRender, &info, &stream)
           == ESP_VIDEO_RENDER_ERR_OK);
    assert(stream && calls == 1 && configured.width == 800 && configured.height == 480);
    info.info = {640, 480};
    assert(__wrap_esp_video_render_stream_open(SupernovaPlayer::s_videoRender, &info, &stream)
           == ESP_VIDEO_RENDER_ERR_OK);
    assert(configured.x == 80 && configured.width == 640 && configured.height == 480);
    assert(__wrap_esp_video_render_stream_open(reinterpret_cast<void *>(3), &info, &stream)
           == ESP_VIDEO_RENDER_ERR_OK && calls == 2);
    failScale = true;
    assert(__wrap_esp_video_render_stream_open(SupernovaPlayer::s_videoRender, &info, &stream)
           == ESP_VIDEO_RENDER_ERR_NO_MEM && !stream && closed == 1);
    failOpen = true;
    assert(__wrap_esp_video_render_stream_open(SupernovaPlayer::s_videoRender, &info, &stream)
           == ESP_VIDEO_RENDER_ERR_NO_MEM && closed == 1);
}
'''

with tempfile.TemporaryDirectory(prefix="supernova-scaling-") as tmp:
    work = Path(tmp)
    cpp = work / "test.cpp"
    cpp.write_text(stubs + wrapper + test)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    f"-I{root / 'main'}", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Supernova scaling wrapper passed")
