// ui_yatzy.h - LVGL screens for Yatzy / Maxi-Yatzy:
//   menu (variant choice) -> players (1-8, names via the touch char wheel) -> game -> results.
//
// Game screen (320 x 480):
//   header      current player, round, menu button
//   throw area  "kastruta": dice that will be rolled
//   nest        "boet": held dice (tap / swipe a die down to hold it, tap / swipe up to release)
//   actions     Kasta (rolls left) + Skriv (write the selected category)
//   score table scrollable in both directions (up to 8 players); the best suggestion is
//               highlighted in gold, the selected category in orange
//
// Must be called from the LVGL task or with Display::lock() held.
#pragma once

namespace UiYatzy {
void showMenu();
}
