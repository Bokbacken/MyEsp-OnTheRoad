#include "my_lvgl.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if LVGL_VERSION_MAJOR != 9
#error "my_lvgl targets LVGL 9.x"
#endif
#if LV_COLOR_DEPTH != 16
#error "my_lvgl expects LV_COLOR_DEPTH 16 (CONFIG_LV_COLOR_DEPTH_16)"
#endif

static const char *TAG = "lvgl";

namespace {

MyLvgl::Config g_cfg;
lv_display_t *g_disp = nullptr;
SemaphoreHandle_t g_mutex = nullptr;
TaskHandle_t g_task = nullptr;
MyLvgl::Stats g_stats = {};
bool g_frameStart = true;
uint32_t g_fpsWindowStart = 0;
uint32_t g_fpsFrames = 0;
MyLvgl::PointerFn g_pointer = nullptr;
MyLvgl::PointerFilterFn g_pointerFilter = nullptr;
uint8_t *g_rotBuf = nullptr;   // Software rotation target (PSRAM, one draw buffer), allocated on first use

uint32_t tickMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void rounderCb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    if (!a || g_cfg.align <= 1) return;
    const int32_t m = g_cfg.align - 1;
    a->x1 &= ~m;
    a->y1 &= ~m;
    a->x2 |= m;
    a->y2 |= m;
    // Logical resolution: swapped while a 90/270 software rotation is active
    const int32_t w = g_disp ? lv_display_get_horizontal_resolution(g_disp) : g_cfg.hres;
    const int32_t h = g_disp ? lv_display_get_vertical_resolution(g_disp) : g_cfg.vres;
    if (a->x2 >= w) a->x2 = w - 1;
    if (a->y2 >= h) a->y2 = h - 1;
}

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    const int32_t srcW = lv_area_get_width(area), srcH = lv_area_get_height(area);
    const uint32_t n = (uint32_t)srcW * (uint32_t)srcH;
    lv_area_t phys = *area;
    const lv_display_rotation_t rot = lv_display_get_rotation(disp);
    if (rot != LV_DISPLAY_ROTATION_0 && g_rotBuf) {
        // Software rotation (LVGL renders in logical coordinates): rotate the chunk into the
        // rotation buffer and send it to the physical window. 4-px alignment survives because
        // both panel dimensions are multiples of 4.
        lv_display_rotate_area(disp, &phys);
        const uint32_t srcStride = lv_draw_buf_width_to_stride(srcW, LV_COLOR_FORMAT_RGB565);
        const uint32_t dstStride = lv_draw_buf_width_to_stride(lv_area_get_width(&phys), LV_COLOR_FORMAT_RGB565);
        lv_draw_rotate(px, g_rotBuf, srcW, srcH, srcStride, dstStride, rot, LV_COLOR_FORMAT_RGB565);
        px = g_rotBuf;
    }
    if (g_cfg.swapBytes) lv_draw_rgb565_swap(px, n);
    if (g_cfg.flush) g_cfg.flush(phys.x1, phys.y1, phys.x2, phys.y2, (const uint16_t *)px, g_frameStart);
    g_frameStart = false;
    g_stats.flushes++;
    if (lv_display_flush_is_last(disp)) {
        g_frameStart = true;
        g_stats.frames++;
        g_fpsFrames++;
    }
    const uint32_t now = tickMs();
    if (now - g_fpsWindowStart >= 1000) {
        g_stats.fps = g_fpsFrames * 1000.0f / (float)(now - g_fpsWindowStart);
        g_fpsFrames = 0;
        g_fpsWindowStart = now;
    }
    lv_display_flush_ready(disp);
}

void pointerCb(lv_indev_t *, lv_indev_data_t *d)
{
    static int16_t lastX = 0, lastY = 0;
    int16_t x = lastX, y = lastY;
    bool pressed = g_pointer && g_pointer(&x, &y);
    if (g_pointerFilter) pressed = g_pointerFilter(pressed);
    if (pressed) {
        lastX = x;
        lastY = y;
    }
    d->point.x = lastX;
    d->point.y = lastY;
    d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lvglTask(void *)
{
    for (;;) {
        uint32_t waitMs = 5;
        if (xSemaphoreTakeRecursive(g_mutex, portMAX_DELAY) == pdTRUE) {
            waitMs = lv_timer_handler();
            xSemaphoreGiveRecursive(g_mutex);
        }
        if (waitMs < 2) waitMs = 2;
        if (waitMs > 20) waitMs = 20;
        vTaskDelay(pdMS_TO_TICKS(waitMs));
    }
}

void *allocBuf(size_t bytes, bool psram)
{
    return psram ? heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM)
                 : heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

}  // namespace

