// supernova_avi.h - minimal AVI reader for the web player: finds the MJPEG frames and the MP3 audio
// chunks of the files made by tools/supernova_convert.ps1 so a browser can get single JPEG frames and
// the audio as an ordinary MP3 byte stream. Pure C++ (host-testable); reading goes through a callback.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <vector>

namespace SupernovaAvi {

using ReadFn = std::function<bool(uint32_t offset, void *buf, size_t n)>;

struct Chunk {
    uint32_t off = 0;    // File offset of the chunk data
    uint32_t size = 0;
};

constexpr size_t MAX_CHUNKS = 60000;   // Per stream
constexpr uint32_t MAX_HEADER_BYTES = 64 * 1024;

struct Info {
    uint32_t width = 0, height = 0;
    double fps = 0;
    std::vector<Chunk> video;       // MJPEG frames in display order
    std::vector<Chunk> audio;       // MP3 chunks; empty when the file has no MP3 audio
    std::vector<uint32_t> audioEnd; // Running total of audio bytes after each audio chunk
    uint32_t audioBytes() const { return audioEnd.empty() ? 0 : audioEnd.back(); }
    uint32_t durationMs() const { return fps > 0 ? (uint32_t)(video.size() * 1000.0 / fps) : 0; }
};

// False when the file is not an AVI with an MJPEG video stream and an idx1 index.
bool parse(const ReadFn &read, uint32_t fileSize, Info &out);

// Copies up to n bytes of the audio stream (all MP3 chunks back to back) starting at `pos`.
// Returns the number of bytes copied, or 0 on error / at the end.
size_t readAudio(const Info &info, const ReadFn &read, uint32_t pos, uint8_t *buf, size_t n);

}  // namespace SupernovaAvi
