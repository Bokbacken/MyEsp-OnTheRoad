#include "supernova_player.h"
#include "supernova_video_geometry.h"

#include "hw_profile.h"

#if CONFIG_OTR_HW_PROFILE_P4_43

#include <new>
#include <stdio.h>
#include <string.h>

#include "board_audio.h"
#include "board_sd.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_render.h"
#include "esp_extractor_defaults.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_video_ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_player.h"
#include "esp_video_dec_default.h"
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "media_lib_adapter.h"
#include "my_lvgl.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace SupernovaPlayer {
namespace {

constexpr int AUDIO_RATE = 22050;
constexpr size_t PCM_CAPACITY = 8192;
constexpr int DISPLAY_FPS = 30;
constexpr int32_t MAX_PATH_BYTES = 512;
static const char *TAG = "supernova";

struct AudioRing {
    int16_t samples[PCM_CAPACITY];
    size_t read = 0;
    size_t write = 0;
    size_t used = 0;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

    int push(const uint8_t *data, size_t bytes)
    {
        if (!data || bytes == 0 || bytes % sizeof(int16_t)) return -1;
        const int16_t *src = reinterpret_cast<const int16_t *>(data);
        size_t count = bytes / sizeof(int16_t);
        while (count > 0) {
            portENTER_CRITICAL(&mux);
            const size_t available = PCM_CAPACITY - used;
            const size_t chunk = count < available ? count : available;
            for (size_t i = 0; i < chunk; i++) {
                samples[write] = src[i];
                write = (write + 1) % PCM_CAPACITY;
            }
            used += chunk;
            portEXIT_CRITICAL(&mux);

            src += chunk;
            count -= chunk;
            if (count > 0 && chunk == 0) {
                // Backpressure keeps decoded PTS from racing ahead of I2S playback.
                vTaskDelay(1);
            }
        }
        return 0;
    }

    void render(int32_t *acc, int count)
    {
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < count; i++) {
            if (!used) break;
            acc[i] += samples[read];
            read = (read + 1) % PCM_CAPACITY;
            used--;
        }
        portEXIT_CRITICAL(&mux);
    }

    void clear()
    {
        portENTER_CRITICAL(&mux);
        read = write = used = 0;
        portEXIT_CRITICAL(&mux);
    }
};

struct LvBackend {
    lv_display_t *display = nullptr;
    lv_obj_t *image = nullptr;
    lv_image_dsc_t descriptor{};
    esp_video_render_fb_t fb{};
    bool bound = false;
    int64_t statsStartUs = 0;
    int64_t previousFrameUs = 0;
    uint64_t lockWaitUs = 0;
    uint64_t writeUs = 0;
    uint32_t lockCount = 0;
    uint32_t frameCount = 0;
    uint32_t maxLockUs = 0;
    uint32_t maxWriteUs = 0;
    uint32_t maxGapUs = 0;
};

AudioRing s_audioRing;
esp_gmf_pool_handle_t s_audioPool = nullptr;
esp_gmf_pool_handle_t s_videoPool = nullptr;
esp_audio_render_handle_t s_audioRender = nullptr;
esp_video_render_handle_t s_videoRender = nullptr;
esp_player_handle_t s_audioPlayer = nullptr;
esp_player_handle_t s_videoPlayer = nullptr;
bool s_defaultsRegistered = false;
bool s_sdTransfer = false;
bool s_paused = false;
bool s_started = false;
char s_error[96] = "";

int audioWriter(uint8_t *pcm, uint32_t len, void *)
{
    return s_audioRing.push(pcm, len);
}

void audioSource(int32_t *acc, int count, int gainQ8, void *)
{
    s_audioRing.render(acc, count);
    if (gainQ8 != 256) {
        for (int i = 0; i < count; i++) acc[i] = (acc[i] * gainQ8) >> 8;
    }
}

