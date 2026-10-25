// kara_web.h - v0.13.5 web karaoke: WebSocket /ws/karaoke on the device web server.
// Binary frames: 22 050 Hz mono int16 PCM (512 samples = 1 024 bytes, ~43 frames/s) of the mixed
// karaoke output. Text frames: JSON (hello / state / newrow / lyric / clear, see KaraStream::toJson),
// sent in the PCM stream exactly at the sample where the syllable starts.
// While at least one browser is connected AND a song is open, the speaker is muted
// (Audio::setWebStream) so the device does not echo the browser. Disabled with NVS kara_webstream=0
// (the endpoint is then never registered).
#pragma once

#include <stdint.h>

#include "kara_stream.h"

namespace KaraWeb {

constexpr int MAX_CLIENTS = 3;

bool begin();          // Registers /ws/karaoke and starts the sender task (AppWeb::begin)
bool running();        // begin() succeeded

// Karaoke engine hooks (any task). setSong: a song was opened (name = file name) or closed.
void setSong(bool open, const char *name);
// Mixer task, once per block after the sequencer advanced: queues lyric events for the stream.
void onRender(const Midi::Song &song, uint32_t posUs);
int clients();         // Connected browsers

}  // namespace KaraWeb
