// board_audio.cpp - see board_audio.h.
// ES8311 register sequence follows Espressif's es8311 driver (esp-bsp, Apache-2.0): slave mode,
// MCLK from the MCLK pin at 256 fs, 16-bit I2S, DAC + output driver powered, EQ bypassed.
#include "board_audio.h"
#include "audio_paths.h"

#include <stdio.h>
#include <atomic>
#include <string.h>
#include <sys/stat.h>

#include "board_pins.h"
#include "board_sd.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "audio";

namespace Audio {

namespace {

constexpr int BLOCK = BLOCK_SAMPLES;     // Samples per mixer block (11.6 ms)
constexpr int FX_CHANNELS = 4;
constexpr uint32_t MAX_CLIP_BYTES = 2u * 1024 * 1024;   // Raw WAV data limit for effects
constexpr uint8_t DAC_VOL = 0xBF;        // 0 dB; software gains do the rest
constexpr uint8_t DAC_VOL_START = 0x40;  // Low level during bring-up

enum Cmd : uint8_t { CMD_SFX, CMD_MUSIC, CMD_INVALIDATE, CMD_FREE_CLIPS };
struct Msg {
    Cmd cmd;
    uint8_t arg;
};

struct Clip {
    int16_t *pcm = nullptr;
    uint32_t len = 0;
    uint8_t state = 0;   // 0 unknown, 1 no file, 2 loaded
};

struct FxChannel {
    bool active = false;
    bool isClip = false;
    const Clip *clip = nullptr;
    uint32_t pos = 0;
    uint32_t started = 0;
    Synth::Voice voice;
};

struct Stream {
    FILE *f = nullptr;
    Wav::Info info{};
    uint32_t remaining = 0;
    uint32_t gen = 0;
    Wav::Resampler rs;
    int16_t mono[512];
    size_t monoN = 0, monoPos = 0;
    uint8_t raw[2048];
};

i2c_master_dev_handle_t s_dev = nullptr;
i2s_chan_handle_t s_tx = nullptr;
QueueHandle_t s_queue = nullptr;
bool s_codec = false, s_running = false, s_ampOn = false;
volatile bool s_soundOn = true;
std::atomic<bool> s_sleepRequested{false}, s_sleeping{false};
volatile int s_sfxGain = 0, s_musicGain = 0;   // Q8
const char *s_error = "";
uint32_t s_underruns = 0;
volatile ExternalRender s_ext = nullptr;
void *volatile s_extCtx = nullptr;
volatile int s_extGain = 0;    // Q8, master switch applied
volatile uint8_t s_extVol = 70;
volatile uint32_t s_mixSeq = 0;   // Incremented after every mixer block
uint32_t s_extLoad100 = 0;        // Smoothed load in 1/100 %
// Render load statistics since the last takeExternalStats() (guarded by s_statMux)
portMUX_TYPE s_statMux = portMUX_INITIALIZER_UNLOCKED;
uint64_t s_extSum100 = 0;
uint32_t s_extCnt = 0, s_extMax100 = 0;
// v0.13.5 web karaoke: tap of the mixed mono output, and the speaker mute while a browser listens
volatile Tap s_tap = nullptr;
void *volatile s_tapCtx = nullptr;
volatile bool s_webStream = false;
bool s_webMuted = false;   // Mixer task: DAC volume currently at 0 for the web stream

// Mixer task state (only touched by the task)
FxChannel s_fx[FX_CHANNELS];
Clip s_clips[(int)Sound::Sfx::COUNT];
uint32_t s_clipGen = 0;
uint32_t s_counter = 0;
Synth::SongPlayer s_song;
Sound::Music s_music = Sound::Music::NONE;
Stream *s_stream = nullptr;   // PSRAM
bool s_musicFile = false;
int32_t s_acc[BLOCK];
int16_t s_out[BLOCK * 2];
int16_t s_mono[BLOCK];

// ---- ES8311 -----------------------------------------------------------------------------
bool wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, 50) == ESP_OK;
}

bool rd(uint8_t reg, uint8_t &val) { return i2c_master_transmit_receive(s_dev, &reg, 1, &val, 1, 50) == ESP_OK; }

