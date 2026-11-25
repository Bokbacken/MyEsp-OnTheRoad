// board_usb_audio.h - USB Audio Class (UAC 1.0) speaker/headphone output through the ESP32-P4
// USB OTG host port. Other targets compile to inert stubs.
//
// The audio mixer calls push() with every mixed mono block (Audio sample rate); the module
// resamples to the device rate and writes stereo 16-bit PCM without blocking.
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace UsbAudio {

enum class State : uint8_t { OFF, WAITING, OPEN, STREAMING, ERROR };

constexpr int MAX_DEVICES = 4;

struct DeviceInfo {
    char name[40];
    uint16_t vid, pid;
    bool selected;         // Currently streaming
};

struct Status {
    State state;
    bool supported;        // Built for a target with a USB host
    bool deviceSeen;       // Any USB device enumerated since boot
    uint16_t vid, pid;     // Last enumerated USB device (hub, headset, ...)
    char product[40];      // Streaming device name (ASCII)
    uint32_t rate;         // Stream rate in Hz (0 if not streaming)
    uint32_t written, dropped;  // Bytes handed to / refused by the USB driver
    char error[56];
    int deviceCount;       // UAC speakers found (incl. behind hubs)
    DeviceInfo devices[MAX_DEVICES];
    bool internalChosen;   // Owner chose the internal speaker
};

void begin();                                       // Install USB host + UAC driver (P4 only)
bool active();                                      // A headset is streaming
void push(const int16_t *mono, int n, int inRate);  // Mixer task; never blocks
void setVolume(uint8_t percent);                    // 0..100 headset volume (if supported)
// Streams to device `index` (as listed in Status::devices) and remembers the choice across boots;
// -1 selects the internal speaker. Returns false for an unknown index.
bool select(int index);
Status status();
const char *stateName(State s);

}  // namespace UsbAudio
