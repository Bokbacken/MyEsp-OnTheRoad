#pragma once

#include <cassert>
#include <mutex>

using SemaphoreHandle_t = std::mutex *;
inline std::mutex g_testSemaphore;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return &g_testSemaphore; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, int timeout)
{
    assert(mutex && timeout == portMAX_DELAY);
    mutex->lock();
    return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex);
    mutex->unlock();
    return pdTRUE;
}