bool codecInit()
{
    uint8_t v = 0;
    bool ok = wr(0x00, 0x1F);   // Reset
    vTaskDelay(pdMS_TO_TICKS(20));
    ok = ok && wr(0x00, 0x00) && wr(0x00, 0x80);   // Power-on command
    // Clock manager: all clocks on, MCLK from pin, not inverted
    ok = ok && wr(0x01, 0x3F);
    ok = ok && rd(0x06, v) && wr(0x06, (uint8_t)(v & ~0x20));
    // 22050 Hz at MCLK 5.6448 MHz (256 fs): pre_div 1, pre_multi x1, adc/dac div 1,
    // single speed, osr 0x10, bclk_div 4, lrck 0x00FF
    ok = ok && rd(0x02, v) && wr(0x02, (uint8_t)(v & 0x07));
    ok = ok && wr(0x03, 0x10) && wr(0x04, 0x10) && wr(0x05, 0x00);
    ok = ok && rd(0x06, v) && wr(0x06, (uint8_t)((v & 0xE0) | 0x03));
    ok = ok && rd(0x07, v) && wr(0x07, (uint8_t)(v & 0xC0));
    ok = ok && wr(0x08, 0xFF);
    // Serial port: slave, I2S, 16 bit in and out
    ok = ok && rd(0x00, v) && wr(0x00, (uint8_t)(v & 0xBF));
    ok = ok && wr(0x09, 0x0C) && wr(0x0A, 0x0C);
    // Power: analog, ADC modulator, DAC, output driver; ADC EQ bypass; DAC EQ bypass
    ok = ok && wr(0x0D, 0x01) && wr(0x0E, 0x02) && wr(0x12, 0x00) && wr(0x13, 0x10);
    ok = ok && wr(0x1C, 0x6A) && wr(0x37, 0x08);
    ok = ok && wr(0x32, DAC_VOL_START);
    ok = ok && rd(0x31, v) && wr(0x31, (uint8_t)(v & ~0x60));   // Unmute
    return ok;
}

void setAmp(bool on)
{
    if (on == s_ampOn) return;
    gpio_set_level((gpio_num_t)PIN_AMP_EN, on == AMP_EN_ACTIVE_HIGH ? 1 : 0);
    s_ampOn = on;
}

// ---- WAV files ------------------------------------------------------------------------------
void freeClips()
{
    for (auto &f : s_fx)
        if (f.isClip) f.active = false;
    for (auto &c : s_clips) {
        heap_caps_free(c.pcm);
        c = Clip();
    }
}

