// board_sd.cpp - see board_sd.h.
#include "board_sd.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_sdmmc.h"   // After ff.h (needs BYTE)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_protocol_defs.h"
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
esp_err_t (*s_rawTransaction)(int, sdmmc_command_t *) = nullptr;
esp_err_t (*s_transaction)(int, sdmmc_command_t *) = nullptr;   // Raw, or traced in diagnostic builds

// R1 state while a write is still open. P4 has shown a lost auto CMD12 (RTO between transactions):
// the card stays in RCV, ESP-IDF then spins CMD13 for 5 s and every following write is rejected.
constexpr uint32_t R1_STATE_RCV = 6;
constexpr int64_t BUSY_SPIN_US = 2000;
constexpr int64_t STOP_RETRY_US = 500000;
int64_t s_notReadySinceUs = 0;
int64_t s_lastStopUs = -STOP_RETRY_US;
esp_err_t s_writeError = ESP_OK;

void afterStatus(int slot, uint32_t r1)
{
    const uint32_t state = MMC_R1_CURRENT_STATE_STATUS(r1);
    if ((r1 & MMC_R1_READY_FOR_DATA) && state == MMC_R1_CURRENT_STATE_TRAN) {
        s_notReadySinceUs = 0;
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (!s_notReadySinceUs) s_notReadySinceUs = now;
    if (state == R1_STATE_RCV && now - s_lastStopUs >= STOP_RETRY_US) {
        s_lastStopUs = now;
        sdmmc_command_t stop = {};
        stop.opcode = MMC_STOP_TRANSMISSION;
        stop.flags = SCF_CMD_AC | SCF_RSP_R1B | SCF_WAIT_BUSY;
        stop.timeout_ms = 1000;
        esp_err_t err = s_transaction(slot, &stop);
        if (err == ESP_OK) err = stop.error;
        ESP_LOGW(TAG, "card left in receive state (r1=%08x); STOP_TRANSMISSION: %s",
                 (unsigned)r1, esp_err_to_name(err));
        return;
    }
    // Busy card: ESP-IDF polls CMD13 back to back and would starve IDLE on this core.
    if (now - s_notReadySinceUs >= BUSY_SPIN_US) vTaskDelay(1);
}

esp_err_t guardedTransaction(int slot, sdmmc_command_t *cmd)
{
    const esp_err_t err = s_transaction(slot, cmd);
    const esp_err_t result = err == ESP_OK ? cmd->error : err;
    if (cmd->opcode == MMC_WRITE_BLOCK_MULTIPLE && cmd->data) {
        s_writeError = result;
    } else if (cmd->opcode == SD_APP_SEND_NUM_WR_BLOCKS && cmd->data) {
        // ESP-IDF v6.1 returns this ACMD22 result as the failed CMD25's result. Once CMD12 has
        // recovered the card, ACMD22 succeeds and a partial write would be reported as written.
        if (s_writeError != ESP_OK && err == ESP_OK) cmd->error = s_writeError;
        s_writeError = ESP_OK;
    } else if (result == ESP_OK && cmd->opcode == MMC_SEND_STATUS) {
        afterStatus(slot, cmd->response[0]);
    }
    return err;
}
#if defined(CONFIG_OTR_SD_IO_DIAGNOSTICS)
struct IoTrace {
    uint32_t commands = 0, statusPolls = 0;
    uint32_t dataBlock = 0, dataBytes = 0, response = 0;
    int opcode = 0, dataOpcode = 0, core = 0;
    uint32_t arg = 0;
    int64_t startedUs = 0, durationUs = 0;
    esp_err_t error = ESP_OK;
    bool active = false;
};
portMUX_TYPE s_traceMux = portMUX_INITIALIZER_UNLOCKED;
IoTrace s_trace;
TaskHandle_t s_traceTask = nullptr;

esp_err_t tracedTransaction(int slot, sdmmc_command_t *cmd)
{
    const int64_t start = esp_timer_get_time();
    portENTER_CRITICAL(&s_traceMux);
    ++s_trace.commands;
    s_trace.opcode = cmd->opcode;
    s_trace.arg = cmd->arg;
    s_trace.core = xPortGetCoreID();
    s_trace.startedUs = start;
    s_trace.active = true;
    s_trace.statusPolls = cmd->opcode == 13 ? s_trace.statusPolls + 1 : 0;
    if (cmd->data) {
        s_trace.dataOpcode = cmd->opcode;
        s_trace.dataBlock = cmd->arg;
        s_trace.dataBytes = cmd->datalen;
    }
    portEXIT_CRITICAL(&s_traceMux);
    const esp_err_t err = s_rawTransaction(slot, cmd);
    const int64_t duration = esp_timer_get_time() - start;
    portENTER_CRITICAL(&s_traceMux);
    s_trace.durationUs = duration;
    s_trace.error = err == ESP_OK ? cmd->error : err;
    s_trace.response = cmd->response[0];
    s_trace.active = false;
    portEXIT_CRITICAL(&s_traceMux);
    return err;
}

void traceTask(void *)
{
    uint32_t previous = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        portENTER_CRITICAL(&s_traceMux);
        const IoTrace t = s_trace;
        portEXIT_CRITICAL(&s_traceMux);
        if (t.commands == previous && !t.active) continue;
        previous = t.commands;
        const int64_t age = t.active ? esp_timer_get_time() - t.startedUs : 0;
        // No BoardSd lock or card access: this must run while a write owns the card.
        ESP_LOGI("sdtrace", "cmd=%d arg=%08x data_cmd=%d block=%u bytes=%u polls=%u active=%d "
                           "age_ms=%llu last_ms=%llu err=%x r1=%08x core=%d",
                 t.opcode, (unsigned)t.arg, t.dataOpcode, (unsigned)t.dataBlock,
                 (unsigned)t.dataBytes, (unsigned)t.statusPolls, t.active,
                 (unsigned long long)(age / 1000), (unsigned long long)(t.durationUs / 1000),
                 (unsigned)t.error, (unsigned)t.response, t.core);
    }
}
#endif
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
// Slot 0 (card) and slot 1 (ESP-Hosted C6) share one SDMMC controller and its host clock divider;
// only a second divider is per slot. ESP-IDF reprograms the shared divider whenever the next
// transaction is for a slot with another frequency, which can cut off the card's pending auto
// CMD12. Equal clocks keep the divider fixed (160 MHz / 4, no per-slot divider).
constexpr int SHARED_FREQ_KHZ = SDMMC_FREQ_HIGHSPEED;
#if defined(CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ)
static_assert(CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ == SHARED_FREQ_KHZ,
              "SD card and ESP-Hosted SDIO must use the same clock on the shared P4 SDMMC controller");
