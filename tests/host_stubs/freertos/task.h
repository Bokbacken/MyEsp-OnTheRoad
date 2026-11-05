#pragma once

using TaskFunction_t = void (*)(void *);
void vTaskDelay(int ticks);
int xTaskCreatePinnedToCore(TaskFunction_t, const char *, int, void *, int, void *, int);