bool loadClip(Sound::Sfx id, Clip &c)
{
    c.state = 1;
    char path[80];
    snprintf(path, sizeof(path), "%s/otr/sounds/%s.wav", BoardSd::MOUNT, Sound::sfxName(id));
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Stream *tmp = (Stream *)heap_caps_calloc(1, sizeof(Stream), MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (tmp) {
        size_t n = fread(tmp->raw, 1, sizeof(tmp->raw), f);
        Wav::Info in;
        if (Wav::parse(tmp->raw, n, (uint32_t)st.st_size, in) && in.dataBytes <= MAX_CLIP_BYTES) {
            uint32_t outCap = (uint32_t)((uint64_t)in.frames() * Synth::RATE / in.rate) + 8;
            c.pcm = (int16_t *)heap_caps_malloc(outCap * 2, MALLOC_CAP_SPIRAM);
            if (c.pcm && fseek(f, (long)in.dataOffset, SEEK_SET) == 0) {
                tmp->rs.reset(in.rate);
                uint32_t left = in.dataBytes, frameB = in.channels * (in.bits / 8);
                c.len = 0;
                while (left > 0) {
                    size_t want = sizeof(tmp->mono) / 2 * frameB;   // <= 256 frames per read
                    if (want > left) want = left;
                    size_t got = fread(tmp->raw, 1, want, f);
                    if (got < frameB) break;
                    size_t frames = got / frameB;
                    left -= (uint32_t)(frames * frameB);
                    Wav::toMono16(tmp->raw, frames, in, tmp->mono);
                    size_t pos = 0;
                    while (pos < frames && c.len < outCap) {
                        size_t used = 0;
                        c.len += (uint32_t)tmp->rs.process(tmp->mono + pos, frames - pos, c.pcm + c.len, outCap - c.len, used);
                        pos += used;
                        if (!used) break;
                    }
                }
                ok = c.len > 0;
            }
            if (!ok) {
                heap_caps_free(c.pcm);
                c.pcm = nullptr;
                c.len = 0;
            }
        } else {
            ESP_LOGW(TAG, "%s: unsupported WAV (PCM 8/16 bit, mono/stereo, 8-48 kHz, max 2 MB)", path);
        }
        heap_caps_free(tmp);
    }
    fclose(f);
    if (ok) {
        c.state = 2;
        ESP_LOGI(TAG, "loaded %s (%u samples)", path, (unsigned)c.len);
    }
    return ok;
}

void closeStream()
{
    if (s_stream && s_stream->f) {
        BoardSd::Guard g;
        if (s_stream->gen == BoardSd::info().generation) fclose(s_stream->f);
        s_stream->f = nullptr;   // On a removed card the FILE died with the mount
    }
    s_musicFile = false;
}

bool openStream(Sound::Music m)
{
    closeStream();
    if (!s_stream) s_stream = (Stream *)heap_caps_calloc(1, sizeof(Stream), MALLOC_CAP_SPIRAM);
    if (!s_stream) return false;
    char path[80];
    snprintf(path, sizeof(path), "%s/otr/music/%s.wav", BoardSd::MOUNT, Sound::musicName(m));
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(s_stream->raw, 1, sizeof(s_stream->raw), f);
    Wav::Info in;
    if (!Wav::parse(s_stream->raw, n, (uint32_t)st.st_size, in) || in.frames() < 64 ||
        fseek(f, (long)in.dataOffset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "%s: unsupported WAV - using the built-in tune", path);
        fclose(f);
        return false;
    }
    s_stream->f = f;
    s_stream->info = in;
    s_stream->remaining = in.dataBytes;
    s_stream->gen = BoardSd::info().generation;
    s_stream->rs.reset(in.rate);
    s_stream->monoN = s_stream->monoPos = 0;
    s_musicFile = true;
    ESP_LOGI(TAG, "music from %s (%u Hz, %u ch, %u bit)", path, (unsigned)in.rate, in.channels, in.bits);
    return true;
}

// Refills the mono buffer from the file (loops at the end). False on a read error / removed card.
bool refill(Stream &s)
{
    BoardSd::Guard g;
    if (!s.f || !BoardSd::mounted() || s.gen != BoardSd::info().generation) return false;
    const uint32_t frameB = s.info.channels * (s.info.bits / 8);
    if (s.remaining < frameB) {
        if (fseek(s.f, (long)s.info.dataOffset, SEEK_SET) != 0) return false;
        s.remaining = s.info.dataBytes;
    }
    size_t want = sizeof(s.mono) / 2 * frameB;
    if (want > s.remaining) want = s.remaining;
    size_t got = fread(s.raw, 1, want, s.f);
    size_t frames = got / frameB;
    if (frames == 0) return false;
    s.remaining -= (uint32_t)(frames * frameB);
    Wav::toMono16(s.raw, frames, s.info, s.mono);
    s.monoN = frames;
    s.monoPos = 0;
    return true;
}

void renderStream(int gain)
{
    Stream &s = *s_stream;
    int16_t tmp[BLOCK];
    int done = 0, guard = 0;
    while (done < BLOCK && guard++ < 16) {
        if (s.monoPos >= s.monoN && !refill(s)) {
            // Card gone or broken file: fall back to the built-in tune
            closeStream();
            const Synth::Song *song = Sound::musicSong(s_music);
            if (song) s_song.start(*song);
            return;
        }
        size_t used = 0;
        size_t n = s.rs.process(s.mono + s.monoPos, s.monoN - s.monoPos, tmp + done, (size_t)(BLOCK - done), used);
        s.monoPos += used;
        done += (int)n;
    }
    for (int i = 0; i < done; i++) s_acc[i] += (tmp[i] * gain) >> 8;
}

// ---- Commands ---------------------------------------------------------------------------------
void startSfx(Sound::Sfx id)
{
    if ((int)id >= (int)Sound::Sfx::COUNT) return;
    uint32_t gen = BoardSd::info().generation;
    if (gen != s_clipGen) {
        freeClips();
        s_clipGen = gen;
    }
    Clip &c = s_clips[(int)id];
    if (c.state == 0) loadClip(id, c);
    // Free channel, else steal the oldest
    FxChannel *ch = &s_fx[0];
    for (auto &f : s_fx) {
        if (!f.active) { ch = &f; break; }
        if (f.started < ch->started) ch = &f;
    }
    ch->active = true;
    ch->started = ++s_counter;
    ch->pos = 0;
    if (c.state == 2) {
        ch->isClip = true;
        ch->clip = &c;
    } else {
        ch->isClip = false;
        ch->voice.start(Sound::sfxMml(id));
    }
}

void startMusic(Sound::Music m)
{
    if (m == s_music && (s_song.playing() || s_musicFile)) return;
    s_song.stop();
    closeStream();
    s_music = m;
    if (m == Sound::Music::NONE) return;
    if (openStream(m)) return;
    const Synth::Song *song = Sound::musicSong(m);
    if (song) s_song.start(*song);
}

void handle(const Msg &m)
{
    switch (m.cmd) {
    case CMD_SFX: startSfx((Sound::Sfx)m.arg); break;
    case CMD_MUSIC: startMusic((Sound::Music)m.arg); break;
    case CMD_INVALIDATE: {
        if (m.arg & AudioPaths::SOUNDS) freeClips();
        if (!(m.arg & AudioPaths::MUSIC)) break;
        Sound::Music cur = s_music;
        s_music = Sound::Music::NONE;
        s_song.stop();
        closeStream();
        startMusic(cur);
        break;
    }
    case CMD_FREE_CLIPS: freeClips(); break;
    }
}

void mixerTask(void *)
{
    // Ramp the DAC up behind the (silent) DMA stream, then enable the amplifier.
    for (int v = DAC_VOL_START; v <= DAC_VOL; v += 8) {
        wr(0x32, (uint8_t)(v > DAC_VOL ? DAC_VOL : v));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    wr(0x32, DAC_VOL);
    for (;;) {
        Msg m;
        while (xQueueReceive(s_queue, &m, 0) == pdTRUE) handle(m);
        if (s_sleepRequested && !s_sleeping) {
            setAmp(false);
            const esp_err_t err = i2s_channel_disable(s_tx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "idle I2S stop failed: %s", esp_err_to_name(err));
                s_sleepRequested = false;
            } else {
                s_sleeping = true;
                ESP_LOGI(TAG, "idle audio stopped");
            }
        }
        if (s_sleeping) {
            if (!s_sleepRequested) {
                const esp_err_t err = i2s_channel_enable(s_tx);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "idle I2S resume failed: %s", esp_err_to_name(err));
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
                s_sleeping = false;
                ESP_LOGI(TAG, "idle audio resumed");
            } else {
                s_mixSeq = s_mixSeq + 1;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
        }
        // Web stream: the browser plays the sound, the speaker is silent (DAC volume 0, amplifier
        // off) so it does not echo; the synth keeps running at its normal level for the stream
        const bool web = s_webStream;
        if (web != s_webMuted) {
            wr(0x32, web ? 0x00 : DAC_VOL);
            s_webMuted = web;
            ESP_LOGI(TAG, "web stream %s: speaker %s", web ? "on" : "off", web ? "muted" : "restored");
        }
        setAmp(s_soundOn && !web);

        memset(s_acc, 0, sizeof(s_acc));
        const int sg = s_sfxGain, mg = s_musicGain;
        const ExternalRender ext = s_ext;
        if (ext) {
            const int64_t t0 = esp_timer_get_time();
            ext(s_acc, BLOCK, (s_soundOn || web) ? s_extGain : 0, s_extCtx);
            const uint32_t pct100 = (uint32_t)((esp_timer_get_time() - t0) * 10000 / (BLOCK * 1000000LL / Synth::RATE));
            s_extLoad100 = (s_extLoad100 * 15 + pct100) / 16;
            portENTER_CRITICAL(&s_statMux);
            s_extSum100 += pct100;
            s_extCnt++;
            if (pct100 > s_extMax100) s_extMax100 = pct100;
            portEXIT_CRITICAL(&s_statMux);
        } else if (s_music != Sound::Music::NONE) {
            if (s_musicFile) renderStream(mg);
            else s_song.render(s_acc, BLOCK, mg);
        }
        for (auto &f : s_fx) {
            if (!f.active) continue;
            if (f.isClip) {
                uint32_t n = f.clip->len - f.pos;
                if (n > BLOCK) n = BLOCK;
                const int16_t *p = f.clip->pcm + f.pos;
                for (uint32_t i = 0; i < n; i++) s_acc[i] += (p[i] * sg) >> 8;
                f.pos += n;
                if (f.pos >= f.clip->len) f.active = false;
            } else if (!f.voice.render(s_acc, BLOCK, sg)) {
                f.active = false;
            }
        }
        for (int i = 0; i < BLOCK; i++) {
            int32_t v = s_acc[i];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            s_out[2 * i] = s_out[2 * i + 1] = s_mono[i] = (int16_t)v;
        }
        const Tap tap = s_tap;
        if (tap) tap(s_mono, BLOCK, s_tapCtx);
        s_mixSeq = s_mixSeq + 1;
        size_t written = 0;
        TickType_t t0 = xTaskGetTickCount();
        i2s_channel_write(s_tx, s_out, sizeof(s_out), &written, portMAX_DELAY);
        // Returning without waiting means the DMA queue had room for a whole block: the mixer
        // is behind (normal for the first blocks and right after a WAV load). Counted when it
        // persists, for diagnostics.
        static uint32_t s_fast = 0;
        if (xTaskGetTickCount() == t0) {
            if (++s_fast == 12) s_underruns++;
        } else {
            s_fast = 0;
        }
    }
}

void post(Cmd c, uint8_t arg)
{
    if (!s_running) return;
    Msg m{c, arg};
    xQueueSend(s_queue, &m, 0);
}

}  // namespace

