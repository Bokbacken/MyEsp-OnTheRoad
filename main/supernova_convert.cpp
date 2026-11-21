#include "supernova_convert.h"
#include "hw_profile.h"

#if CONFIG_OTR_HW_PROFILE_P4_43

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "avi_muxer.h"
#include "board_sd.h"
#include "esp_cache.h"
#include "esp_extractor_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_muxer_default.h"
#include "esp_timer.h"
#include "esp_video_dec.h"
#include "esp_video_dec_default.h"
#include "esp_video_enc.h"
#include "esp_video_enc_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_wifi.h"

namespace SupernovaConvert {
namespace {

constexpr const char *TAG = "sn_conv";
constexpr uint32_t TARGET_FPS = 15;
constexpr int MAX_W = 800;
constexpr int MAX_H = 480;
constexpr uint8_t JPEG_QUALITY = 75;
constexpr uint32_t EXTRACTOR_POOL = 2 * 1024 * 1024;
constexpr size_t JPEG_BUF = 512 * 1024;

volatile State s_state = State::Idle;
volatile int s_percent = 0;
volatile bool s_cancel = false;
uint32_t s_startMs = 0;
uint32_t s_endMs = 0;
const char *s_error = "";
char s_mp4[192];
char s_avi[192];
char s_tmp[200];
TaskHandle_t s_task = nullptr;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void *psramAlloc(size_t align, size_t size)
{
    size = (size + 63) & ~(size_t)63;
    return heap_caps_aligned_calloc(std::max<size_t>(align, 64), 1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

struct Job {
    FILE *in = nullptr;
    esp_extractor_handle_t ex = nullptr;
    esp_video_dec_handle_t dec = nullptr;
    esp_video_enc_handle_t enc = nullptr;
    esp_muxer_handle_t mux = nullptr;
    uint8_t *scratch = nullptr;
    size_t scratchCap = 0;
    uint8_t *decBuf = nullptr;
    size_t decCap = 0;
    uint8_t *encIn = nullptr;
    uint8_t *encOut = nullptr;
    std::vector<uint16_t> colMap;
    bool registered = false;
};

int readCb(void *buffer, uint32_t size, void *ctx)
{
    FILE *f = static_cast<FILE *>(ctx);
    const size_t n = fread(buffer, 1, size, f);
    return n == 0 && size != 0 ? -1 : (int)n;
}

int seekCb(uint32_t pos, void *ctx) { return fseek(static_cast<FILE *>(ctx), (long)pos, SEEK_SET); }

uint32_t sizeCb(void *ctx)
{
    FILE *f = static_cast<FILE *>(ctx);
    const long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    const long end = ftell(f);
    fseek(f, cur, SEEK_SET);
    return end < 0 ? 0 : (uint32_t)end;
}

int urlPattern(char *path, int len, int)
{
    snprintf(path, len, "%s", s_tmp);
    return 0;
}

void *wOpen(char *path) { return fopen(path, "wb"); }
int wWrite(void *w, void *buf, int len) { return (int)fwrite(buf, 1, (size_t)len, static_cast<FILE *>(w)); }
int wSeek(void *w, uint64_t pos) { return fseek(static_cast<FILE *>(w), (long)pos, SEEK_SET); }
int wClose(void *w) { return fclose(static_cast<FILE *>(w)); }

bool ensureScratch(Job &j, size_t need)
{
    if (need <= j.scratchCap) return true;
    heap_caps_free(j.scratch);
    j.scratchCap = (need + 0xFFFF) & ~(size_t)0xFFFF;
    j.scratch = static_cast<uint8_t *>(heap_caps_malloc(j.scratchCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!j.scratch) { j.scratchCap = 0; return false; }
    return true;
}

bool isAnnexB(const uint8_t *d, uint32_t n)
{
    return (n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1) || (n >= 3 && d[0] == 0 && d[1] == 0 && d[2] == 1);
}

// Builds Annex-B SPS+PPS from avcC, or copies already Annex-B data.
size_t buildParamSets(const uint8_t *s, uint32_t n, uint8_t *out, size_t cap)
{
    if (!s || n == 0) return 0;
    if (isAnnexB(s, n)) {
        if (n > cap) return 0;
        memcpy(out, s, n);
        return n;
    }
    if (s[0] != 1 || n < 7) return 0;
    static const uint8_t sc[4] = {0, 0, 0, 1};
    size_t pos = 0, off = 5;
    for (int pass = 0; pass < 2; pass++) {
        if (off >= n) return pos;
        const int count = pass == 0 ? (s[off] & 0x1F) : s[off];
        off++;
        for (int i = 0; i < count; i++) {
            if (off + 2 > n) return pos;
            const uint32_t len = ((uint32_t)s[off] << 8) | s[off + 1];
            off += 2;
            if (off + len > n || pos + 4 + len > cap) return pos;
            memcpy(out + pos, sc, 4);
            memcpy(out + pos + 4, s + off, len);
            pos += 4 + len;
            off += len;
        }
    }
    return pos;
}

// Copies a sample into dst as Annex-B (converting 4-byte NAL lengths when needed). Returns the size.
size_t toAnnexB(const uint8_t *s, uint32_t n, uint8_t *dst)
{
    if (isAnnexB(s, n)) {
        memcpy(dst, s, n);
        return n;
    }
    size_t pos = 0, off = 0;
    while (off + 4 <= n) {
        const uint32_t len = ((uint32_t)s[off] << 24) | ((uint32_t)s[off + 1] << 16) | ((uint32_t)s[off + 2] << 8) | s[off + 3];
        off += 4;
        if (len == 0 || off + len > n) break;
        dst[pos++] = 0; dst[pos++] = 0; dst[pos++] = 0; dst[pos++] = 1;
        memcpy(dst + pos, s + off, len);
        pos += len;
        off += len;
    }
    return pos;
}

void buildColMap(Job &j, int sw, int dw)
{
    j.colMap.resize(dw);
    for (int x = 0; x < dw; x++) j.colMap[x] = (uint16_t)std::min(sw - 1, (int)((int64_t)x * sw / dw));
}

// Nearest-neighbour I420 -> UYVY422 with scaling.
void i420ToUyvy(const Job &j, const uint8_t *src, int stride, int srcRows, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    const uint8_t *yPlane = src;
    const uint8_t *uPlane = src + (size_t)stride * srcRows;
    const int cs = stride / 2;
    const uint8_t *vPlane = uPlane + (size_t)cs * (srcRows / 2);
    for (int y = 0; y < dh; y++) {
        const int sy = std::min(sh - 1, (int)((int64_t)y * sh / dh));
        const uint8_t *yr = yPlane + (size_t)sy * stride;
        const uint8_t *ur = uPlane + (size_t)(sy >> 1) * cs;
        const uint8_t *vr = vPlane + (size_t)(sy >> 1) * cs;
        uint8_t *o = dst + (size_t)y * dw * 2;
        for (int x = 0; x < dw; x += 2) {
            const int s0 = j.colMap[x], s1 = j.colMap[x + 1];
            o[0] = ur[s0 >> 1];
            o[1] = yr[s0];
            o[2] = vr[s0 >> 1];
            o[3] = yr[s1];
            o += 4;
        }
    }
}

void cleanup(Job &j)
{
    if (j.mux) esp_muxer_close(j.mux);
    if (j.enc) esp_video_enc_close(j.enc);
    if (j.dec) esp_video_dec_close(j.dec);
    if (j.ex) esp_extractor_close(j.ex);
    if (j.in) fclose(j.in);
    heap_caps_free(j.scratch);
    heap_caps_free(j.decBuf);
    heap_caps_free(j.encIn);
    heap_caps_free(j.encOut);
    if (j.registered) {
        esp_video_enc_unregister_default();
        esp_video_dec_unregister_default();
        esp_extractor_unregister_default();
    }
    j = Job{};
}

bool fail(const char *why)
{
    s_error = why;
    ESP_LOGE(TAG, "%s", why);
    return false;
}

bool convert()
{
    Job j;
    bool ok = false;
    do {
        j.in = fopen(s_mp4, "rb");
        if (!j.in) { fail("Cannot open MP4"); break; }
        setvbuf(j.in, nullptr, _IOFBF, 16 * 1024);
        esp_extractor_register_default();
        esp_video_dec_register_default();
        esp_video_enc_register_default();
        esp_muxer_register_default();
        j.registered = true;

        esp_extractor_config_t xc{};
        xc.type = ESP_EXTRACTOR_TYPE_MP4;
        xc.extract_mask = ESP_EXTRACT_MASK_AV;
        xc.in_read_cb = readCb;
        xc.in_seek_cb = seekCb;
        xc.in_size_cb = sizeCb;
        xc.in_ctx = j.in;
        xc.out_pool_size = EXTRACTOR_POOL;
        if (esp_extractor_open(&xc, &j.ex) != ESP_EXTRACTOR_ERR_OK) { fail("MP4 open failed"); break; }
        if (esp_extractor_parse_stream(j.ex) != ESP_EXTRACTOR_ERR_OK) { fail("MP4 parse failed"); break; }

        uint16_t vn = 0, an = 0;
        esp_extractor_get_stream_num(j.ex, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &vn);
        esp_extractor_get_stream_num(j.ex, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, &an);
        esp_extractor_stream_info_t vi{}, ai{};
        if (vn == 0 || esp_extractor_get_stream_info(j.ex, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, 0, &vi) != ESP_EXTRACTOR_ERR_OK) {
            fail("No video track"); break;
        }
        if (vi.video_info.format != ESP_EXTRACTOR_VIDEO_FORMAT_H264) { fail("Video is not H.264"); break; }
        esp_extractor_enable_stream(j.ex, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, 0, true);
        bool haveAudio = false;
        esp_muxer_audio_codec_t aCodec = ESP_MUXER_ADEC_NONE;
        if (an > 0 && esp_extractor_get_stream_info(j.ex, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, 0, &ai) == ESP_EXTRACTOR_ERR_OK) {
            if (ai.audio_info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC) aCodec = ESP_MUXER_ADEC_AAC;
            else if (ai.audio_info.format == ESP_EXTRACTOR_AUDIO_FORMAT_MP3) aCodec = ESP_MUXER_ADEC_MP3;
            if (aCodec == ESP_MUXER_ADEC_NONE) { fail("Unsupported audio format"); break; }
            esp_extractor_enable_stream(j.ex, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, 0, true);
            haveAudio = true;
        }

        // Output size: fit inside 800x480, multiples of 16, never upscaled.
        const int sw = vi.video_info.width, sh = vi.video_info.height;
        if (sw < 16 || sh < 16) { fail("Bad video size"); break; }
        int dw = sw, dh = sh;
        if (dw > MAX_W || dh > MAX_H) {
            if ((int64_t)dw * MAX_H > (int64_t)dh * MAX_W) { dh = (int)((int64_t)dh * MAX_W / dw); dw = MAX_W; }
            else { dw = (int)((int64_t)dw * MAX_H / dh); dh = MAX_H; }
        }
        dw &= ~15;
        dh &= ~15;
        if (dw < 16 || dh < 16) { fail("Bad video size"); break; }
        const uint32_t fps = vi.video_info.fps > 0 ? std::min<uint32_t>(vi.video_info.fps, TARGET_FPS) : TARGET_FPS;
        ESP_LOGI(TAG, "%dx%d@%u -> %dx%d@%u, audio %s", sw, sh, (unsigned)vi.video_info.fps, dw, dh, (unsigned)fps,
                 haveAudio ? (aCodec == ESP_MUXER_ADEC_AAC ? "AAC" : "MP3") : "none");

        esp_video_dec_cfg_t dc{};
        dc.codec_type = ESP_VIDEO_CODEC_TYPE_H264;
        dc.out_fmt = ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P;
        if (esp_video_dec_open(&dc, &j.dec) != ESP_VC_ERR_OK) { fail("H.264 decoder open failed"); break; }
        uint8_t decAlign = 0, decInAlign = 0;
        esp_video_dec_get_frame_align(j.dec, &decInAlign, &decAlign);
        j.decCap = ((size_t)(sw + 15) & ~15) * ((sh + 15) & ~15) * 3 / 2 + 4096;
        j.decBuf = static_cast<uint8_t *>(psramAlloc(decAlign, j.decCap));
        if (!j.decBuf) { fail("Out of memory"); break; }

        esp_video_enc_cfg_t ec{};
        ec.codec_type = ESP_VIDEO_CODEC_TYPE_MJPEG;
        ec.in_fmt = ESP_VIDEO_CODEC_PIXEL_FMT_UYVY422;
        ec.resolution = {(uint32_t)dw, (uint32_t)dh};
        ec.fps = fps;
        if (esp_video_enc_open(&ec, &j.enc) != ESP_VC_ERR_OK) { fail("MJPEG encoder open failed"); break; }
        esp_video_enc_set_quality(j.enc, JPEG_QUALITY);
        uint8_t encInAlign = 0, encOutAlign = 0;
        esp_video_enc_get_frame_align(j.enc, &encInAlign, &encOutAlign);
        const size_t inSize = (size_t)dw * dh * 2;
        j.encIn = static_cast<uint8_t *>(psramAlloc(encInAlign, inSize));
        j.encOut = static_cast<uint8_t *>(psramAlloc(encOutAlign, JPEG_BUF));
        if (!j.encIn || !j.encOut) { fail("Out of memory"); break; }
        buildColMap(j, sw, dw);

        avi_muxer_config_t mc{};
        mc.base_config.muxer_type = ESP_MUXER_TYPE_AVI;
        mc.base_config.slice_duration = ESP_MUXER_MAX_SLICE_DURATION;
        mc.base_config.url_pattern = urlPattern;
        mc.base_config.ram_cache_size = 32 * 1024;
        mc.base_config.no_key_frame_verify = true;
        mc.index_type = AVI_MUXER_INDEX_AT_END;
        snprintf(s_tmp, sizeof(s_tmp), "%s.tmp", s_avi);
        j.mux = esp_muxer_open(&mc.base_config, sizeof(mc));
        if (!j.mux) { fail("AVI muxer open failed"); break; }
        esp_muxer_file_writer_t fw{wOpen, wWrite, wSeek, wClose};
        esp_muxer_set_file_writer(j.mux, &fw);
        esp_muxer_video_stream_info_t mv{};
        mv.codec = ESP_MUXER_VDEC_MJPEG;
        mv.width = (uint16_t)dw;
        mv.height = (uint16_t)dh;
        mv.fps = (uint8_t)fps;
        mv.min_packet_duration = 1000 / fps;
        int vIdx = 0, aIdx = 0;
        if (esp_muxer_add_video_stream(j.mux, &mv, &vIdx) != ESP_MUXER_ERR_OK) { fail("AVI video stream failed"); break; }
        if (haveAudio) {
            esp_muxer_audio_stream_info_t ma{};
            ma.codec = aCodec;
            ma.channel = ai.audio_info.channel;
            ma.bits_per_sample = ai.audio_info.bits_per_sample ? ai.audio_info.bits_per_sample : 16;
            ma.sample_rate = ai.audio_info.sample_rate;
            ma.min_packet_duration = ai.audio_info.sample_rate ? 1024u * 1000u / ai.audio_info.sample_rate : 23;
            ma.codec_spec_info = aCodec == ESP_MUXER_ADEC_AAC ? ai.spec_info : nullptr;
            ma.spec_info_len = aCodec == ESP_MUXER_ADEC_AAC ? (int)ai.spec_info_len : 0;
            if (esp_muxer_add_audio_stream(j.mux, &ma, &aIdx) != ESP_MUXER_ERR_OK) { fail("AVI audio stream failed"); break; }
        }

        // SPS/PPS go in front of the first sample so the decoder always has them.
        uint8_t params[1024];
        const size_t paramLen = buildParamSets(vi.spec_info, vi.spec_info_len, params, sizeof(params));
        bool first = true;
        uint32_t outIdx = 0;
        const uint32_t durationMs = vi.duration ? vi.duration : (an ? ai.duration : 0);
        bool eos = false;
        bool bad = false;

        while (!eos && !bad) {
            if (s_cancel) { s_error = "Cancelled"; break; }
            esp_extractor_frame_info_t fi{};
            const esp_extractor_err_t r = esp_extractor_read_frame(j.ex, &fi);
            if (r == ESP_EXTRACTOR_ERR_EOS) break;
            if (r == ESP_EXTRACTOR_ERR_SKIPPED) continue;
            if (r == ESP_EXTRACTOR_ERR_WAITING_OUTPUT || r == ESP_EXTRACTOR_ERR_NEED_MORE_BUF) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            if (r != ESP_EXTRACTOR_ERR_OK) { fail("MP4 read failed"); bad = true; break; }
            eos = EXTRACTOR_IS_EOS(fi.frame_flag);
            if (fi.frame_buffer && fi.frame_size > 0) {
                if (fi.stream_type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO && haveAudio) {
                    esp_muxer_audio_packet_t ap{fi.frame_buffer, (int)fi.frame_size, fi.pts};
                    if (esp_muxer_add_audio_packet(j.mux, aIdx, &ap) != ESP_MUXER_ERR_OK) { fail("AVI audio write failed"); bad = true; }
                } else if (fi.stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
                    if (!ensureScratch(j, fi.frame_size + fi.frame_size / 2 + sizeof(params) + 64)) {
                        fail("Out of memory"); bad = true;
                    } else {
                        size_t n = 0;
                        if (first && paramLen) { memcpy(j.scratch, params, paramLen); n = paramLen; }
                        first = false;
                        n += toAnnexB(fi.frame_buffer, fi.frame_size, j.scratch + n);
                        size_t off = 0;
                        while (off < n && !bad) {
                            esp_video_dec_in_frame_t in{};
                            in.pts = in.dts = fi.pts;
                            in.data = j.scratch + off;
                            in.size = (uint32_t)(n - off);
                            esp_video_dec_out_frame_t out{};
                            out.data = j.decBuf;
                            out.size = (uint32_t)j.decCap;
                            esp_vc_err_t dr = esp_video_dec_process(j.dec, &in, &out);
                            if (dr == ESP_VC_ERR_BUF_NOT_ENOUGH) {
                                esp_video_codec_frame_info_t info{};
                                esp_video_dec_get_frame_info(j.dec, &info);
                                const size_t need = (size_t)info.res.width * info.res.height * 3 / 2 + 4096;
                                if (need <= j.decCap) { fail("Decoder buffer error"); bad = true; break; }
                                heap_caps_free(j.decBuf);
                                j.decCap = need;
                                j.decBuf = static_cast<uint8_t *>(psramAlloc(decAlign, need));
                                if (!j.decBuf) { fail("Out of memory"); bad = true; }
                                continue;
                            }
                            if (dr < 0) break;  // Skip a damaged sample
                            off += in.consumed;
                            if (out.decoded_size > 0) {
                                const uint32_t pts = fi.pts;
                                if ((uint64_t)pts * fps >= (uint64_t)outIdx * 1000) {
                                    esp_video_codec_frame_info_t info{};
                                    esp_video_dec_get_frame_info(j.dec, &info);
                                    const int stride = (int)info.res.width;
                                    const int rows = (int)info.res.height;
                                    int vw = info.origin_res.width ? (int)info.origin_res.width : stride;
                                    int vh = info.origin_res.height ? (int)info.origin_res.height : rows;
                                    vw = std::min(vw, stride);
                                    vh = std::min(vh, rows);
                                    if (vw != sw) buildColMap(j, vw, dw);
                                    i420ToUyvy(j, j.decBuf, stride, rows, vw, vh, j.encIn, dw, dh);
                                    esp_cache_msync(j.encIn, (inSize + 63) & ~(size_t)63, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                                    esp_video_enc_in_frame_t ei{};
                                    ei.pts = outIdx * 1000 / fps;
                                    ei.data = j.encIn;
                                    ei.size = (uint32_t)inSize;
                                    esp_video_enc_out_frame_t eo{};
                                    eo.data = j.encOut;
                                    eo.size = JPEG_BUF;
                                    if (esp_video_enc_process(j.enc, &ei, &eo) != ESP_VC_ERR_OK || eo.encoded_size == 0) {
                                        fail("JPEG encode failed"); bad = true; break;
                                    }
                                    esp_muxer_video_packet_t vp{};
                                    vp.data = eo.data;
                                    vp.len = (int)eo.encoded_size;
                                    vp.pts = ei.pts;
                                    vp.key_frame = true;
                                    if (esp_muxer_add_video_packet(j.mux, vIdx, &vp) != ESP_MUXER_ERR_OK) {
                                        fail("AVI video write failed"); bad = true; break;
                                    }
                                    outIdx++;
                                }
                            }
                            if (in.consumed == 0 && out.decoded_size == 0) break;
                        }
                    }
                }
                if (durationMs) s_percent = (int)std::min<uint64_t>(99, (uint64_t)fi.pts * 100 / durationMs);
            }
            esp_extractor_release_frame(j.ex, &fi);
            vTaskDelay(1);
        }
        if (bad || s_cancel) break;
        if (outIdx == 0) { fail("No video frames decoded"); break; }
        ok = true;
    } while (false);

    const bool closedOk = j.mux != nullptr;
    cleanup(j);
    if (ok && closedOk) {
        remove(s_avi);
        if (rename(s_tmp, s_avi) != 0) { fail("Rename failed"); ok = false; }
    }
    if (!ok) remove(s_tmp);
    return ok;
}

void worker(void *)
{
    s_startMs = nowMs();
    const bool sdOk = BoardSd::beginTransfer();
    bool wifiByUs = false;
    bool ok = false;
    if (!sdOk) {
        fail("SD card busy");
    } else {
        if (!MyWifi::suspended()) { MyWifi::setSuspended(true); wifiByUs = true; }
        ok = convert();
        BoardSd::endTransfer();
        if (wifiByUs) MyWifi::setSuspended(false);
    }
    s_endMs = nowMs();
    s_percent = ok ? 100 : s_percent;
    s_state = ok ? State::Done : (s_cancel ? State::Cancelled : State::Failed);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool start(const char *mp4Path, const char *aviPath)
{
    if (s_state == State::Running || !mp4Path || !aviPath) return false;
    snprintf(s_mp4, sizeof(s_mp4), "%s", mp4Path);
    snprintf(s_avi, sizeof(s_avi), "%s", aviPath);
    s_error = "";
    s_percent = 0;
    s_cancel = false;
    s_startMs = nowMs();
    s_endMs = 0;
    s_state = State::Running;
    if (xTaskCreate(worker, "sn_conv", 16384, nullptr, 4, &s_task) != pdPASS) {
        s_state = State::Failed;
        s_error = "Task start failed";
        return false;
    }
    return true;
}

State state() { return s_state; }
int percent() { return s_percent; }
uint32_t elapsedMs() { return (s_state == State::Running || s_endMs == 0 ? nowMs() : s_endMs) - s_startMs; }
const char *error() { return s_error; }
void cancel() { s_cancel = true; }
void reset() { if (s_state != State::Running) s_state = State::Idle; }

}  // namespace SupernovaConvert

#else

namespace SupernovaConvert {
bool start(const char *, const char *) { return false; }
State state() { return State::Idle; }
int percent() { return 0; }
uint32_t elapsedMs() { return 0; }
const char *error() { return "P4 only"; }
void cancel() {}
void reset() {}
}  // namespace SupernovaConvert

#endif
