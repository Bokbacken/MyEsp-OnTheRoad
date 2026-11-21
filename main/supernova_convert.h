// supernova_convert.h - on-device MP4 (H.264 + AAC/MP3) to AVI (MJPEG + the original audio)
// conversion for the P4 video player. The audio is copied without re-encoding.
#pragma once

#include <stdint.h>

namespace SupernovaConvert {

enum class State : uint8_t { Idle, Running, Done, Failed, Cancelled };

bool start(const char *mp4Path, const char *aviPath);  // False when busy or the task could not start
State state();
int percent();            // 0..100 while running
uint32_t elapsedMs();
const char *error();      // Short English reason after Failed
void cancel();            // Asynchronous; state becomes Cancelled
void reset();             // Idle again after Done/Failed/Cancelled

}  // namespace SupernovaConvert