bool begin(i2c_master_bus_handle_t bus)
{
    // 1. Amplifier off before anything else
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_AMP_EN;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_set_level((gpio_num_t)PIN_AMP_EN, AMP_EN_ACTIVE_HIGH ? 0 : 1);
    gpio_config(&io);
    gpio_set_level((gpio_num_t)PIN_AMP_EN, AMP_EN_ACTIVE_HIGH ? 0 : 1);
    s_ampOn = false;

    // 2. Codec on the shared I2C bus
    if (!bus || i2c_master_probe(bus, ES8311_I2C_ADDR, 50) != ESP_OK) {
        s_error = "ES8311 not found (I2C 0x18)";
        ESP_LOGW(TAG, "%s - running silent", s_error);
        return false;
    }
    s_codec = true;
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = ES8311_I2C_ADDR;
    dc.scl_speed_hz = I2C_FREQ_HZ;
    if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) {
        s_error = "I2C add device failed";
        ESP_LOGE(TAG, "%s", s_error);
        return false;
    }

    // 3. I2S (TX only), DMA running with silence
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    cc.dma_desc_num = 8;
    cc.dma_frame_num = 240;   // 8 x 240 frames = 87 ms of buffering
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(Synth::RATE);
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = (gpio_num_t)PIN_I2S_MCLK;
    sc.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCLK;
    sc.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
    sc.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
    sc.gpio_cfg.din = (gpio_num_t)PIN_I2S_DIN;
    if (i2s_new_channel(&cc, &s_tx, nullptr) != ESP_OK || i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK ||
        i2s_channel_enable(s_tx) != ESP_OK) {
        s_error = "I2S init failed";
        ESP_LOGE(TAG, "%s", s_error);
        return false;
    }

    // 4. Codec registers (MCLK is running now)
    if (!codecInit()) {
        s_error = "ES8311 init failed";
        ESP_LOGE(TAG, "%s - running silent", s_error);
        return false;
    }

    // 5. Mixer task; it ramps the DAC volume and switches the amplifier on
    s_queue = xQueueCreate(24, sizeof(Msg));
    if (!s_queue || xTaskCreatePinnedToCore(mixerTask, "audio", 8192, nullptr, 6, nullptr, 0) != pdPASS) {
        s_error = "mixer task failed";
        ESP_LOGE(TAG, "%s", s_error);
        return false;
    }
    s_running = true;
    ESP_LOGI(TAG, "ES8311 ready, %d Hz mono mixer, %d effect channels", Synth::RATE, FX_CHANNELS);
    return true;
}