esp_video_render_err_t backendInit(void *cfg, int cfgSize, esp_video_render_backend_handle_t *out)
{
    if (!cfg || !out || cfgSize < (int)sizeof(esp_video_render_lvgl_cfg_t)) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    auto *config = static_cast<esp_video_render_lvgl_cfg_t *>(cfg);
    if (!config->lv_disp || config->width == 0 || config->height == 0) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    auto *backend = new (std::nothrow) LvBackend;
    if (!backend) return ESP_VIDEO_RENDER_ERR_NO_MEM;
    backend->display = static_cast<lv_display_t *>(config->lv_disp);
    if (!MyLvgl::lock(1000)) {
        delete backend;
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }
    backend->image = lv_image_create(lv_display_get_screen_active(backend->display));
    if (backend->image) {
        lv_obj_set_size(backend->image, config->width, config->height);
        lv_obj_set_pos(backend->image, 0, 0);
    }
    MyLvgl::unlock();
    if (!backend->image) {
        delete backend;
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    backend->fb.info = {config->out_format, config->width, config->height, 0};
    backend->fb.size = (uint32_t)config->width * config->height * 2;
    backend->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    backend->descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    backend->descriptor.header.w = config->width;
    backend->descriptor.header.h = config->height;
    backend->descriptor.header.stride = config->width * 2;
    backend->descriptor.data_size = backend->fb.size;
    *out = backend;
    return ESP_VIDEO_RENDER_ERR_OK;
}

bool backendWithGram(esp_video_render_backend_handle_t)
{
    return true;
}

esp_video_render_err_t backendDisplayInfo(esp_video_render_backend_handle_t handle,
                                          esp_video_render_disp_info_t *info)
{
    if (!handle || !info) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    auto *backend = static_cast<LvBackend *>(handle);
    info->format = backend->fb.info.format;
    info->width = backend->fb.info.width;
    info->height = backend->fb.info.height;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t backendGetFb(esp_video_render_backend_handle_t handle, esp_video_render_fb_t *fb)
{
    if (!handle || !fb) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    auto *backend = static_cast<LvBackend *>(handle);
    if (!backend->fb.data) {
        backend->fb.data = static_cast<uint8_t *>(
            heap_caps_aligned_alloc(64, backend->fb.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!backend->fb.data) return ESP_VIDEO_RENDER_ERR_NO_MEM;
        memset(backend->fb.data, 0, backend->fb.size);
    }
    *fb = backend->fb;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t backendLockFb(esp_video_render_backend_handle_t handle, esp_video_render_fb_t *, bool lock)
{
    if (!handle) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    if (lock) {
        const int64_t start = esp_timer_get_time();
        if (!MyLvgl::lock(1000)) return ESP_VIDEO_RENDER_ERR_TIMEOUT;
        auto *backend = static_cast<LvBackend *>(handle);
        const uint32_t wait = (uint32_t)(esp_timer_get_time() - start);
        backend->lockWaitUs += wait;
        backend->lockCount++;
        if (wait > backend->maxLockUs) backend->maxLockUs = wait;
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    MyLvgl::unlock();
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t backendWriteFb(esp_video_render_backend_handle_t handle, esp_video_render_fb_t *fb,
                                      const esp_video_render_rect_t *, const esp_video_render_pos_t *pos)
{
    if (!handle || !fb || !fb->data) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    auto *backend = static_cast<LvBackend *>(handle);
    if (fb->size != backend->fb.size || fb->info.width != backend->fb.info.width ||
        fb->info.height != backend->fb.info.height || fb->info.format != backend->fb.info.format)
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    const int64_t waitStart = esp_timer_get_time();
    if (!MyLvgl::lock(1000)) return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    const int64_t writeStart = esp_timer_get_time();
    const uint32_t wait = (uint32_t)(writeStart - waitStart);
    backend->lockWaitUs += wait;
    backend->lockCount++;
    if (wait > backend->maxLockUs) backend->maxLockUs = wait;
    esp_video_render_fb_t ownedFb{};
    const esp_video_render_err_t result = backendGetFb(handle, &ownedFb);
    if (result != ESP_VIDEO_RENDER_ERR_OK) {
        MyLvgl::unlock();
        return result;
    }
    if (fb->data != backend->fb.data) memcpy(backend->fb.data, fb->data, backend->fb.size);
    backend->descriptor.data = backend->fb.data;
    if (pos) lv_obj_set_pos(backend->image, pos->x, pos->y);
    if (!backend->bound) {
        lv_image_set_src(backend->image, &backend->descriptor);
        backend->bound = true;
    } else {
        lv_obj_invalidate(backend->image);
    }
    const int64_t now = esp_timer_get_time();
    const uint32_t duration = (uint32_t)(now - writeStart);
    backend->writeUs += duration;
    if (duration > backend->maxWriteUs) backend->maxWriteUs = duration;
    if (!backend->statsStartUs) backend->statsStartUs = now;
    if (backend->previousFrameUs) {
        const uint32_t gap = (uint32_t)(now - backend->previousFrameUs);
        if (gap > backend->maxGapUs) backend->maxGapUs = gap;
    }
    backend->previousFrameUs = now;
    backend->frameCount++;
    if (now - backend->statsStartUs >= 5000000) {
        ESP_LOGI(TAG, "video %u frames/%u ms, LVGL lock avg/max %u/%u ms, write avg/max %u/%u ms, max frame gap %u ms",
                 (unsigned)backend->frameCount, (unsigned)((now - backend->statsStartUs) / 1000),
                 (unsigned)(backend->lockWaitUs / backend->lockCount / 1000),
                 (unsigned)(backend->maxLockUs / 1000),
                 (unsigned)(backend->writeUs / backend->frameCount / 1000),
                 (unsigned)(backend->maxWriteUs / 1000), (unsigned)(backend->maxGapUs / 1000));
        backend->statsStartUs = now;
        backend->lockWaitUs = backend->writeUs = 0;
        backend->lockCount = backend->frameCount = 0;
        backend->maxLockUs = backend->maxWriteUs = backend->maxGapUs = 0;
    }
    MyLvgl::unlock();
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t backendDeinit(esp_video_render_backend_handle_t handle)
{
    if (!handle) return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    auto *backend = static_cast<LvBackend *>(handle);
    if (MyLvgl::lock(1000)) {
        if (backend->image) lv_obj_delete(backend->image);
        MyLvgl::unlock();
    }
    heap_caps_free(backend->fb.data);
    delete backend;
    return ESP_VIDEO_RENDER_ERR_OK;
}

const esp_video_render_backend_ops_t s_backendOps = {
    .init = backendInit,
    .with_gram = backendWithGram,
    .get_display_info = backendDisplayInfo,
    .get_fb = backendGetFb,
    .lock_fb = backendLockFb,
    .write_fb = backendWriteFb,
    .deinit = backendDeinit,
};

void cleanup()
{
    if (s_audioPlayer) {
        esp_player_deinit(s_audioPlayer);
        s_audioPlayer = nullptr;
    }
    if (s_videoPlayer) {
        esp_player_deinit(s_videoPlayer);
        s_videoPlayer = nullptr;
    }
    Audio::setExternal(nullptr, nullptr);
    s_audioRing.clear();
    if (s_audioRender) {
        esp_audio_render_destroy(s_audioRender);
        s_audioRender = nullptr;
    }
    if (s_videoRender) {
        esp_video_render_destroy(s_videoRender);
        s_videoRender = nullptr;
    }
    if (s_audioPool) {
        esp_gmf_pool_deinit(s_audioPool);
        s_audioPool = nullptr;
    }
    if (s_videoPool) {
        esp_gmf_pool_deinit(s_videoPool);
        s_videoPool = nullptr;
    }
    if (s_defaultsRegistered) {
        esp_video_dec_unregister_default();
        esp_audio_dec_unregister_default();
        esp_extractor_unregister_default();
        s_defaultsRegistered = false;
    }
    if (s_sdTransfer) {
        BoardSd::endTransfer();
        s_sdTransfer = false;
    }
    s_started = s_paused = false;
}

bool makeAudioRender()
{
    if (esp_gmf_pool_init(&s_audioPool) != ESP_GMF_ERR_OK) return false;
    esp_gmf_element_handle_t el = nullptr;
    esp_ae_ch_cvt_cfg_t chCfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    if (esp_gmf_ch_cvt_init(&chCfg, &el) != ESP_GMF_ERR_OK ||
        esp_gmf_pool_register_element(s_audioPool, el, nullptr) != ESP_GMF_ERR_OK) return false;
    el = nullptr;
    esp_ae_bit_cvt_cfg_t bitCfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    if (esp_gmf_bit_cvt_init(&bitCfg, &el) != ESP_GMF_ERR_OK ||
        esp_gmf_pool_register_element(s_audioPool, el, nullptr) != ESP_GMF_ERR_OK) return false;
    el = nullptr;
    esp_ae_rate_cvt_cfg_t rateCfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    if (esp_gmf_rate_cvt_init(&rateCfg, &el) != ESP_GMF_ERR_OK ||
        esp_gmf_pool_register_element(s_audioPool, el, nullptr) != ESP_GMF_ERR_OK) return false;
    el = nullptr;
    esp_ae_alc_cfg_t alcCfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    if (esp_gmf_alc_init(&alcCfg, &el) != ESP_GMF_ERR_OK ||
        esp_gmf_pool_register_element(s_audioPool, el, nullptr) != ESP_GMF_ERR_OK) return false;

    esp_audio_render_cfg_t cfg{};
    cfg.max_stream_num = 1;
    cfg.out_writer = audioWriter;
    cfg.out_sample_info.sample_rate = AUDIO_RATE;
    cfg.out_sample_info.bits_per_sample = 16;
    cfg.out_sample_info.channel = 1;
    cfg.pool = s_audioPool;
    cfg.process_period = 20;
    if (esp_audio_render_create(&cfg, &s_audioRender) != ESP_AUDIO_RENDER_ERR_OK) return false;
    return true;
}

bool makeVideoRender()
{
    if (esp_gmf_pool_init(&s_videoPool) != ESP_GMF_ERR_OK) return false;
    esp_gmf_element_handle_t el = nullptr;
    if (esp_gmf_video_ppa_init(nullptr, &el) != ESP_GMF_ERR_OK ||
        esp_gmf_pool_register_element(s_videoPool, el, nullptr) != ESP_GMF_ERR_OK) return false;
    el = nullptr;
    esp_imgfx_color_convert_cfg_t colorCfg{};
    colorCfg.color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601;
    if (esp_gmf_video_color_convert_init(&colorCfg, &el) != ESP_GMF_ERR_OK ||
        esp_gmf_pool_register_element(s_videoPool, el, nullptr) != ESP_GMF_ERR_OK) return false;

    esp_video_render_cfg_t cfg = {.pool = s_videoPool, .fps = DISPLAY_FPS};
    if (esp_video_render_create(&cfg, &s_videoRender) != ESP_VIDEO_RENDER_ERR_OK) return false;
    lv_display_t *display = MyLvgl::display();
    if (!display) return false;
    esp_video_render_lvgl_cfg_t lvCfg = {
        .lv_disp = display,
        .out_format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = (uint16_t)lv_display_get_horizontal_resolution(display),
        .height = (uint16_t)lv_display_get_vertical_resolution(display),
    };
    esp_video_render_backend_cfg_t backendCfg = {
        .ops = &s_backendOps,
        .cfg = &lvCfg,
        .cfg_size = sizeof(lvCfg),
    };
    return esp_video_render_set_display(s_videoRender, &backendCfg) == ESP_VIDEO_RENDER_ERR_OK;
}

}  // namespace

bool start(const char *audioPath, const char *videoPath)
{
    stop();
    s_error[0] = '\0';
    const bool separateAudio = audioPath && audioPath[0] != '\0';
    if (!videoPath || (separateAudio && strlen(audioPath) >= MAX_PATH_BYTES) ||
        strlen(videoPath) >= MAX_PATH_BYTES) {
        snprintf(s_error, sizeof(s_error), "Invalid song media paths");
        return false;
    }
    if (!BoardSd::beginTransfer()) {
        snprintf(s_error, sizeof(s_error), "SD card is not ready");
        return false;
    }
    s_sdTransfer = true;
    media_lib_add_default_adapter();
    esp_extractor_register_default();
    esp_audio_dec_register_default();
    esp_video_dec_register_default();
    s_defaultsRegistered = true;

    if (!makeAudioRender() || !makeVideoRender()) {
        snprintf(s_error, sizeof(s_error), "Could not initialize media renderers");
        cleanup();
        return false;
    }
    esp_audio_render_stream_handle_t audioStream = nullptr;
    if (esp_audio_render_stream_get(s_audioRender, ESP_AUDIO_RENDER_FIRST_STREAM, &audioStream) != ESP_AUDIO_RENDER_ERR_OK ||
        !audioStream) {
        snprintf(s_error, sizeof(s_error), "Could not open audio renderer");
        cleanup();
        return false;
    }

    if (separateAudio) {
        esp_player_config_t audioCfg = ESP_PLAYER_CONFIG_DEFAULT();
        audioCfg.audio_render_hd = audioStream;
        if (esp_player_init(&audioCfg, &s_audioPlayer) != ESP_PLAYER_ERR_OK) {
            snprintf(s_error, sizeof(s_error), "Could not initialize MP3 player");
            cleanup();
            return false;
        }
    }
    esp_player_config_t videoCfg = ESP_PLAYER_CONFIG_DEFAULT();
    videoCfg.video_render_hd = s_videoRender;
    if (!separateAudio) videoCfg.audio_render_hd = audioStream;
    if (esp_player_init(&videoCfg, &s_videoPlayer) != ESP_PLAYER_ERR_OK) {
        snprintf(s_error, sizeof(s_error), "Could not initialize MP4 player");
        cleanup();
        return false;
    }
    if ((separateAudio &&
         (esp_player_set_av_mask(s_audioPlayer, ESP_PLAYER_MASK_AUDIO) != ESP_PLAYER_ERR_OK ||
          esp_player_set_url(s_audioPlayer, audioPath) != ESP_PLAYER_ERR_OK)) ||
        esp_player_set_av_mask(s_videoPlayer, separateAudio ? ESP_PLAYER_MASK_VIDEO
                                                            : (ESP_PLAYER_MASK_AUDIO | ESP_PLAYER_MASK_VIDEO)) != ESP_PLAYER_ERR_OK ||
        esp_player_set_url(s_videoPlayer, videoPath) != ESP_PLAYER_ERR_OK) {
        snprintf(s_error, sizeof(s_error), "Could not open song media");
        cleanup();
        return false;
    }
    s_audioRing.clear();
    Audio::setExternal(audioSource, nullptr);
    if ((separateAudio && esp_player_run(s_audioPlayer) != ESP_PLAYER_ERR_OK) ||
        esp_player_run(s_videoPlayer) != ESP_PLAYER_ERR_OK) {
        snprintf(s_error, sizeof(s_error), "Could not start song media");
        cleanup();
        return false;
    }
    s_started = true;
    s_paused = false;
    ESP_LOGI(TAG, "Started %s and video playback",
             separateAudio ? "separate MP3" : "embedded audio");
    return true;
}

void stop()
{
    if (s_audioPlayer || s_videoPlayer || s_audioRender || s_videoRender || s_audioPool || s_videoPool ||
        s_defaultsRegistered || s_sdTransfer)
        cleanup();
}

bool pause()
{
    if (!s_started || s_paused) return false;
    const bool audioOk = !s_audioPlayer || esp_player_pause(s_audioPlayer) == ESP_PLAYER_ERR_OK;
    const bool videoOk = esp_player_pause(s_videoPlayer) == ESP_PLAYER_ERR_OK;
    if (audioOk && videoOk) {
        s_paused = true;
        return true;
    }
    if (s_audioPlayer && audioOk) esp_player_resume(s_audioPlayer);
    if (videoOk) esp_player_resume(s_videoPlayer);
    snprintf(s_error, sizeof(s_error), "Could not pause song media");
    return false;
}

bool resume()
{
    if (!s_started || !s_paused) return false;
    const bool audioOk = !s_audioPlayer || esp_player_resume(s_audioPlayer) == ESP_PLAYER_ERR_OK;
    const bool videoOk = esp_player_resume(s_videoPlayer) == ESP_PLAYER_ERR_OK;
    if (audioOk && videoOk) {
        s_paused = false;
        return true;
    }
    if (s_audioPlayer && audioOk) esp_player_pause(s_audioPlayer);
    if (videoOk) esp_player_pause(s_videoPlayer);
    snprintf(s_error, sizeof(s_error), "Could not resume song media");
    return false;
}

uint32_t positionMs()
{
    esp_player_handle_t clockPlayer = s_audioPlayer ? s_audioPlayer : s_videoPlayer;
    if (!clockPlayer) return 0;
    uint64_t position = 0;
    if (esp_player_get_play_time(clockPlayer, &position) != ESP_PLAYER_ERR_OK) return 0;
    return position > UINT32_MAX ? UINT32_MAX : (uint32_t)position;
}

const char *error()
{
    return s_error;
}

}  // namespace SupernovaPlayer

extern "C" esp_video_render_err_t __real_esp_video_render_stream_open(
    esp_video_render_handle_t, esp_video_render_stream_info_t *, esp_video_render_stream_handle_t *);
extern "C" esp_video_render_err_t __wrap_esp_video_render_stream_open(
    esp_video_render_handle_t render, esp_video_render_stream_info_t *info,
    esp_video_render_stream_handle_t *stream)
{
    const esp_video_render_err_t result = __real_esp_video_render_stream_open(render, info, stream);
    if (result != ESP_VIDEO_RENDER_ERR_OK || render != SupernovaPlayer::s_videoRender) return result;

    esp_video_render_disp_info_t display{};
    esp_video_render_err_t status = esp_video_render_get_display_info(render, &display);
    if (status == ESP_VIDEO_RENDER_ERR_OK) {
        const auto fit = SupernovaVideo::fit(info->info.width, info->info.height, display.width, display.height);
        if (!fit.width || !fit.height) {
            status = ESP_VIDEO_RENDER_ERR_INVALID_ARG;
        } else {
            esp_video_render_rect_t rect{fit.x, fit.y, fit.width, fit.height};
            status = esp_video_render_stream_set_disp_rect(*stream, &rect);
            if (status == ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGI("supernova", "video scale %ux%u -> %ux%u at %u,%u",
                         (unsigned)info->info.width, (unsigned)info->info.height,
                         (unsigned)fit.width, (unsigned)fit.height, (unsigned)fit.x, (unsigned)fit.y);
            }
        }
    }
    if (status != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE("supernova", "Could not configure video scaling: %d", (int)status);
        esp_video_render_stream_close(*stream);
        *stream = nullptr;
    }
    return status;
}

#else

namespace SupernovaPlayer {
bool start(const char *, const char *) { return false; }
void stop() {}
bool pause() { return false; }
bool resume() { return false; }
uint32_t positionMs() { return 0; }
const char *error() { return "Video playback is available on P4 only"; }
}  // namespace SupernovaPlayer

#endif
