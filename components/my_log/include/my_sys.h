// my_sys.h - tiny time helpers shared by all MyEsp components (replaces Arduino millis/delay).
#pragma once

#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace MySys {
inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }
inline uint64_t millis64() { return (uint64_t)(esp_timer_get_time() / 1000); }
inline void delayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
template <typename T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace MySys
