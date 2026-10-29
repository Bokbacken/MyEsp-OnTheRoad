// board_sd.cpp - see board_sd.h.
#include "board_sd.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_sdmmc.h"   // After ff.h (needs BYTE)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "sd";

namespace BoardSd {

namespace {

SemaphoreHandle_t s_mux = nullptr;
sdmmc_card_t *s_card = nullptr;
Info s_info{};
uint32_t s_nextPoll = 0;
bool s_logMissing = true;
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
sd_pwr_ctrl_handle_t s_pwr = nullptr;   // Card IO power from on-chip LDO, kept for the whole runtime
bool s_recover = false;

bool cardPower(bool reset)
{
    // Q1 switches card VDD; LDO4 powers the SD IO bank, not the card itself.
    esp_err_t err = gpio_set_level((gpio_num_t)PIN_SD_POWER, reset ? 1 : 0);
    if (err == ESP_OK) err = gpio_set_direction((gpio_num_t)PIN_SD_POWER, GPIO_MODE_OUTPUT);
    if (err == ESP_OK && reset) {
        vTaskDelay(pdMS_TO_TICKS(200));
        err = gpio_set_level((gpio_num_t)PIN_SD_POWER, 0);
    }
    if (err != ESP_OK) {
        s_info.lastError = err;
        ++s_info.errors;
        ESP_LOGE(TAG, "card VDD GPIO%d failed: %s", PIN_SD_POWER, esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}
#endif

bool tryMount(int width, int freqKhz = SDMMC_FREQ_DEFAULT)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = freqKhz;
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    // Slot 1 belongs to ESP-Hosted (ESP32-C6); deinit_p only releases our slot on unmount.
    host.slot = SD_SLOT;
    if (!s_pwr) {
        sd_pwr_ctrl_ldo_config_t ldo = {};
        ldo.ldo_chan_id = SD_PWR_LDO_CHAN;
        const esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s_pwr);
        if (err != ESP_OK) {
            s_pwr = nullptr;
            s_info.lastError = err;
            ++s_info.errors;
            if (s_logMissing) ESP_LOGW(TAG, "SD IO LDO %d failed: %s", SD_PWR_LDO_CHAN, esp_err_to_name(err));
            return false;
        }
    }
    host.pwr_ctrl_handle = s_pwr;
#endif
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = (uint8_t)width;
    slot.clk = (gpio_num_t)PIN_SD_CLK;
    slot.cmd = (gpio_num_t)PIN_SD_CMD;
    slot.d0 = (gpio_num_t)PIN_SD_D0;
    slot.d1 = width == 4 ? (gpio_num_t)PIN_SD_D1 : GPIO_NUM_NC;
    slot.d2 = width == 4 ? (gpio_num_t)PIN_SD_D2 : GPIO_NUM_NC;
    slot.d3 = width == 4 ? (gpio_num_t)PIN_SD_D3 : GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
    mcfg.format_if_mount_failed = false;   // Never format the user's card
    mcfg.max_files = 6;
    mcfg.allocation_unit_size = 16 * 1024;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        s_card = nullptr;
        s_info.lastError = err;
        ++s_info.errors;
        if (s_logMissing) ESP_LOGW(TAG, "mount slot %d, %d-bit, %d kHz failed: %s",
                                  host.slot, width, freqKhz, esp_err_to_name(err));
        return false;
    }
    s_info.mounted = true;
    s_info.lastError = ESP_OK;
    s_info.width = (uint8_t)width;
    s_info.freqKhz = (uint32_t)s_card->real_freq_khz;
    memcpy(s_info.name, s_card->cid.name, sizeof(s_card->cid.name));
    s_info.name[sizeof(s_info.name) - 1] = 0;
    s_info.generation++;
    return true;
}

bool mountLocked()
{
    if (s_info.mounted) return true;
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    if (!cardPower(s_recover)) return false;
    const int freq = s_recover ? 10000 : SDMMC_FREQ_DEFAULT;
    bool ok = tryMount(4, freq);
    if (!ok) {
        s_recover = true;
        ok = cardPower(true) && tryMount(1, 10000);
    }
#else
    const bool ok = tryMount(4) || tryMount(1);
#endif
    if (!ok) {
        s_logMissing = false;   // Log the first failure only; poll() retries quietly
        return false;
    }
    s_logMissing = true;
    uint64_t total = 0, freeB = 0;
    if (esp_vfs_fat_info(MOUNT, &total, &freeB) == ESP_OK) {
        s_info.totalBytes = total;
        s_info.freeBytes = freeB;
    }
    ESP_LOGI(TAG, "mounted %s: %u-bit %u kHz, %u MB, %u MB free", s_info.name, s_info.width,
             (unsigned)s_info.freqKhz, (unsigned)(s_info.totalBytes >> 20), (unsigned)(s_info.freeBytes >> 20));
    return true;
}

bool unmountLocked()
{
    if (!s_info.mounted) return true;
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
    if (err != ESP_OK) {
        s_info.lastError = err;
        ++s_info.errors;
        ESP_LOGE(TAG, "unmount failed: %s", esp_err_to_name(err));
        return false;
    }
    s_card = nullptr;
    s_info.mounted = false;
    s_info.totalBytes = s_info.freeBytes = 0;
    ESP_LOGW(TAG, "unmounted");
    return true;
}

}  // namespace

