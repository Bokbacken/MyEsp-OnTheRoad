#pragma once
using SemaphoreHandle_t = void*;
inline void *xSemaphoreCreateMutex(){ return (void*)1; }
inline void xSemaphoreTake(void*,int){}
inline void xSemaphoreGive(void*){}
