// board_sd.cpp - see board_sd.h.
#include "board_sd.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

namespace BoardSd {

namespace {

SemaphoreHandle_t s_mux = nullptr;
sdmmc_card_t *s_card = nullptr;
Info s_info{};
uint32_t s_nextPoll = 0;
bool s_logMissing = true;

bool tryMount(int width)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;   // 20 MHz: robust on long board traces
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
        if (s_logMissing) ESP_LOGW(TAG, "mount %d-bit failed: %s", width, esp_err_to_name(err));
        return false;
    }
    s_info.mounted = true;
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
    if (!tryMount(4) && !tryMount(1)) {
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

void unmountLocked()
{
    if (!s_info.mounted) return;
    esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
    s_card = nullptr;
    s_info.mounted = false;
    s_info.totalBytes = s_info.freeBytes = 0;
    ESP_LOGW(TAG, "unmounted");
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

bool remount()
{
    Guard g;
    unmountLocked();
    s_logMissing = true;
    return mountLocked();
}

void poll(uint32_t nowMs)
{
    if (!s_mux || (int32_t)(nowMs - s_nextPoll) < 0) return;
    Guard g;
    if (s_info.mounted) {
        s_nextPoll = nowMs + 3000;
        if (sdmmc_get_status(s_card) != ESP_OK) {
            ESP_LOGW(TAG, "card removed or not responding");
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
