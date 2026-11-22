#pragma once

#include <stdint.h>

namespace SupernovaPlayer {

// Logical (landscape) rectangle that LVGL never draws over while video plays; call before start().
void setVideoArea(int x, int y, int w, int h);
// Rectangle (landscape pixels) the scaled video occupies; false until playback has opened.
bool videoRect(int &x, int &y, int &w, int &h);
// Lyrics overlay: draw ARGB8888 into overlayBack(), then overlayPublish() offsets relative to videoRect().
// Buffers are freed by stop(); capacity covers 800x160 pixels.
uint8_t *overlayBack(uint32_t &capacity);
void overlayPublish(int w, int h, int stride, int dx, int dy);
void overlayClear();
bool start(const char *audioPath, const char *videoPath);
void stop();
bool pause();
bool resume();
// Moves the playback position by deltaMs (clamped to the song); works while playing or paused.
bool seekBy(int32_t deltaMs);
uint32_t positionMs();
// Total length in ms, or 0 if unknown.
uint32_t durationMs();
const char *error();

}  // namespace SupernovaPlayer
