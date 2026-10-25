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

Status status();

}  // namespace Audio
