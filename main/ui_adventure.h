// ui_adventure.h - OnTheRoad Engine: adventure worlds (v0.8.0).
//
// Setup screen: world chooser (Testvärlden, Minfältet), walk mode Pro / Assistent (NVS), player
// from the registry or guest; a player with a save gets Continue / New game.
// Game screen (320 x 480):
//   header    menu, gems / keys, energy bar (top right), lives (or lives used), suit charges
//   viewport  320 x 320 RGB565 canvas in PSRAM, redrawn by our own tile blitter (no LVGL object
//             per tile). The camera follows the player smoothly (pixel interpolation between cells)
//             and stops at the world edges. Zoom: 32 px tiles (10 x 10 visible) or 16 px (20 x 20).
//             Tap = walk there (A*, step by step), tap the player = hit, long press in fog = flag.
//   controls  arrow pad (hold = keep walking) with STOP in the middle, Slå (hit the facing cell), Zoom.
// Saves: /ffat/adv_<player id>_<world id>.sav (Otr::World::save image, versioned + CRC32).
// The world logic is Otr::World (components/otr_engine, host tested); tile art is generated at
// start-up from simple shapes (placeholder until real tilesets are loaded from flash / SD).
//
// Must be called from the LVGL task or with the LVGL lock held.
#pragma once

namespace UiAdventure {
void show();   // Opens the test world; "back" returns to the game menu
}