#endif
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

bool tryMount(int width, int freqKhz = SDMMC_FREQ_DEFAULT, bool formatIfFail = false)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = freqKhz;
    s_rawTransaction = host.do_transaction;
#if defined(CONFIG_OTR_SD_IO_DIAGNOSTICS)
    s_transaction = tracedTransaction;
#else
    s_transaction = s_rawTransaction;
#endif
    host.do_transaction = guardedTransaction;
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
    mcfg.format_if_mount_failed = formatIfFail;
    mcfg.max_files = 8;
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

bool mountLocked(bool formatIfFail = false)
{
    if (s_info.mounted) return true;
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    if (!cardPower(s_recover)) return false;
    // Recovery keeps the shared clock: a slower card clock would reprogram the divider again.
    bool ok = tryMount(4, SHARED_FREQ_KHZ, formatIfFail);
    if (!ok) {
        s_recover = true;
        ok = cardPower(true) && (tryMount(4, SDMMC_FREQ_DEFAULT, formatIfFail) ||
                                 tryMount(1, SDMMC_FREQ_DEFAULT, formatIfFail));
    }
#else
    const bool ok = tryMount(4, SDMMC_FREQ_DEFAULT, formatIfFail) ||
                    tryMount(1, SDMMC_FREQ_DEFAULT, formatIfFail);
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
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    if (s_info.freqKhz != (uint32_t)SHARED_FREQ_KHZ)
        ESP_LOGW(TAG, "card clock %u kHz differs from ESP-Hosted SDIO %d kHz; shared SDMMC divider "
                      "will be reprogrammed on slot switches", (unsigned)s_info.freqKhz, SHARED_FREQ_KHZ);
#endif
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
#if defined(CONFIG_OTR_SD_IO_DIAGNOSTICS)
    // P4 CPU 0 can be starved by the writer; sample independently on CPU 1.
    if (!s_traceTask &&
        xTaskCreatePinnedToCore(traceTask, "sd_trace", 4096, nullptr, 1, &s_traceTask, 1) != pdPASS)
        ESP_LOGE(TAG, "SD transaction sampler could not start");
#endif
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

bool format()
{
    Guard g;
    if (!s_info.mounted && !mountLocked(true)) return false;
    if (!s_card) return false;

    const esp_err_t err = esp_vfs_fat_sdcard_format(MOUNT, s_card);
    if (err != ESP_OK) {
        s_info.lastError = err;
        ++s_info.errors;
        ESP_LOGE(TAG, "format failed: %s", esp_err_to_name(err));

        const esp_err_t unmountErr = esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
        s_card = nullptr;
        s_info.mounted = false;
        s_info.totalBytes = s_info.freeBytes = 0;
        if (unmountErr != ESP_OK)
            ESP_LOGW(TAG, "cleanup after format failure: %s", esp_err_to_name(unmountErr));
        mountLocked();
        s_info.lastError = err;
        return false;
    }

    ++s_info.generation;
    s_info.lastError = ESP_OK;
    uint64_t total = 0, freeB = 0;
    if (esp_vfs_fat_info(MOUNT, &total, &freeB) == ESP_OK) {
        s_info.totalBytes = total;
        s_info.freeBytes = freeB;
    }
    ESP_LOGW(TAG, "formatted SD card as FAT");
    return true;
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