void lock()
{
    if (s_mux) xSemaphoreTakeRecursive(s_mux, portMAX_DELAY);
}

void unlock()
{
    if (s_mux) xSemaphoreGiveRecursive(s_mux);
}

bool begin()
{
    if (!s_mux) s_mux = xSemaphoreCreateRecursiveMutex();
    Guard g;
    bool ok = mountLocked();
    if (!ok) ESP_LOGI(TAG, "no SD card - continuing without it");
    return ok;
}

bool mounted() { return s_info.mounted; }

Info info()
{
    Guard g;
    return s_info;
}

void refreshUsage()
{
    Guard g;
    uint64_t total = 0, freeB = 0;
    if (s_info.mounted && esp_vfs_fat_info(MOUNT, &total, &freeB) == ESP_OK) {
        s_info.totalBytes = total;
        s_info.freeBytes = freeB;
    }
}

uint32_t clusterBytes()
{
    Guard g;
    if (!s_info.mounted || !s_card) return 0;
    const BYTE pdrv = ff_diskio_get_pdrv_card(s_card);
    if (pdrv == 0xFF) return 0;
    char drv[3] = {(char)('0' + pdrv), ':', 0};
    FATFS *fs = nullptr;
    DWORD freeClusters = 0;
    if (f_getfree(drv, &freeClusters, &fs) != FR_OK || !fs) return 0;
#if FF_MAX_SS != FF_MIN_SS
    return (uint32_t)fs->csize * fs->ssize;
#else
    return (uint32_t)fs->csize * FF_MIN_SS;
#endif
}

bool remount()
{
    Guard g;
    if (!unmountLocked()) return false;
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    s_recover = true;
#endif
    s_logMissing = true;
    return mountLocked();
}

void poll(uint32_t nowMs)
{
    if (!s_mux || (int32_t)(nowMs - s_nextPoll) < 0) return;
    Guard g;
    if (s_info.mounted) {
        s_nextPoll = nowMs + 3000;
        const esp_err_t err = sdmmc_get_status(s_card);
        if (err != ESP_OK) {
            s_info.lastError = err;
            ++s_info.errors;
            ESP_LOGW(TAG, "card status failed: %s; unmounting our slot only", esp_err_to_name(err));
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
            s_recover = true;
#endif
            unmountLocked();
        }
    } else {
        s_nextPoll = nowMs + 15000;
        mountLocked();
    }
}

bool ensureDir(const char *path)
{
    if (!path || !*path) return false;
    char buf[160];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return false;
    memcpy(buf, path, n + 1);
    struct stat st;
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] != '/' && buf[i] != 0) continue;
        char c = buf[i];
        buf[i] = 0;
        // Mount points ("/sdcard", "/ffat") are VFS roots and may not stat as directories
        const bool root = strcmp(buf, MOUNT) == 0 || strcmp(buf, "/ffat") == 0;
        if (!root) {
            if (stat(buf, &st) != 0) {
                if (mkdir(buf, 0775) != 0 && errno != EEXIST) return false;
            } else if (!S_ISDIR(st.st_mode)) {
                return false;
            }
        }
        buf[i] = c;
    }
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace BoardSd