namespace MyLvgl {

bool begin(const Config &cfg)
{
    if (g_disp) return true;
    g_cfg = cfg;
    g_mutex = xSemaphoreCreateRecursiveMutex();
    if (!g_mutex) return false;

    lv_init();
    lv_tick_set_cb(tickMs);

    // PSRAM first (principle: large, non time-critical buffers go to PSRAM), internal fallback.
    const bool havePsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    size_t bytes = (size_t)cfg.hres * cfg.bufLines * 2;
    void *b1 = nullptr, *b2 = nullptr;
    if (havePsram) {
        b1 = allocBuf(bytes, true);
        b2 = allocBuf(bytes, true);
    }
    g_stats.bufInPsram = b1 && b2;
    if (!g_stats.bufInPsram) {
        ESP_LOGW(TAG, "PSRAM draw buffers unavailable - using internal RAM");
        heap_caps_free(b1);
        heap_caps_free(b2);
        bytes = (size_t)cfg.hres * cfg.fallbackLines * 2;
        b1 = allocBuf(bytes, false);
        b2 = allocBuf(bytes, false);
        if (!b1 || !b2) {
            ESP_LOGE(TAG, "no memory for draw buffers");
            return false;
        }
    }
    g_stats.bufBytes = bytes;

    g_disp = lv_display_create(cfg.hres, cfg.vres);
    if (!g_disp) return false;
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_disp, flushCb);
    lv_display_add_event_cb(g_disp, rounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);
    g_fpsWindowStart = tickMs();
    ESP_LOGI(TAG, "LVGL %d.%d.%d %dx%d, draw buf 2x%u B (%s)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH, cfg.hres, cfg.vres, (unsigned)bytes, g_stats.bufInPsram ? "PSRAM" : "internal");
    return true;
}

bool ok() { return g_disp != nullptr; }

bool setRotation(lv_display_rotation_t rot)
{
    if (!g_disp) return false;
    if (rot != LV_DISPLAY_ROTATION_0 && !g_rotBuf) {
        const bool psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
        g_rotBuf = (uint8_t *)allocBuf(g_stats.bufBytes, psram);
        if (!g_rotBuf) {
            ESP_LOGE(TAG, "no memory for the rotation buffer (%u B)", (unsigned)g_stats.bufBytes);
            return false;
        }
    }
    if (lv_display_get_rotation(g_disp) == rot) return true;
    lv_display_set_rotation(g_disp, rot);   // Swaps the logical resolution, resizes the screens
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI(TAG, "software rotation %d", (int)rot * 90);
    return true;
}

lv_display_rotation_t rotation() { return g_disp ? lv_display_get_rotation(g_disp) : LV_DISPLAY_ROTATION_0; }
lv_display_t *display() { return g_disp; }

lv_indev_t *addPointer(PointerFn fn)
{
    if (!g_disp || !fn) return nullptr;
    g_pointer = fn;
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, pointerCb);
    lv_indev_set_display(in, g_disp);
    return in;
}

void setPointerFilter(PointerFilterFn fn) { g_pointerFilter = fn; }

bool startTask(int core, int prio, uint32_t stackBytes)
{
    if (!g_disp || g_task) return false;
    return xTaskCreatePinnedToCore(lvglTask, "lvgl", stackBytes, nullptr, prio, &g_task, core) == pdPASS;
}

bool lock(uint32_t timeoutMs)
{
    if (!g_mutex) return false;
    return xSemaphoreTakeRecursive(g_mutex, timeoutMs ? pdMS_TO_TICKS(timeoutMs) : portMAX_DELAY) == pdTRUE;
}

void unlock()
{
    if (g_mutex) xSemaphoreGiveRecursive(g_mutex);
}

Stats stats()
{
    Stats s = g_stats;
    lv_mem_monitor_t mon = {};
    if (lock(50)) {
        lv_mem_monitor(&mon);
        unlock();
    }
    s.lvMemTotal = mon.total_size;
    s.lvMemFree = mon.free_size;
    s.lvMemMaxUsed = mon.max_used;
    s.lvMemFrag = mon.frag_pct;
    s.taskStackFree = g_task ? uxTaskGetStackHighWaterMark(g_task) : 0;
    return s;
}

}  // namespace MyLvgl
