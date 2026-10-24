// otr_maps.h - Built-in maps for the OnTheRoad Engine (until maps are loaded from flash / SD).
//
// Legend (one character per cell, see otr_world.cpp decode()):
//   terrain   .  grass     ,  sand      _  stone floor   #  wall     %  cracked wall (hit twice)
//             T  tree      ~  water     ^  lava          o  pressure plate         =  gate
//   objects   R/r rock on grass/floor (push)   c/C crate on grass/floor (hit)   k/K key grass/floor
//             D  locked door (floor)           B  boat (sand)   * + $  gem on grass/floor/sand
//   start     S  (grass)
#pragma once

namespace Otr {

constexpr int TEST_WORLD_W = 60;
constexpr int TEST_WORLD_H = 60;
extern const char *const TEST_WORLD[TEST_WORLD_H];

}  // namespace Otr
