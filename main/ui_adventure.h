// ui_adventure.h - OnTheRoad Engine proof of concept: the 60 x 60 "Testvärld" (v0.7.0).
//
// Screen (320 x 480):
//   header    back, title, gems / keys / boat, render statistics (fps, ms per frame), restart
//   viewport  320 x 320 RGB565 canvas in PSRAM, redrawn by our own tile blitter (no LVGL object
//             per tile). The camera follows the player smoothly (pixel interpolation between cells)
//             and stops at the world edges. Zoom: 32 px tiles (10 x 10 visible) or 16 px (20 x 20).
//   controls  arrow pad (hold = keep walking), Slå (hit the facing cell), Zoom.
//             Holding a finger in the viewport walks towards it; tapping the player hits.
// The world logic is Otr::World (components/otr_engine, host tested); tile art is generated at
// start-up from simple shapes (placeholder until real tilesets are loaded from flash / SD).
//
// Must be called from the LVGL task or with the LVGL lock held.
#pragma once

namespace UiAdventure {
void show();   // Opens the test world; "back" returns to the game menu
}
