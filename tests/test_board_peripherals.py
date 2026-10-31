#!/usr/bin/env python3
"""Compile and exercise the production RTC/ADC/SD drivers with deterministic IDF doubles."""
import pathlib
import subprocess
import tempfile
import csv

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
inline int delayCalls=0;
inline void vTaskDelay(int ms) {
    ++delayCalls;
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
using TaskHandle_t=void *;
inline int64_t esp_timer_get_time() { return ticks; }
inline int xPortGetCoreID() { return 0; }

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
constexpr int SDMMC_FREQ_DEFAULT=20000, SDMMC_FREQ_HIGHSPEED=40000, SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1;
inline bool radioAlive=true;
inline int hostInit() { return ESP_OK; }
inline int hostDeinit(int slot) { assert(slot==0); assert(radioAlive); return ESP_OK; }
struct sdmmc_command_t {
    int opcode;
    uint32_t arg;
    void *data;
    size_t datalen;
    esp_err_t error;
    uint32_t response[4];
    int flags;
    int timeout_ms;
};
constexpr int MMC_STOP_TRANSMISSION=12, MMC_SEND_STATUS=13, MMC_WRITE_BLOCK_MULTIPLE=25, SD_APP_SEND_NUM_WR_BLOCKS=22;
constexpr uint32_t MMC_R1_READY_FOR_DATA=1u<<8, MMC_R1_CURRENT_STATE_TRAN=4;
#define MMC_R1_CURRENT_STATE_STATUS(s) (((s) & 0x1E00) >> 9)
constexpr int SCF_CMD_AC=1, SCF_RSP_R1B=2, SCF_WAIT_BUSY=4;
inline int transactionResult=ESP_OK, commandError=ESP_OK, stopError=ESP_OK;
inline int transactionCalls=0, stopCalls=0;
inline uint32_t cardStatus=0x900;   // R1 returned by CMD13
inline int fakeTransaction(int slot, sdmmc_command_t *cmd) {
    assert(slot==0 || slot==1);
    ++transactionCalls;
    ticks+=2500;
    cmd->error=commandError;
    if (cmd->opcode==MMC_STOP_TRANSMISSION) {
        assert(cmd->flags==(SCF_CMD_AC|SCF_RSP_R1B|SCF_WAIT_BUSY) && cmd->timeout_ms>0);
        ++stopCalls;
        cmd->error=stopError;
        if (!stopError) cardStatus=0x900;
    }
    cmd->response[0]=cmd->opcode==MMC_SEND_STATUS ? cardStatus : 0x900;
    return transactionResult;
}
struct sdmmc_host_t {
    int slot, max_freq_khz;
    void *pwr_ctrl_handle;
    int (*init)();
    int (*deinit_p)(int);
    int (*do_transaction)(int, sdmmc_command_t *);
};
#define SDMMC_HOST_DEFAULT() {1,20000,nullptr,hostInit,hostDeinit,fakeTransaction}
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
inline int mountCalls=0, mountFailures=0, statusResult=ESP_OK, unmountResult=ESP_OK, formatResult=ESP_OK;
inline int formatMountCalls=0;
inline bool acceptFormatMount=false;
inline int formatCalls=0;
inline int lastWidth=0, lastFreq=0;
inline int (*mountedTransaction)(int, sdmmc_command_t *)=nullptr;
inline int esp_vfs_fat_sdmmc_mount(const char *, const sdmmc_host_t *h, const sdmmc_slot_config_t *s,
                                 const esp_vfs_fat_sdmmc_mount_config_t *c, sdmmc_card_t **out) {
    if (c->format_if_mount_failed) {
        ++formatMountCalls;
        if (!acceptFormatMount) return ESP_FAIL;
    }
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(h->slot==0 && s->clk==43 && s->cmd==44 && s->d0==39 && powerLevel==0);
    assert(h->pwr_ctrl_handle && radioAlive);
#else
    assert(h->slot==1 && s->clk==5 && s->cmd==4 && s->d0==6);
#endif
    ++mountCalls; lastWidth=s->width; lastFreq=h->max_freq_khz;
    mountedTransaction=h->do_transaction;
    if (mountFailures) { --mountFailures; return ESP_ERR_TIMEOUT; }
    fakeCard.real_freq_khz=h->max_freq_khz; *out=&fakeCard; return ESP_OK;
}
inline int esp_vfs_fat_info(const char *, uint64_t *total, uint64_t *free) {
    *total=32000000; *free=16000000; return ESP_OK;
}
inline int esp_vfs_fat_sdcard_unmount(const char *, sdmmc_card_t *) {
    assert(radioAlive); return unmountResult;
}
inline int esp_vfs_fat_sdcard_format(const char *, sdmmc_card_t *card) {
    assert(card==&fakeCard); ++formatCalls; return formatResult;
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
    assert(!traceEnabled() && traceRemainingSeconds()==0);
    setTrace(true);
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
#ifdef CONFIG_OTR_SD_50MHZ_EXPERIMENT
    assert(lastFreq==50000 && powerCycles==0);
#else
    assert(lastFreq==40000 && powerCycles==0); // Same clock as ESP-Hosted on the shared controller.
#endif
#else
    assert(lastFreq==20000);
#endif
    sdmmc_command_t cmd{25, 123, reinterpret_cast<void *>(1), 32768, ESP_OK, {}, 0, 0};
    assert(mountedTransaction(0, &cmd)==ESP_OK && transactionCalls==1);
#ifdef CONFIG_OTR_SD_IO_DIAGNOSTICS
    assert(s_trace.commands==1 && !s_trace.active && s_trace.dataBlock==123);
    assert(s_trace.dataOpcode==25 && s_trace.dataBytes==32768 && s_trace.durationUs==2500);
    cmd.opcode=13; cmd.data=nullptr;
    for (int i=0;i<1000;++i) assert(mountedTransaction(0,&cmd)==ESP_OK);
    assert(s_trace.statusPolls==1000 && s_trace.dataOpcode==25 && s_trace.dataBlock==123);
    commandError=ESP_ERR_TIMEOUT;
    assert(mountedTransaction(0,&cmd)==ESP_OK && s_trace.error==ESP_ERR_TIMEOUT);
    commandError=ESP_OK; transactionResult=ESP_FAIL;
    assert(mountedTransaction(0,&cmd)==ESP_FAIL && s_trace.error==ESP_FAIL);
    transactionResult=ESP_OK;
    cmd.opcode=24; cmd.data=reinterpret_cast<void *>(1);
    assert(mountedTransaction(0,&cmd)==ESP_OK && s_trace.statusPolls==0);
#endif
    assert(mountedTransaction==guardedTransaction);
    // Lost CMD12: card stuck in RCV gets one STOP_TRANSMISSION, then normal status again.
    cmd={13, 0, nullptr, 0, ESP_OK, {}, 0, 0};
    cardStatus=0x0c00;
    stopCalls=0;
    assert(mountedTransaction(0,&cmd)==ESP_OK && stopCalls==1);
    assert(mountedTransaction(0,&cmd)==ESP_OK && cmd.response[0]==0x900);
    // Failed CMD12 is rate limited instead of being sent on every CMD13 poll.
    cardStatus=0x0c00; stopError=ESP_ERR_TIMEOUT; stopCalls=0;
    ticks+=1000000;
    for (int i=0;i<50;++i) assert(mountedTransaction(0,&cmd)==ESP_OK);
    assert(stopCalls==1);
    ticks+=600000;
    assert(mountedTransaction(0,&cmd)==ESP_OK && stopCalls==2);
    stopError=ESP_OK; cardStatus=0x900;
    assert(mountedTransaction(0,&cmd)==ESP_OK);
    // Programming card: short polls spin, longer waits yield so IDLE0 can feed the watchdog.
    cardStatus=0x0e00; delayCalls=0;
    assert(mountedTransaction(0,&cmd)==ESP_OK && delayCalls==0);
    for (int i=0;i<3;++i) assert(mountedTransaction(0,&cmd)==ESP_OK);
    assert(delayCalls>=1);
    cardStatus=0x900; delayCalls=0;
    assert(mountedTransaction(0,&cmd)==ESP_OK && delayCalls==0);
    // ESP-IDF v6.1 returns ACMD22's result for a failed CMD25: keep the write failed.
    sdmmc_command_t write{25, 7, reinterpret_cast<void *>(1), 16384, ESP_OK, {}, 0, 0};
    sdmmc_command_t written{22, 0, reinterpret_cast<void *>(1), 4, ESP_OK, {}, 0, 0};
    commandError=ESP_ERR_TIMEOUT;
    assert(mountedTransaction(0,&write)==ESP_OK && write.error==ESP_ERR_TIMEOUT);
    commandError=ESP_OK;
    assert(mountedTransaction(0,&written)==ESP_OK && written.error==ESP_ERR_TIMEOUT);
    written.error=ESP_OK;
    assert(mountedTransaction(0,&written)==ESP_OK && written.error==ESP_OK);
    assert(mountedTransaction(0,&write)==ESP_OK && mountedTransaction(0,&written)==ESP_OK && written.error==ESP_OK);
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
#ifdef CONFIG_OTR_SD_50MHZ_EXPERIMENT
    assert(powerCycles>=1 && lastFreq==50000);
#else
    assert(powerCycles>=1 && lastFreq==40000); // VDD reset, same clock as the shared C6 slot.
#endif
#else
    assert(lastFreq==20000 && powerCycles==0);
#endif
    mountFailures=1;
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(remount() && lastWidth==4 && lastFreq==20000 && radioAlive); // Fallback if 40 MHz will not mount.
    mountFailures=2;
    assert(remount() && lastWidth==1 && lastFreq==20000 && radioAlive);
#else
    assert(remount() && lastWidth==1 && radioAlive);
#endif
    assert(info().lastError==ESP_OK);
    const uint32_t generation=info().generation;
    assert(format() && formatCalls==1 && info().generation==generation+1);
    formatResult=ESP_FAIL;
    assert(!format() && formatCalls==2 && mounted() && info().lastError==ESP_FAIL);
    statusResult=ESP_ERR_TIMEOUT;
    poll(20000);
    assert(!mounted());
    statusResult=ESP_OK;
    formatResult=ESP_OK;
    acceptFormatMount=true;
    assert(format() && formatMountCalls==1 && formatCalls==3 && mounted());
    // A failed session blocks new work, aborts its peers and waits for every open file to close.
    const uint32_t beforeRecovery=info().generation;
    Transfer first, second;
    assert(first.active && second.active);
    assert(!remount() && !format());
    requestRecovery();
    assert(!transferHealthy());
    Transfer rejected;
    assert(!rejected.active);
    poll(40000);
    assert(mounted() && info().generation==beforeRecovery);
    first.finish();
    assert(info().generation==beforeRecovery);
    second.finish();
    assert(transferHealthy() && info().generation==beforeRecovery+1);
    second.finish(); // Idempotent: no extra remount.
    assert(info().generation==beforeRecovery+1);
    setTrace(false);
    assert(!traceEnabled());
    setTrace(true);
    ticks+=601000000;
    assert(!traceEnabled() && traceRemainingSeconds()==0);
}
'''

PROFILE = r'''
#include <cassert>
#include "hw_profile.h"
int main() {
    const auto &p = HwProfile::active();
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(p.id == HwProfile::Id::P4_43);
    assert(p.flashBytes == 32u * 1024 * 1024 && p.psramBytes == 32u * 1024 * 1024);
    assert(p.soundFontMaxBytes == p.psramBytes && p.psramReserveBytes == 2u * 1024 * 1024);
    assert(!p.resampleSoundFonts);
    assert(p.maxKaraokeVoices == 192);
#else
    assert(p.id == HwProfile::Id::ES3C35P);
    assert(p.flashBytes == 16u * 1024 * 1024 && p.psramBytes == 8u * 1024 * 1024);
    assert(p.soundFontMaxBytes == 7u * 1024 * 1024 && p.psramReserveBytes == 512u * 1024);
    assert(p.resampleSoundFonts);
    assert(p.maxKaraokeVoices == 96);
#endif
}
'''


def partitions(path):
    result = {}
    with path.open(newline="") as file:
        for row in csv.reader(file):
            if not row or row[0].lstrip().startswith("#"):
                continue
            name, _type, _subtype, offset, size, *_ = [value.strip() for value in row]
            result[name] = (int(offset, 0), int(size, 0))
    return result


def main():
    s3 = partitions(ROOT / "partitions.csv")
    p4 = partitions(ROOT / "partitions.esp32p4.csv")
    for name in ("app0", "app1"):
        assert p4[name] == s3[name], f"{name} OTA slot changed on P4"
    assert p4["ffat"][0] == s3["ffat"][0] and p4["ffat"][1] == 0x15E0000
    assert p4["coredump"][0] + p4["coredump"][1] == 32 * 1024 * 1024
    for table, flash in ((s3, 16 * 1024 * 1024), (p4, 32 * 1024 * 1024)):
        ranges = sorted((offset, offset + size, name) for name, (offset, size) in table.items())
        assert all(end <= flash for _, end, _ in ranges)
        assert all(ranges[i][1] <= ranges[i + 1][0] for i in range(len(ranges) - 1))

    with tempfile.TemporaryDirectory(prefix="otr-peripheral-test-") as directory:
        work = pathlib.Path(directory)
        (work / "test_idf.h").write_text(COMMON)
        headers = [
            "sdkconfig.h", "driver/i2c_master.h", "esp_attr.h", "esp_log.h",
            "esp_rtc_time.h", "esp_system.h", "freertos/FreeRTOS.h", "freertos/task.h",
            "freertos/portmacro.h", "freertos/semphr.h", "esp_adc/adc_cali.h",
            "esp_adc/adc_cali_scheme.h", "esp_adc/adc_oneshot.h", "driver/gpio.h",
            "driver/sdmmc_host.h", "esp_vfs_fat.h", "ff.h", "diskio_sdmmc.h",
            "sdmmc_cmd.h", "sd_protocol_defs.h", "sd_pwr_ctrl_by_on_chip_ldo.h", "esp_timer.h",
        ]
        for header in headers:
            path = work / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "test_idf.h"\n')
        for name, code, targets in [("rtc", RTC, ["p4"]), ("adc", ADC, ["s3", "p4"]), ("sd", SD, ["s3", "p4", "p4-50"])]:
            source = work / f"{name}.cpp"
            source.write_text(code)
            for target in targets:
                binary = work / f"{name}-{target}"
                flags = ["-DCONFIG_IDF_TARGET_ESP32P4", "-DCONFIG_OTR_HW_PROFILE_P4_43"] if target.startswith("p4") else ["-DCONFIG_IDF_TARGET_ESP32S3"]
                if name == "sd" and target.startswith("p4"):
                    flags += ["-DCONFIG_OTR_SD_IO_DIAGNOSTICS"]
                    clock = 50000 if target == "p4-50" else 40000
                    flags += [f"-DCONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ={clock}"]
                    if target == "p4-50":
                        flags += ["-DCONFIG_OTR_SD_50MHZ_EXPERIMENT"]
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
            profile = work / f"profile-{target}"
            flags = ["-DCONFIG_OTR_HW_PROFILE_P4_43"] if target == "p4" else []
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                f"-I{work}", f"-I{ROOT / 'main'}", str(ROOT / "main/hw_profile.cpp"),
                "-x", "c++", "-o", str(profile), "-",
            ], input=PROFILE, text=True, check=True)
            subprocess.run([str(profile)], check=True)


if __name__ == "__main__":
    main()
