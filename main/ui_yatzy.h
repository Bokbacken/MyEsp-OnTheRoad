// ui_yatzy.h - LVGL screens for Yatzy / Maxi-Yatzy:
//   menu (variant choice) -> players (1-8, names via the touch char wheel) -> game -> results.
//
// Game screen (320 x 480), one screen with two animated phases:
//   ROLL phase (rolls 1-3)
//     header       current player, round, menu button
//     felt         big dice area (~68 % of the height); free dice are scattered in it
//     nest         subtle darker framed strip at the felt's bottom (no label) for held dice.
//                  Tap / swipe a die down -> it glides into the nest; tap / swipe up -> it
//                  glides back and is rolled on the next Kasta. Held dice never move on a roll.
//     suggestions  translucent floating card (opa 180) with the top-5 categories + points;
//                  tapping one jumps to the result phase with it selected
//     buttons      Kasta (rolls left) + Välj ruta
//     Roll animation: 800 ms, faces change at ~15 fps for 400 ms then slow down (ease-out),
//     shake/rotation fade out, each die lands with a 110 % -> 100 % bounce; LED pulses
//     white/gold.
//   RESULT phase (automatically after the last roll, or via Välj / a suggestion)
//     upper half   all dice in one row (the whole felt becomes the nest)
//     action bar   "Kasta n" (back to rolling while rolls remain) + Skriv
//     lower half   full score table, scrollable in both directions (up to 8 players); best
//                  suggestion in gold, selected category in orange (tap twice to write)
//
// Must be called from the LVGL task or with Display::lock() held.
#pragma once

#include "lvgl.h"

namespace UiYatzy {
struct CupVisual {
    lv_obj_t *rim;
    lv_obj_t *lip;
    lv_obj_t *bottom;
};

const void *faceSprite(int32_t size, int face);
CupVisual createCupVisual(lv_obj_t *parent, int32_t diameter);
void showMenu();
}