bool running() { return s_running; }
void setSleeping(bool sleeping) { s_sleepRequested = sleeping; }
bool sleeping() { return s_sleeping; }

void setVolumes(bool soundOn, uint8_t sfxVol, bool musicOn, uint8_t musicVol)
{
    if (sfxVol > 100) sfxVol = 100;
    if (musicVol > 100) musicVol = 100;
    // Perceptual (square) curve; effects up to 2x, music up to 1.5x of the synth level
    s_sfxGain = soundOn ? sfxVol * sfxVol * 512 / 10000 : 0;
    s_musicGain = (soundOn && musicOn) ? musicVol * musicVol * 384 / 10000 : 0;
    s_soundOn = soundOn;
}

void setExternalVolume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    s_extVol = vol;
    s_extGain = vol * vol * 512 / 10000;   // Up to 2x the synth level (the SF2 synth has 14 dB headroom)
}

void setExternal(ExternalRender fn, void *ctx)
{
    if (!fn) {
        s_ext = nullptr;
        // Wait until the mixer finished a block without the old callback (max ~50 ms)
        if (s_running) {
            const uint32_t seq = s_mixSeq;
            for (int i = 0; i < 10 && (uint32_t)(s_mixSeq - seq) < 2; i++) vTaskDelay(pdMS_TO_TICKS(5));
        }
        s_extCtx = nullptr;
        s_extLoad100 = 0;
        return;
    }
    if (s_extGain == 0 && s_extVol) setExternalVolume(s_extVol);
    s_extCtx = ctx;
    s_ext = fn;
}

