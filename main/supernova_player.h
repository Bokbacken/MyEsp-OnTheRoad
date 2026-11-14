#pragma once

#include <stdint.h>

namespace SupernovaPlayer {

bool start(const char *audioPath, const char *videoPath);
void stop();
bool pause();
bool resume();
uint32_t positionMs();
const char *error();

}  // namespace SupernovaPlayer
