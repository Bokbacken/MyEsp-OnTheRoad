// sound_bank.cpp - built-in chiptune sound effects and music (MML, see audio_synth.h).
#include "sound_bank.h"

namespace Sound {

namespace {

struct SfxDef {
    const char *name;
    const char *mml;
};

// Order must match enum class Sfx. Remember: slide (s) persists until "s0".
const SfxDef SFX[(int)Sfx::COUNT] = {
    {"click",      "t240 @1 v9 x0,12,0,6 o6 c32"},
    {"dice_shake", "t300 @5 v9 x0,15,0,10 o5 c32 r32 o6 c32 r32 o5 e32 r32 o6 d32"},
    {"dice_roll",  "t300 @5 v11 x1,20,4,20 o5 c16 o6 c32 o5 g32 o6 d32 o5 a32 o6 e16"},
    {"die_land",   "t240 @5 v12 x0,30,0,20 o3 c32 @3 o3 g32"},
    {"score",      "t240 @1 v11 x2,40,8,40 o5 c16 e16 g16 > c8"},
    {"zero",       "t200 @3 v13 x2,60,8,60 o4 g8 s-7 e4"},
    {"yatzy",      "t220 @1 v12 x2,50,9,60 o5 c16 e16 g16 > c16 < g16 > c16 e16 g4."},
    {"bust",       "t160 @4 v10 x2,80,6,80 o3 s-12 c4"},
    {"win",        "t180 @1 v12 x2,60,9,80 o5 g16 g16 g16 > c4. < g8 > c2"},
    {"step",       "t300 @5 v5 x0,12,0,8 o3 c32"},
    {"bump",       "t240 @3 v13 x0,40,0,30 o2 s-5 c16"},
    {"push",       "t240 @3 v11 x2,60,6,40 o2 s3 g8"},
    {"hit",        "t300 @5 v13 x0,40,0,30 o4 c16 @3 o2 s-12 c16"},
    {"break",      "t300 @5 v13 x0,60,2,60 o5 c16 o4 g16 o3 c8"},
    {"gem",        "t300 @2 v11 x1,40,6,40 o6 e16 b16 > e8"},
    {"key",        "t260 @1 v11 x1,40,7,40 o6 c16 g16 > c16"},
    {"potion",     "t220 @6 v13 x2,50,10,60 o5 s7 c16 e16 g8"},
    {"heart",      "t220 @6 v13 x2,50,10,60 o5 c16 g16 > c8"},
    {"life",       "t200 @1 v12 x2,40,9,60 o5 c16 e16 g16 > c16 e16 g8"},
    {"door",       "t200 @4 v10 x2,80,6,60 o2 s4 c8 s0 g8"},
    {"locked",     "t240 @2 v10 x1,30,6,30 o3 c16 r32 c16"},
    {"mine",       "t120 @5 v15 x0,200,4,250 o4 s-24 c2"},
    {"splash",     "t200 @5 v10 x5,100,3,100 o6 s-12 c4"},
    {"hurt",       "t260 @4 v12 x1,40,6,40 o4 s-6 c16 r32 a16"},
    {"life_lost",  "t160 @3 v13 x2,60,10,80 o5 c8 < b8 b-8 a4"},
    {"game_over",  "t100 @3 v13 x2,80,10,120 o4 g8 e8 c8 < g4. s-5 g4"},
    {"checkpoint", "t220 @1 v11 x2,40,8,50 o5 g16 > d16 g8"},
    {"exit",       "t200 @1 v12 x2,40,9,80 o5 c16 e16 g16 > c16 r16 < g16 > c4"},
    {"talk",       "t300 @2 v6 x0,8,0,6 o6 c64"},
    {"quest",      "t220 @1 v11 x2,40,8,60 o5 g16 > c16 e16 g8"},
};

// ---- Adventure theme: 8 bars, C major, 132 bpm --------------------------------------------
const char ADV_LEAD[] =
    "t132 @1 v9 l8 x3,80,7,60 o5 "
    "e c d e g4 e d | c d e c < a4 > c d | e g a g e4 d c | d e c < a g2 > | "
    "a4 g a > c4 < a g | e4 g e d2 | c d e g a g e d | c2 r4 < g4 >";
const char ADV_BASS[] =
    "t132 @3 v12 l4 x2,40,10,60 o3 "
    "c c g g | < a a > e e | f f a a | g g d d | f f > c c < | c c e e | g g b b | c g c r";
#define ADV_DRUM_BAR "o2c8 o6c8 o6c8 o6c8 o2c8 o6c8 o6c8 o6c8 "
const char ADV_DRUMS[] =
    "t132 @5 v5 x0,20,0,10 " ADV_DRUM_BAR ADV_DRUM_BAR ADV_DRUM_BAR ADV_DRUM_BAR
    ADV_DRUM_BAR ADV_DRUM_BAR ADV_DRUM_BAR ADV_DRUM_BAR;

// ---- Dice lounge: 8 bars, C major, 112 bpm -----------------------------------------------
const char DICE_LEAD[] =
    "t112 @2 v8 l8 x4,90,6,80 o5 "
    "g e g a g4 e4 | f d f g f4 d4 | e c e g > c4 < b a | g2 r2 | "
    "a f a > c < a4 f4 | g e g b g4 e4 | f a g f e d c d | c2 r2";
const char DICE_BASS[] =
    "t112 @3 v11 l4 x2,40,10,60 o3 "
    "c g e g | d a f a | c g e g | g d < b > d | f > c < a > c < | c g e g | d f g b | c g c r";
#define DICE_DRUM_BAR "r4 o6c4 r4 o6c4 "
const char DICE_DRUMS[] =
    "t112 @5 v4 x0,15,0,10 " DICE_DRUM_BAR DICE_DRUM_BAR DICE_DRUM_BAR DICE_DRUM_BAR
    DICE_DRUM_BAR DICE_DRUM_BAR DICE_DRUM_BAR DICE_DRUM_BAR;

const Synth::Song SONG_ADVENTURE = {{ADV_LEAD, ADV_BASS, ADV_DRUMS, nullptr}, true};
const Synth::Song SONG_DICE = {{DICE_LEAD, DICE_BASS, DICE_DRUMS, nullptr}, true};

}  // namespace

const char *sfxName(Sfx s) { return (int)s < (int)Sfx::COUNT ? SFX[(int)s].name : nullptr; }
const char *sfxMml(Sfx s) { return (int)s < (int)Sfx::COUNT ? SFX[(int)s].mml : nullptr; }

const char *musicName(Music m)
{
    switch (m) {
    case Music::ADVENTURE: return "adventure";
    case Music::DICE: return "dice";
    default: return nullptr;
    }
}

const Synth::Song *musicSong(Music m)
{
    switch (m) {
    case Music::ADVENTURE: return &SONG_ADVENTURE;
    case Music::DICE: return &SONG_DICE;
    default: return nullptr;
    }
}

}  // namespace Sound
