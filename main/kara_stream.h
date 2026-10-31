// kara_stream.h - v0.13.5 web karaoke: lock-free hand-over of the mixed karaoke audio and the lyric
// events from the mixer task (producer) to the Wi-Fi sender task (consumer). No ESP-IDF dependencies
// (host-tested in tests/test_karastream.cpp).
//
// PCM: 22 050 Hz mono int16, single-producer/single-consumer ring. The producer never blocks: when
// the ring is full (client too slow) or streaming is disabled (no client) the block is dropped.
// Lyrics: the producer (karaoke render callback, same task as the PCM tap) records an event at the
// stream sample where the block that starts the syllable begins. The consumer sends PCM up to that
// sample, then the event, so the browser shows the text exactly when that audio plays.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "otr_midi.h"

namespace KaraStream {

constexpr int RATE = 22050;
constexpr uint32_t RING_SAMPLES = 16384;   // 32 kB = 0.74 s (power of two)
constexpr uint32_t FRAME_SAMPLES = 512;    // Binary frame size (1 024 bytes, ~43 frames/s)
constexpr uint32_t EVT_CAP = 16;           // Pending lyric events (power of two)
constexpr size_t TEXT_CAP = 1028;          // Two 512-byte .lyr rows plus separators and terminator
constexpr size_t JSON_CAP = TEXT_CAP * 6 + 128;   // Worst case escaping
constexpr char SYL_SEP = '\x1f';           // ROW text: syllables separated by SYL_SEP ...
constexpr char NEXT_SEP = '\x1e';          // ... then NEXT_SEP and the next line's text

enum class Kind : uint8_t {
    ROW,     // A new line started (or resync): text = syllables + next line
    SYL,     // Syllable `syl` (index within the row) started: text = syllable
    CLEAR,   // Before the first line / seek to the start: nothing to show
    COUNTDOWN,   // v0.13.6: long pause - row = dots, syl = lit dots (dots 0 = countdown over)
};

// ---- v0.13.6: countdown dots in long pauses -------------------------------------------------
// A pause starts when the last syllable of a line has been sung (its start + an estimated hold, see
// below) and ends at the first syllable of the next line; before the first line the pause starts at
// 0 (intro). Pauses >= COUNTDOWN_GAP_MS show dots = round(pause s), max COUNTDOWN_MAX, all white at
// first; one dot per second takes the sung-syllable colour, aligned so the last one lights exactly
// when the next line starts. Driven by the song position (synth samples), not the wall clock.
constexpr uint32_t COUNTDOWN_GAP_MS = 3000;
constexpr int COUNTDOWN_MAX = 8;
// Syllables carry only a start time: the last one is assumed to be held for the line's average
// syllable spacing, at most COUNTDOWN_HOLD_MAX_MS (COUNTDOWN_HOLD_ONE_MS for a one-syllable line).
constexpr uint32_t COUNTDOWN_HOLD_MAX_MS = 1000;
constexpr uint32_t COUNTDOWN_HOLD_ONE_MS = 500;

struct Countdown {
    int dots = 0;        // 0 = no countdown at this position
    int elapsed = 0;     // Lit dots (0..dots)
    int nextSyl = -1;    // First syllable of the line the countdown leads to
    uint32_t startUs = 0, endUs = 0;   // Pause start (after the hold) / next line's first syllable
};
// Countdown state at song position posUs (pure; used by the device UI and the web stream).
Countdown countdownAt(const Midi::Song &song, uint32_t posUs);

struct Event {
    uint64_t sample;   // Stream sample (PCM samples pushed before this event)
    uint32_t ts;       // Song position in ms
    int32_t row, syl;
    Kind kind;
    char text[TEXT_CAP];
};

class Stream {
public:
    // Buffers are supplied by the caller (PSRAM on the device). cap and evCap: powers of two.
    void init(int16_t *pcm, uint32_t cap, Event *ev, uint32_t evCap);
    bool ready() const { return pcm_ != nullptr; }

    // ---- Producer (mixer task) ----
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }
    void pushPcm(const int16_t *s, uint32_t n);        // Drops the block when disabled or full
    bool pushEvent(const Event &e);                    // e.sample is set here; false = dropped
    uint64_t written() const { return w_.load(std::memory_order_relaxed); }
    bool takeResync() { return resync_.exchange(false, std::memory_order_acq_rel); }
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    // ---- Consumer (sender task) ----
    void setEnabled(bool on);       // Off -> on drops everything buffered (fresh start)
    void requestResync() { resync_.store(true, std::memory_order_release); }
    uint32_t available() const;
    // Copies the next PCM chunk: up to `max` samples, stopping at the next pending event. With
    // wantFull, returns 0 unless `max` samples are buffered or an event boundary is reached.
    uint32_t readPcm(int16_t *out, uint32_t max, bool wantFull = true);
    // Next event once all PCM before it has been read.
    bool popEvent(Event &e);
    void drain();                   // Drops all buffered PCM and events

private:
    int16_t *pcm_ = nullptr;
    Event *ev_ = nullptr;
    uint32_t cap_ = 0, evCap_ = 0;
    std::atomic<uint64_t> w_{0}, r_{0};       // Samples written / read (monotonic)
    std::atomic<uint32_t> ew_{0}, er_{0};     // Events written / read
    std::atomic<bool> enabled_{false}, resync_{false};
    std::atomic<uint32_t> dropped_{0};
};

// Producer-side lyric follower: call once per mixer block (after the sequencer advanced) with the
// song position. Pushes ROW / SYL / CLEAR events when the line or syllable changes; a resync request
// (new client) re-sends the current line and syllable. Does nothing while the stream is disabled.
class LyricTracker {
public:
    void reset() { row_ = INVALID, syl_ = INVALID, cdDots_ = 0, cdLit_ = -1, cdSyl_ = -1; }
    void update(const Midi::Song &song, uint32_t posUs, Stream &st);

private:
    static constexpr int INVALID = -1000;
    int row_ = INVALID, syl_ = INVALID;
    int cdDots_ = 0, cdLit_ = -1, cdSyl_ = -1;   // Last countdown sent
};

// Copies at most cap-1 bytes of UTF-8 without splitting a character; control characters become
// spaces. Returns the bytes written (0-terminated).
size_t copyUtf8(char *dst, size_t cap, const char *src, size_t n);
// Event -> JSON text frame:
//   ROW:   {"type":"newrow","row":N,"ts":MS,"syls":["..",".."],"next":".."}
//   SYL:   {"type":"lyric","text":"..","row":N,"k":K,"ts":MS}
//   CLEAR: {"type":"clear","ts":MS}
//   COUNTDOWN: {"type":"countdown","dots":N,"elapsed":K,"ts":MS}   (dots 0 = hide)
// Returns the length (0 if cap is too small).
size_t toJson(const Event &e, char *out, size_t cap);
// JSON string body (no quotes) with escaping; returns false when it does not fit.
bool jsonEscape(const char *s, size_t n, char *&p, char *end);

}  // namespace KaraStream
