// board_audio.h - ES8311 codec + speaker amplifier + software mixer.
//
// Start-up order (safe against pops and a missing codec):
//   AMP_EN (GPIO1, active LOW) driven HIGH = amplifier off
//   -> ES8311 probed on the shared I2C bus (0x18)
//   -> I2S0 standard mode, 22050 Hz 16 bit, MCLK 256 fs, DMA running with silence
//   -> codec register init at low DAC volume, ramped up
//   -> amplifier on (GPIO1 LOW) only while sound is enabled.
// If the codec does not answer, the module logs it and every call below is a cheap no-op.
//
// Mixer task (core 0): one music channel (built-in chiptune or a looping WAV streamed from
// /sdcard/otr/music/<name>.wav) + 4 effect channels (built-in MML or a WAV from
// /sdcard/otr/sounds/<name>.wav, loaded once into PSRAM). Calls are non-blocking.
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "sound_bank.h"

namespace Audio {

struct Status {
    bool codecFound;      // ES8311 answered on I2C
    bool running;         // I2S + mixer task running
    bool ampOn;
    Sound::Music music;
    bool musicFromFile;   // Current music streams from the SD card
    uint8_t busyChannels;
    uint8_t filesCached;  // Effect WAVs loaded from SD
    uint32_t underruns;   // Mixer blocks that were late (DMA ran dry)
    bool external;        // An external music source (karaoke) is active
    uint8_t externalLoad; // Its render time in % of a mixer block (smoothed)
    const char *error;    // Last error ("" if none)
};

bool begin(i2c_master_bus_handle_t bus);
bool running();

// Volumes 0..100. soundOn = master switch (also switches the amplifier).
void setVolumes(bool soundOn, uint8_t sfxVol, bool musicOn, uint8_t musicVol);

void play(Sound::Sfx s);
void music(Sound::Music m);          // Sound::Music::NONE stops; same as current = keeps playing
void stopMusic();
void invalidateFiles();              // Files under /sdcard/otr changed: reload WAVs on next use

// External music source (karaoke synth). While set it replaces the music channel (the game music
// is paused, not forgotten). The callback runs in the mixer task and adds n mono samples (16-bit
// scale) to acc; gainQ8 is the master switch * setExternalVolume(). nullptr removes it; the call
// returns once the mixer no longer runs the old callback.
using ExternalRender = void (*)(int32_t *acc, int n, int gainQ8, void *ctx);
void setExternal(ExternalRender fn, void *ctx);
void setExternalVolume(uint8_t vol);   // 0..100
constexpr int BLOCK_SAMPLES = 256;      // Samples per mixer call (at Synth::RATE)

Status status();

// Frees the cached effect WAVs (PSRAM) before the karaoke loads a SoundFont; they load again on
// next use. Returns once the mixer task has done it (max ~100 ms).
void releaseClips();

// External render load since the previous call (then reset): average and peak time in the
// callback in 1/100 % of a mixer block, blocks measured, total late blocks (underruns).
struct ExternalStats {
    uint32_t avgLoad100, maxLoad100, blocks, underruns;
};
ExternalStats takeExternalStats();

}  // namespace Audio