void setTap(Tap fn, void *ctx)
{
    if (!fn) {
        s_tap = nullptr;
        if (s_running) {   // Wait until the mixer no longer runs the old tap (max ~50 ms)
            const uint32_t seq = s_mixSeq;
            for (int i = 0; i < 10 && (uint32_t)(s_mixSeq - seq) < 2; i++) vTaskDelay(pdMS_TO_TICKS(5));
        }
        s_tapCtx = nullptr;
        return;
    }
    s_tapCtx = ctx;
    s_tap = fn;
}

void setWebStream(bool active) { s_webStream = active; }
bool webStream() { return s_webStream; }

void play(Sound::Sfx s)
{
    if (s_sfxGain > 0) post(CMD_SFX, (uint8_t)s);
}

void music(Sound::Music m) { post(CMD_MUSIC, (uint8_t)m); }
void stopMusic() { music(Sound::Music::NONE); }
void invalidateFiles(uint8_t what)
{
    if (what) post(CMD_INVALIDATE, what);
}

void releaseClips()
{
    if (!s_running) return;
    post(CMD_FREE_CLIPS, 0);
    // The command runs at the start of the next mixer block: wait for two blocks (max ~100 ms)
    const uint32_t seq = s_mixSeq;
    for (int i = 0; i < 20 && (uint32_t)(s_mixSeq - seq) < 2; i++) vTaskDelay(pdMS_TO_TICKS(5));
}

ExternalStats takeExternalStats()
{
    ExternalStats st{};
    portENTER_CRITICAL(&s_statMux);
    st.blocks = s_extCnt;
    st.avgLoad100 = s_extCnt ? (uint32_t)(s_extSum100 / s_extCnt) : 0;
    st.maxLoad100 = s_extMax100;
    s_extSum100 = 0, s_extCnt = 0, s_extMax100 = 0;
    portEXIT_CRITICAL(&s_statMux);
    st.underruns = s_underruns;
    return st;
}

Status status()
{
    Status st{};
    st.codecFound = s_codec;
    st.running = s_running;
    st.ampOn = s_ampOn;
    st.music = s_music;
    st.musicFromFile = s_musicFile;
    for (auto &f : s_fx) st.busyChannels += f.active ? 1 : 0;
    for (auto &c : s_clips) st.filesCached += c.state == 2 ? 1 : 0;
    st.underruns = s_underruns;
    st.external = s_ext != nullptr;
    st.webStream = s_webStream;
    st.externalLoad = (uint8_t)(s_extLoad100 / 100 > 255 ? 255 : s_extLoad100 / 100);
    st.error = s_error;
    return st;
}

}  // namespace Audio
