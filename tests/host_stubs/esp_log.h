#pragma once

inline const char *esp_err_to_name(int) { return "test error"; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
