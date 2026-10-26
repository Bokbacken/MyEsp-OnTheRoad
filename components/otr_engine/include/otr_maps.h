// otr_maps.h - Built-in worlds for the OnTheRoad Engine (until maps are loaded from flash / SD).
//
// Legend (one character per cell, see otr_world.cpp decode()):
//   terrain   .  grass     ,  sand      _  stone floor   #  wall     %  cracked wall (hit twice)
//             T  tree      ~  water     ^  lava          o  pressure plate         =  gate
//             Q  checkpoint (respawn point)              X  exit (goal)
//   objects   R/r rock on grass/floor (push)   c/C crate on grass/floor (hit)   k/K key grass/floor
//             D  locked door (floor)           B  boat (sand)   * + $  gem on grass/floor/sand
//             b  bush (hit once)   p  energy potion   h  heart (+max energy)   u  protective suit
//             l  extra life        m  hidden mine (minesweeper worlds)
//   people    1..9 a person on grass (number = Npc::num in the world's story script, otr_story.h)
//   start     S  (grass)
// Hidden treasures are not in the rows but in a Treasure list per world (MapDef).
#pragma once

#include "otr_world.h"

namespace Otr {

constexpr int TEST_WORLD_W = 60;
constexpr int TEST_WORLD_H = 60;
extern const char *const TEST_WORLD[TEST_WORLD_H];

constexpr int MINE_WORLD_W = 40;
constexpr int MINE_WORLD_H = 30;
extern const char *const MINE_WORLD[MINE_WORLD_H];

constexpr int VILLAGE_W = 32;
constexpr int VILLAGE_H = 22;
extern const char *const VILLAGE[VILLAGE_H];

// Josef (bibeläventyr, 1 Mosebok 37), kapitel 1: two 40x30 maps (camp + road to Dothan).
constexpr int JOSEPH_W = 40;
constexpr int JOSEPH_CAMP_H = 30;
constexpr int JOSEPH_ROAD_H = 30;
extern const char *const JOSEPH_CAMP_ROWS[JOSEPH_CAMP_H];   // camp is WORLDS[WORLD_JOSEPH-1] (map id 4 == world id)
extern const char *const JOSEPH_ROAD_ROWS[JOSEPH_ROAD_H];
extern const MapDef JOSEPH_ROAD_DEF;   // second map of the Josef world; stable map id 5

enum WorldId : uint8_t { WORLD_TEST = 1, WORLD_MINES = 2, WORLD_VILLAGE = 3, WORLD_JOSEPH = 4 };
constexpr int WORLD_COUNT = 4;
extern const MapDef WORLDS[WORLD_COUNT];   // [0] Testvärlden, [1] Minfältet, [2] Byn, [3] Josef (1 Mos 37)

}  // namespace Otr
