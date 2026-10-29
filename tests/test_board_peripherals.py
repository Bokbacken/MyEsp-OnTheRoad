#!/usr/bin/env python3
"""Compile and exercise the production RTC/ADC/SD drivers with deterministic IDF doubles."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMON = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <sys/time.h>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_TIMEOUT=0x107;
inline const char *esp_err_to_name(int e) { return e ? "test error" : "ESP_OK"; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define RTC_NOINIT_ATTR
#define pdMS_TO_TICKS(v) (v)
constexpr int pdPASS=1, portMAX_DELAY=-1;
inline uint64_t ticks=1000000;
inline bool stopSample=false;
inline void vTaskDelay(int ms) {
    ticks += uint64_t(ms)*1000;
    if (stopSample && ms==10000) throw std::runtime_error("sample complete");
}
using i2c_master_bus_handle_t = void *;
enum esp_reset_reason_t { ESP_RST_POWERON, ESP_RST_SW, ESP_RST_DEEPSLEEP };
inline esp_reset_reason_t resetReason=ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return resetReason; }
inline uint64_t esp_rtc_get_time_us() { return ticks; }
inline int setSystemTime(const timeval *, const void *) { return 0; }
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
using SemaphoreHandle_t=void *;
inline void *xSemaphoreCreateRecursiveMutex() { return reinterpret_cast<void *>(1); }
inline void xSemaphoreTakeRecursive(void *, int) {}
inline void xSemaphoreGiveRecursive(void *) {}

using adc_oneshot_unit_handle_t=void *;
using adc_cali_handle_t=void *;
using adc_channel_t=int;
using adc_unit_t=int;
constexpr int ADC_ATTEN_DB_12=12, ADC_BITWIDTH_12=12;
struct adc_oneshot_unit_init_cfg_t { int unit_id; };
struct adc_oneshot_chan_cfg_t { int atten, bitwidth; };
struct adc_cali_curve_fitting_config_t { int unit_id, chan, atten, bitwidth; };
inline bool failMap=false, failCali=false, failTask=false, failRead=false;
inline int liveAdc=0, liveCali=0, mappedPin=-1, configuredChannel=-1;
inline int expectedChannel() {
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    return 4;
#else
    return 7;
#endif
}
inline int adc_oneshot_io_to_channel(int pin, int *unit, int *chan) {
    mappedPin=pin; *unit=0; *chan=expectedChannel(); return failMap ? ESP_FAIL : ESP_OK;
}
inline int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *c, void **p) {
    assert(c->unit_id==0); ++liveAdc; *p=reinterpret_cast<void *>(1); return ESP_OK;
}
inline int adc_oneshot_del_unit(void *) { --liveAdc; return ESP_OK; }
inline int adc_oneshot_config_channel(void *, int chan, const adc_oneshot_chan_cfg_t *c) {
    configuredChannel=chan; assert(c->bitwidth==12 && c->atten==12); return ESP_OK;
}
inline int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *c, void **p) {
    assert(c->chan==expectedChannel() && c->bitwidth==12);
    if (failCali) return ESP_FAIL;
    ++liveCali; *p=reinterpret_cast<void *>(2); return ESP_OK;
}
inline int adc_cali_delete_scheme_curve_fitting(void *) { --liveCali; return ESP_OK; }
inline int adc_oneshot_read(void *, int chan, int *raw) {
    assert(chan==expectedChannel()); *raw=1600; return failRead ? ESP_FAIL : ESP_OK;
}
inline int adc_cali_raw_to_voltage(void *, int, int *mv) { *mv=1300; return ESP_OK; }
inline void (*sampleFn)(void *)=nullptr;
inline int xTaskCreatePinnedToCore(void (*fn)(void *), const char *, int, void *, int, void *, int) {
    sampleFn=fn; return failTask ? 0 : pdPASS;
}

using gpio_num_t=int;
constexpr int GPIO_NUM_NC=-1, GPIO_MODE_OUTPUT=1;
inline int powerLevel=-1, powerCycles=0;
inline int gpio_set_level(int pin, int level) {
    assert(pin==45); if (level) ++powerCycles; powerLevel=level; return ESP_OK;
}
inline int gpio_set_direction(int pin, int mode) {
    assert(pin==45 && mode==GPIO_MODE_OUTPUT); return ESP_OK;
}
using sd_pwr_ctrl_handle_t=void *;
struct sd_pwr_ctrl_ldo_config_t { int ldo_chan_id; };
inline int sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *c, void **p) {
    assert(c->ldo_chan_id==4); *p=reinterpret_cast<void *>(3); return ESP_OK;
}
constexpr int SDMMC_FREQ_DEFAULT=20000, SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1;
inline bool radioAlive=true;
inline int hostInit() { return ESP_OK; }
inline int hostDeinit(int slot) { assert(slot==0); assert(radioAlive); return ESP_OK; }
struct sdmmc_host_t {
    int slot, max_freq_khz;
    void *pwr_ctrl_handle;
    int (*init)();
    int (*deinit_p)(int);
};
#define SDMMC_HOST_DEFAULT() {1,20000,nullptr,hostInit,hostDeinit}
struct sdmmc_slot_config_t { int width,clk,cmd,d0,d1,d2,d3,flags; };
#define SDMMC_SLOT_CONFIG_DEFAULT() {0,-1,-1,-1,-1,-1,-1,0}
struct esp_vfs_fat_sdmmc_mount_config_t {
    bool format_if_mount_failed; int max_files; int allocation_unit_size;
};
struct sdmmc_card_t {
    int real_freq_khz;
    struct { char name[5]; } cid;
};
inline sdmmc_card_t fakeCard{20000, {'T','E','S','T','0'}};
inline int mountCalls=0, mountFailures=0, statusResult=ESP_OK, unmountResult=ESP_OK;
inline int lastWidth=0, lastFreq=0;
inline int esp_vfs_fat_sdmmc_mount(const char *, const sdmmc_host_t *h, const sdmmc_slot_config_t *s,
                                 const esp_vfs_fat_sdmmc_mount_config_t *c, sdmmc_card_t **out) {
    assert(!c->format_if_mount_failed);
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(h->slot==0 && s->clk==43 && s->cmd==44 && s->d0==39 && powerLevel==0);
    assert(h->pwr_ctrl_handle && radioAlive);
#else
    assert(h->slot==1 && s->clk==5 && s->cmd==4 && s->d0==6);
#endif
    ++mountCalls; lastWidth=s->width; lastFreq=h->max_freq_khz;
    if (mountFailures) { --mountFailures; return ESP_ERR_TIMEOUT; }
    fakeCard.real_freq_khz=h->max_freq_khz; *out=&fakeCard; return ESP_OK;
}
inline int esp_vfs_fat_info(const char *, uint64_t *total, uint64_t *free) {
    *total=32000000; *free=16000000; return ESP_OK;
}
inline int esp_vfs_fat_sdcard_unmount(const char *, sdmmc_card_t *) {
    assert(radioAlive); return unmountResult;
}
inline int sdmmc_get_status(sdmmc_card_t *) { return statusResult; }
using BYTE=unsigned char;
using DWORD=unsigned long;
struct FATFS { unsigned csize; };
#define FF_MAX_SS 512
#define FF_MIN_SS 512
constexpr int FR_OK=0;
inline BYTE ff_diskio_get_pdrv_card(sdmmc_card_t *) { return 0; }
inline int f_getfree(const char *, DWORD *n, FATFS **fs) {
    static FATFS f{16}; *fs=&f; *n=100; return FR_OK;
}
'''

RTC = r'''
#define settimeofday setSystemTime
#include "components/my_rtc/my_rtc_internal.cpp"
#undef settimeofday
int main() {
    using namespace MyRtc;
    time_t out=0;
    assert(!begin(nullptr, &out));
    assert(status().present && status().ticking && !status().timeValid);
    assert(!temperatureAvailable() && std::strcmp(chipName(),"ESP32-P4 RTC")==0);
    assert(!write(100));
    assert(write(1793282400));
    ticks+=7000000;
    assert(read(out) && out==1793282407);
    resetReason=ESP_RST_SW;
    assert(begin(nullptr,&out) && out==1793282407);
    resetReason=ESP_RST_DEEPSLEEP;
    ticks+=10000000;
    assert(begin(nullptr,&out) && out==1793282417);
    resetReason=ESP_RST_POWERON;
    assert(!begin(nullptr,&out)); // Never trust a stale anchor after abrupt power loss.
    assert(write(1793282400));
    ticks=0;
    resetReason=ESP_RST_SW;
    assert(!begin(nullptr,&out)); // Counter restarted: old anchor cannot be extrapolated.
    assert(write(4102444799LL));
    ticks+=2000000;
    assert(!read(out)); // Supported epoch upper bound.
}
'''

ADC = r'''
#include "main/board_battery.cpp"
int main(int argc, char **argv) {
    const char *mode=argc>1 ? argv[1] : "";
    failTask=std::strcmp(mode,"taskfail")==0;
    failMap=std::strcmp(mode,"mapfail")==0;
    failCali=std::strcmp(mode,"califail")==0;
    failRead=std::strcmp(mode,"readfail")==0;
    using namespace BoardBattery;
    if (failTask || failMap) {
        assert(!begin() && status().supported && !status().initialized && status().errors);
        assert(liveAdc==0 && liveCali==0);
        failTask=failMap=false;
    }
    assert(begin() && status().initialized);
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(mappedPin==20);
#else
    assert(mappedPin==8);
#endif
    assert(configuredChannel==expectedChannel());
    stopSample=true;
    try { sampleFn(nullptr); } catch (const std::runtime_error &) {}
    auto st=status();
    if (failRead) assert(!st.sampleValid && st.errors);
    else {
        assert(st.sampleValid && st.raw==1600);
        assert(st.adcCalibrated==!failCali);
        if (!failCali) assert(st.adcMillivolts==1300);
    }
}
'''

SD = r'''
#include "main/board_sd.cpp"
int main() {
    using namespace BoardSd;
    assert(begin() && info().mounted && info().width==4);
    assert(info().generation==1 && info().totalBytes==32000000);
    unmountResult=ESP_FAIL;
    assert(!remount() && mounted()); // Do not discard ownership or reinit after failed unmount.
    unmountResult=ESP_OK;
    statusResult=ESP_ERR_TIMEOUT;
    poll(100);
    assert(!mounted() && info().lastError==ESP_ERR_TIMEOUT && info().errors);
    statusResult=ESP_OK;
    poll(4000);
    assert(mounted() && info().generation==2 && radioAlive);
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(powerCycles>=1 && lastFreq==10000); // Runtime failure gets a real card VDD reset.
#else
    assert(lastFreq==20000 && powerCycles==0);
#endif
    mountFailures=1;
    assert(remount() && lastWidth==1 && radioAlive);
    assert(info().lastError==ESP_OK);
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="otr-peripheral-test-") as directory:
        work = pathlib.Path(directory)
        (work / "test_idf.h").write_text(COMMON)
        headers = [
            "sdkconfig.h", "driver/i2c_master.h", "esp_attr.h", "esp_log.h",
            "esp_rtc_time.h", "esp_system.h", "freertos/FreeRTOS.h", "freertos/task.h",
            "freertos/portmacro.h", "freertos/semphr.h", "esp_adc/adc_cali.h",
            "esp_adc/adc_cali_scheme.h", "esp_adc/adc_oneshot.h", "driver/gpio.h",
            "driver/sdmmc_host.h", "esp_vfs_fat.h", "ff.h", "diskio_sdmmc.h",
            "sdmmc_cmd.h", "sd_pwr_ctrl_by_on_chip_ldo.h",
        ]
        for header in headers:
            path = work / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "test_idf.h"\n')
        for name, code, targets in [("rtc", RTC, ["p4"]), ("adc", ADC, ["s3", "p4"]), ("sd", SD, ["s3", "p4"])]:
            source = work / f"{name}.cpp"
            source.write_text(code)
            for target in targets:
                binary = work / f"{name}-{target}"
                flags = ["-DCONFIG_IDF_TARGET_ESP32P4", "-DCONFIG_OTR_HW_PROFILE_P4_43"] if target == "p4" else ["-DCONFIG_IDF_TARGET_ESP32S3"]
                subprocess.run([
                    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                    *flags, "-include", str(work / "test_idf.h"),
                    f"-I{work}", f"-I{ROOT}", f"-I{ROOT / 'main'}",
                    f"-I{ROOT / 'components/my_rtc/include'}", str(source), "-o", str(binary),
                ], check=True)
                modes = ["ok", "taskfail", "mapfail", "califail", "readfail"] if name == "adc" else ["ok"]
                for mode in modes:
                    subprocess.run([str(binary), mode], check=True)
                print(f"{name}/{target}: passed")
        for target in ["s3", "p4"]:
            binary = work / f"config-{target}"
            flags = ["-DCONFIG_OTR_HW_PROFILE_P4_43"] if target == "p4" else []
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                f"-I{ROOT / 'tests/battery_stubs'}", f"-I{ROOT / 'main'}",
                f"-I{ROOT / 'components/my_wifi/include'}", f"-I{ROOT / 'main/lang'}",
                *[str(ROOT / p) for p in [
                    "tests/test_battery_config.cpp", "main/app_config.cpp",
                    "main/lang/lang.cpp", "main/lang/lang_sv.cpp", "main/lang/lang_en.cpp",
                ]], "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
