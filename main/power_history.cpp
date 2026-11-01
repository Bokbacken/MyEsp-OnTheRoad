#include "power_history.h"
#include "power_interval.h"
#include "power_io.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app_config.h"
#include "adv_live.h"
#include "battery_math.h"
#include "board_audio.h"
#include "board_backlight.h"
#include "board_battery.h"
#include "board_sd.h"
#include "game_live.h"
#include "hw_profile.h"
#include "karaoke.h"
#include "ui_screen_power.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_diag.h"
#include "my_rtc.h"
#include "my_sys.h"
#include "my_time.h"
#include "my_wifi.h"

namespace PowerHistory {
namespace {
constexpr unsigned FILE_COUNT = 16;
constexpr size_t MAX_BYTES = 1024 * 1024;
constexpr const char *HEADER =
    "schema,board,firmware,boot,reset,epoch,time_source,certain,uptime_ms,interval_ms,"
    "battery_mv_min,battery_mv_avg,battery_mv_max,battery_samples,battery_scale,"
    "screen_ms,brightness_avg,wifi_ms,wifi_rssi,audio_ms,sfx_volume_avg,music_volume_avg,eco_ms,"
    "yatzy_ms,pig_ms,farkle_ms,scc_ms,adventure_ms,karaoke_ms,idle_other_ms,"
    "sd_read_bytes,sd_write_bytes,ffat_read_bytes,ffat_write_bytes,"
    "internal_free_min,internal_largest_min,sd_errors,audio_underruns,rtc_temp_c,dropped\n";
Status s_status{};
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void publish(bool ok, const char *path)
{
    portENTER_CRITICAL(&s_mux);
    if (ok) ++s_status.written;
    else ++s_status.dropped;
    snprintf(s_status.path, sizeof(s_status.path), "%s", path);
    portEXIT_CRITICAL(&s_mux);
}

bool append(const char *row, unsigned &slot, bool &fresh)
{
    char path[64];
    snprintf(path, sizeof(path), "/sdcard/otr/power/power-%02u.csv", slot);
    bool ok = false;
    {
        BoardSd::Guard lock;
        if (BoardSd::transferHealthy() && BoardSd::ensureDir("/sdcard/otr/power")) {
            struct stat st{};
            if (!fresh && stat(path, &st) != 0) {
                if (errno == ENOENT) fresh = true;
                else {
                    ESP_LOGE("power", "cannot stat %s: errno=%d", path, errno);
                    publish(false, path);
                    return false;
                }
            }
            if (!fresh && st.st_size + strlen(row) >= MAX_BYTES) {
                slot = (slot + 1) % FILE_COUNT;
                snprintf(path, sizeof(path), "/sdcard/otr/power/power-%02u.csv", slot);
                fresh = true;
            }
            FILE *f = fopen(path, fresh ? "w" : "a");
            if (f) {
                ok = (!fresh || fputs(HEADER, f) >= 0) && fputs(row, f) >= 0;
                if (fclose(f) != 0) ok = false;
                if (ok) fresh = false;
            }
        }
    }
    publish(ok, path);
    if (!ok) ESP_LOGW("power", "history row dropped: SD unavailable or write failed (%s)", path);
    return ok;
}

void task(void *)
{
    PowerInterval::Totals totals;
    PowerIo::Counts previousIo = PowerIo::counts();
    uint32_t previousErrors = BoardSd::info().errors, previousUnderruns = Audio::status().underruns;
    uint32_t last = MySys::millis();
    size_t freeMin = SIZE_MAX, largestMin = SIZE_MAX;
    unsigned slot = MyDiag::bootCount() % FILE_COUNT;
    bool enabled = false, fresh = true;
    PowerInterval::Sample previous{};
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint32_t now = MySys::millis(), dt = now - last;
        last = now;
        const AppConfig cfg = Config::get();
        const bool active = cfg.powerLog;
        if (!enabled && !active) continue;
        if (enabled) totals.add(previous, dt);

        if ((totals.elapsed >= PowerInterval::PERIOD_MS || !active) && totals.elapsed) {
            const auto io = PowerIo::counts();
            const auto sd = BoardSd::info();
            const auto audio = Audio::status();
            const auto wifi = MyWifi::info();
            char batteryMin[12] = "", batteryAvg[12] = "", batteryMax[12] = "", temp[16] = "";
            if (totals.batterySamples) {
                snprintf(batteryMin, sizeof(batteryMin), "%u", totals.batteryMin);
                snprintf(batteryAvg, sizeof(batteryAvg), "%llu",
                         (unsigned long long)(totals.batterySum / totals.batterySamples));
                snprintf(batteryMax, sizeof(batteryMax), "%u", totals.batteryMax);
            }
            float c;
            if (MyRtc::temperatureAvailable() && MyRtc::readTemp(c))
                snprintf(temp, sizeof(temp), "%.2f", (double)c);
            char row[768];
            const int n = snprintf(row, sizeof(row),
                "1,%s,%s,%lu,%d,%lld,%u,%u,%lu,%lu,%s,%s,%s,%lu,%u,"
                "%lu,%llu,%lu,%d,%lu,%llu,%llu,%lu,"
                "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%s,%lu\n",
                HwProfile::active().id == HwProfile::Id::P4_43 ? "p4-43" : "es3c35p", FW_VERSION,
                (unsigned long)MyDiag::bootCount(), (int)esp_reset_reason(), (long long)MyTime::now(),
                (unsigned)MyTime::source(), MyTime::certain() ? 1u : 0u,
                (unsigned long)now, (unsigned long)totals.elapsed,
                batteryMin, batteryAvg, batteryMax, (unsigned long)totals.batterySamples,
                cfg.batteryScalePermille, (unsigned long)totals.screen,
                (unsigned long long)(totals.brightness / totals.elapsed), (unsigned long)totals.wifi,
                wifi.connected ? wifi.rssi : 0, (unsigned long)totals.audio,
                (unsigned long long)(totals.sfxVolume / totals.elapsed),
                (unsigned long long)(totals.musicVolume / totals.elapsed), (unsigned long)totals.eco,
                (unsigned long)totals.games[0], (unsigned long)totals.games[1],
                (unsigned long)totals.games[2], (unsigned long)totals.games[3],
                (unsigned long)totals.games[4], (unsigned long)totals.games[5], (unsigned long)totals.games[6],
                (unsigned long)(io.sdRead - previousIo.sdRead), (unsigned long)(io.sdWrite - previousIo.sdWrite),
                (unsigned long)(io.flashRead - previousIo.flashRead),
                (unsigned long)(io.flashWrite - previousIo.flashWrite),
                (unsigned long)freeMin, (unsigned long)largestMin,
                (unsigned long)(sd.errors - previousErrors),
                (unsigned long)(audio.underruns - previousUnderruns), temp, (unsigned long)status().dropped);
            if (n > 0 && n < (int)sizeof(row)) append(row, slot, fresh);
            else {
                publish(false, "");
                ESP_LOGE("power", "history row exceeds buffer");
            }
            totals = {};
            previousIo = io;
            previousErrors = sd.errors;
            previousUnderruns = audio.underruns;
            freeMin = largestMin = SIZE_MAX;
        }
        if (active && !enabled) {
            previousIo = PowerIo::counts();
            previousErrors = BoardSd::info().errors;
            previousUnderruns = Audio::status().underruns;
            ESP_LOGI("power", "five-minute SD history enabled");
        }
        if (!active) ESP_LOGI("power", "SD history disabled");
        enabled = active;
        if (!active) continue;

        GameLive::Snapshot game{};
        GameLive::get(game);
        unsigned activity = PowerInterval::GAME_COUNT - 1;
        if (Karaoke::playing()) activity = 5;
        else if (AdvLive::active()) activity = 4;
        else if (game.active && GameLive::started(game) && !GameLive::finished(game))
            activity = static_cast<unsigned>(game.gameType);
        const auto audio = Audio::status();
        const auto battery = BoardBattery::status();
        uint16_t mv = 0;
        if (battery.supported && battery.sampleValid && battery.adcCalibrated)
            BatteryMath::batteryMillivolts(battery.adcMillivolts, cfg.batteryScalePermille, mv);
        previous = {activity, !Backlight::sleeping(), MyWifi::connected(), audio.ampOn,
                    UiScreenPower::ecoSleeping(), Backlight::get(), cfg.sfxVol, cfg.musicVol, mv};
        const size_t free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (free < freeMin) freeMin = free;
        if (largest < largestMin) largestMin = largest;
    }
}
}

bool begin()
{
    if (s_status.running) return true;
    if (xTaskCreate(task, "power_history", 6144, nullptr, 1, nullptr) != pdPASS) {
        ESP_LOGE("power", "cannot start history task");
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    s_status.running = true;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

Status status()
{
    portENTER_CRITICAL(&s_mux);
    const Status s = s_status;
    portEXIT_CRITICAL(&s_mux);
    return s;
}
}
