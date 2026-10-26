#pragma once
#include <cstdio>
#define ESP_LOGE(tag,fmt,...) printf("E %s " fmt "\n",tag,##__VA_ARGS__)
#define ESP_LOGI(tag,fmt,...) printf("I %s " fmt "\n",tag,##__VA_ARGS__)
#define ESP_LOGW ESP_LOGE
