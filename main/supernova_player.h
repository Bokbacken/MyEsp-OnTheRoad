#pragma once

#include <stdint.h>

namespace SupernovaPlayer {

// Logical (landscape) rectangle that LVGL never draws over while video plays; call before start().
void setVideoArea(int x, int y, int w, int h);
bool start(const char *audioPath, const char *videoPath);
void stop();
bool pause();
bool resume();
uint32_t positionMs();
const char *error();

}  // namespace SupernovaPlayer
