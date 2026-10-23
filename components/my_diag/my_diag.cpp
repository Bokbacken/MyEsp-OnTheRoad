#include "my_diag.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_core_dump.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_log.h"
#include "my_storage.h"

namespace MyDiag {

static const char *TAG = "diag";
static constexpr int MAX_PROVIDERS = 16;

struct Entry { const char *name; Provider fn; };
static Entry s_prov[MAX_PROVIDERS];
static int s_count = 0;
static uint32_t s_boots = 0;
static bool s_coredump = false;
static const char *s_fw = "";
static const char *s_ver = "";

const char *resetReasonCode()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    default: return "unknown";
    }
}

const char *resetReasonText()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Strömpåslag";
    case ESP_RST_EXT: return "Extern reset";
    case ESP_RST_SW: return "Omstart (mjukvara)";
    case ESP_RST_PANIC: return "Krasch (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Spänningsfall (brownout)";
    case ESP_RST_USB: return "USB-reset";
    case ESP_RST_DEEPSLEEP: return "Väckt ur djupsömn";
    default: return "Okänd";
    }
}

bool lastBootCrashed()
{
    esp_reset_reason_t r = esp_reset_reason();
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
           r == ESP_RST_BROWNOUT;
}

void begin(const char *ns, const char *fwName, const char *fwVersion)
{
    s_fw = fwName;
    s_ver = fwVersion;
    {
        MyNvs::Handle h(ns, false);
        if (h) {
            s_boots = h.getU32("boots", 0) + 1;
            h.setU32("boots", s_boots);
        }
    }
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    s_coredump = esp_core_dump_image_check() == ESP_OK;
#endif
    MyLog::event(TAG, "boot #%lu %s v%s, reset: %s%s", (unsigned long)s_boots, fwName, fwVersion, resetReasonCode(),
                 s_coredump ? ", core dump stored" : "");
}

bool add(const char *name, Provider fn)
{
    if (s_count >= MAX_PROVIDERS || !name || !fn) return false;
    s_prov[s_count++] = {name, fn};
    return true;
}

uint32_t bootCount() { return s_boots; }
bool coreDumpPresent() { return s_coredump; }

cJSON *tasks()
{
    cJSON *arr = cJSON_CreateArray();
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *st = (TaskStatus_t *)heap_caps_malloc(n * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!st) st = (TaskStatus_t *)malloc(n * sizeof(TaskStatus_t));
    if (!st) return arr;
    configRUN_TIME_COUNTER_TYPE total = 0;
    n = uxTaskGetSystemState(st, n, &total);
    static const char *states[] = {"running", "ready", "blocked", "suspended", "deleted", "invalid"};
    for (UBaseType_t i = 0; i < n; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", st[i].pcTaskName);
        cJSON_AddStringToObject(t, "state", states[st[i].eCurrentState < 5 ? st[i].eCurrentState : 5]);
        cJSON_AddNumberToObject(t, "prio", st[i].uxCurrentPriority);
        cJSON_AddNumberToObject(t, "stack_free", st[i].usStackHighWaterMark);
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
        cJSON_AddNumberToObject(t, "core", st[i].xCoreID == tskNO_AFFINITY ? -1 : (int)st[i].xCoreID);
#endif
        if (total) cJSON_AddNumberToObject(t, "cpu_x10", (double)(uint32_t)((uint64_t)st[i].ulRunTimeCounter * 1000 / total));
        cJSON_AddItemToArray(arr, t);
    }
    free(st);
#endif
    return arr;
}

void fill(cJSON *root)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(nullptr, &flash);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macs[18];
    snprintf(macs, sizeof(macs), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON_AddStringToObject(root, "fw", s_fw);
    cJSON_AddStringToObject(root, "version", s_ver);
    char build[40];
    snprintf(build, sizeof(build), "%s %s", app->date, app->time);
    cJSON_AddStringToObject(root, "build", build);
    cJSON_AddStringToObject(root, "sdk", esp_get_idf_version());
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "heap_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "heap_size", heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "heap_largest", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "dma_free", heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "psram_min", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "psram_size", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "flash_size", flash);
    cJSON_AddNumberToObject(root, "cpu_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    cJSON_AddStringToObject(root, "chip", CONFIG_IDF_TARGET);
    cJSON_AddNumberToObject(root, "chip_rev", chip.revision);
    cJSON_AddNumberToObject(root, "cores", chip.cores);
    cJSON_AddStringToObject(root, "mac", macs);
    cJSON_AddNumberToObject(root, "reset_reason", esp_reset_reason());
    cJSON_AddStringToObject(root, "reset_code", resetReasonCode());
    cJSON_AddStringToObject(root, "reset_text", resetReasonText());
    cJSON_AddNumberToObject(root, "boot_count", s_boots);
    cJSON_AddBoolToObject(root, "coredump", s_coredump);
    cJSON_AddNumberToObject(root, "tasks", uxTaskGetNumberOfTasks());
    MyNvs::Usage nu = MyNvs::usage();
    cJSON *nvs = cJSON_AddObjectToObject(root, "nvs");
    cJSON_AddNumberToObject(nvs, "used", nu.used);
    cJSON_AddNumberToObject(nvs, "total", nu.total);
    MyLog::Stats ls = MyLog::stats();
    cJSON *lg = cJSON_AddObjectToObject(root, "log");
    cJSON_AddNumberToObject(lg, "lines", ls.lines);
    cJSON_AddNumberToObject(lg, "capacity", ls.capacity);
    cJSON_AddNumberToObject(lg, "events", ls.events);
    cJSON_AddNumberToObject(lg, "event_errors", ls.eventWriteErrors);
    cJSON_AddBoolToObject(lg, "psram", ls.inPsram);
    for (int i = 0; i < s_count; i++) {
        cJSON *o = cJSON_AddObjectToObject(root, s_prov[i].name);
        if (o) s_prov[i].fn(o);
    }
}

}  // namespace MyDiag
