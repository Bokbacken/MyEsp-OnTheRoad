// sound_bank.h - named sound events and music with built-in chiptune versions (MML, see
// audio_synth.h). A WAV file on the SD card overrides the built-in version:
//   effects: /sdcard/otr/sounds/<name>.wav     music: /sdcard/otr/music/<name>.wav
// Pure C++ (host-tested by tests/test_audio.cpp).
#pragma once

#include <stdint.h>

#include "audio_synth.h"

namespace Sound {

enum class Sfx : uint8_t {
    CLICK,        // UI button
    DICE_SHAKE,   // Cup shaken / dice rattle
    DICE_ROLL,    // Dice thrown
    DIE_LAND,     // One die lands
    SCORE,        // Points / bonus
    ZERO,         // Zero scored / crossed out
    YATZY,        // Yatzy!
    BUST,         // Bust / farkle
    WIN,          // Game won / results
    STEP,         // Adventure: step
    BUMP,         // Walked into a wall
    PUSH,         // Pushed a block
    HIT,          // Hit (attack)
    BREAK,        // Something broke
    GEM,          // Picked up a gem / treasure
    KEY,          // Picked up a key
    POTION,       // Potion / suit
    HEART,        // Heart (health)
    LIFE,         // Extra life
    DOOR,         // Door / gate opens
    LOCKED,       // Needs a key
    MINE,         // Mine / explosion
    SPLASH,       // Water / fire hiss
    HURT,         // Damage
    LIFE_LOST,    // Lost a life
    GAME_OVER,    // Game over
    CHECKPOINT,   // Checkpoint reached
    EXIT,         // Level exit / win
    TALK,         // Dialogue: typewriter blip
    QUEST,        // Dialogue: quest started / done
    COUNT
};

enum class Music : uint8_t { NONE, ADVENTURE, DICE, COUNT };

const char *sfxName(Sfx s);          // File stem, e.g. "dice_roll" (nullptr if out of range)
const char *sfxMml(Sfx s);           // Built-in single-voice MML
const char *musicName(Music m);      // File stem ("adventure", "dice"); nullptr for NONE
const Synth::Song *musicSong(Music m);  // Built-in song; nullptr for NONE

}  // namespace Sound
