// otr_maps.cpp - built-in worlds (v0.8.0): "Testvärlden" 60 x 60 and "Minfältet" 40 x 30.
//
// Testvärlden route: gems lie around the meadow (NW). Push the rock onto the plate to open the
// garden gate and take the key. The key opens the boathouse door in the ruin (NE); a cracked wall
// there hides a gem room. With the boat the river and the lake island (SE) can be reached.
// Alternative: fill the river with three rocks. The lava island (SW) is reached by pushing rocks
// into the lava - or, since v0.8.0, with the protective suit hidden in a crate in the ruin (3 lava
// steps free, more with extra max energy) plus some energy. Treasures hide under crates, bushes,
// cracked walls and the first rock. Checkpoints: south meadow, beach path, lava island.
//
// Minfältet: classic minesweeper on a meadow. 95 hidden mines; revealed ground shows how many
// mines touch it, zeros open up automatically, long-press flags a cell. Reach the exit (SE),
// collect the three gems on the way. Checkpoint in the middle, treasures under the bushes.
// Generated once with a fixed seed (start / checkpoint / exit / gems / bushes have no adjacent
// mines, a mine-free way exists) and kept verbatim.
#include "otr_maps.h"

namespace Otr {

const char *const TEST_WORLD[TEST_WORLD_H] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T.........................bbb.T...........................hT",  // 1
    "T.............................T............................T",  // 2
    "T.......TT..........*.........T..########################..T",  // 3
    "T.........................T...T..#_+_________#__________#..T",  // 4
    "T.*..S........T............T..T..#___________#______C___#..T",  // 5
    "T.............................T..#________%__#____+_____#..T",  // 6
    "T..............############...T..#___________#__________#..T",  // 7
    "T..............#__________#...T..#____r______%__________#..T",  // 8
    "T..TT..........#_______+__#...T..#___________#_______+__#..T",  // 9
    "T..............#____K_____#...T..#___________#__C_____C_#..T",  // 10
    "T..............#__________#...T..#___________#__________#..T",  // 11
    "T..............#__________#...T..#___________############..T",  // 12
    "T..TTTT........#__________#...T..#___________#,,,,,,,,,,#..T",  // 13
    "T..T*.T.....T..#__________#......____________#,,,,,,,,,,#..T",  // 14
    "T..T..T........#####=######......#___________#,,,,,,,,,,#..T",  // 15
    "T..TccT.......................T..#___________#,,,,,,,,,,#..T",  // 16
    "T.............................T..#___________#,,,,~~,,,,#..T",  // 17
    "T....TT...........R...........T..#______CC___D,,,,~~,,,,#..T",  // 18
    "T........................*....T..#___________#,,,,~~,,,,#..T",  // 19
    "T.....................o.......T..#___________#,,B,~~,,,,#..T",  // 20
    "T.............................T..#___r_______#,,,,~~,,,,#..T",  // 21
    "T.T.....R.......R..........T..T..#___________#,,,,~~,,,,#..T",  // 22
    "T...........R.................T..#___________#,,,,~~,,,,#..T",  // 23
    "T.............................T..#################~~#####..T",  // 24
    "T.............................T.....Q.............~~.......T",  // 25
    "T,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,~~,,,,,,,T",  // 26
    "T,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,~~,,,,,,,T",  // 27
    "T~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~T",  // 28
    "T~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~T",  // 29
    "T~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~T",  // 30
    "T,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,T",  // 31
    "T,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,T",  // 32
    "T.............................Q............................T",  // 33
    "T.............R............................................T",  // 34
    "T........R......R....R...........~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 35
    "T......R....R......R........T....~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 36
    "T................................~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 37
    "T....^^^^^^^^^^^^^^^^^^^.........~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 38
    "T....^^^^^^^^^^^^^^^^^^^.........~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 39
    "T..T.^^^^^^^^^^^^^^^^^^^.....T...~~~~~~,,,,,,,,,,,,,,~~~~~.T",  // 40
    "T..T.^^^^^^^^^^^^^^^^^^^.........~~~~~~,,,,,,,,,,,,,,~~~~~.T",  // 41
    "T....^^^^^^^^^^^^^^^^^^^.........~~~~~~,,####_#####,,~~~~~.T",  // 42
    "T....^^^^^^.......^^^^^^.........~~~~~~,,#________#,,~~~~~.T",  // 43
    "T....^^^^^^...T...^^^^^^...T.....~~~~~~,,#_+_____C#,,~~~~~.T",  // 44
    "T....^^^^^^..*....^^^^^^.T.......~~~~~~,,#________#,,~~~~~.T",  // 45
    "T....^^^^^^....*..^^^^^^..T......~~~~~~,,###%###%##,,~~~~~.T",  // 46
    "T....^^^^^^.Q.....^^^^^^.........~~~~~~,,#________#,,~~~~~.T",  // 47
    "T....^^^^^^.......^^^^^^......T..~~~~~~,,#_____+__#,,~~~~~.T",  // 48
    "T....^^^^^^^^^^^^^^^^^^^.........~~~~~~,,#__C_____#,,~~~~~.T",  // 49
    "T.T..^^^^^^^^^^^^^^^^^^^.........~~~~~~,,##########,,~~~~~.T",  // 50
    "T....^^^^^^^^^^^^^^^^^^^.........~~~~~~,$,,,,,,,,,,,,~~~~~.T",  // 51
    "T....^^^^^^^^^^^^^^^^^^^....T....~~~~~~,,,,,,,,,,,,,,~~~~~.T",  // 52
    "T....^^^^^^^^^^^^^^^^^^^.........~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 53
    "T................................~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 54
    "T.........R.......R..............~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 55
    "T.......................T....T...~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 56
    "T................................~~~~~~~~~~~~~~~~~~~~~~~~~.T",  // 57
    "T..........................................................T",  // 58
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 59
};

const char *const MINE_WORLD[MINE_WORLD_H] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T......m..........m.....m..............T",  // 1
    "T.S..m..m.........m....m.......mm..m.m.T",  // 2
    "T..............b...m.m...m.....m...m...T",  // 3
    "T........................TT......*.....T",  // 4
    "Tm................m.......m............T",  // 5
    "T.........TTm..m.......m...m.m.........T",  // 6
    "Tm.....m..Tm...........................T",  // 7
    "T..................m....m.........m....T",  // 8
    "T........m..m...............m..m.......T",  // 9
    "T.............##########..m.mm......b.mT",  // 10
    "T..................m..m........m.......T",  // 11
    "T.m..............mm...........T........T",  // 12
    "T.................m...m.b.....TT..m....T",  // 13
    "T..b.....m.....................m.......T",  // 14
    "T...................Q.m...............mT",  // 15
    "T.............m.......m..m...m...mm....T",  // 16
    "T....m..........m.mm.......#.m.........T",  // 17
    "T...........m...m.m........#.........m.T",  // 18
    "T...m....m.................#..........mT",  // 19
    "T.......TT...........*.m...#......TT...T",  // 20
    "T......m.Tm...m.........m..#...........T",  // 21
    "T.m....................m..m#...........T",  // 22
    "T...........m...m..........#......m....T",  // 23
    "T.............m...TT.......m.....m.....T",  // 24
    "T.....*.m..............................T",  // 25
    "T...........b...............p...mm.m...T",  // 26
    "T.........m............m.......m.....X.T",  // 27
    "T.....mm..m......m...............m.....T",  // 28
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 29
};

// Byn (v0.11.0): dialogue demo, no Bible text yet (see otr_story_village.cpp). The baker (1)
// outside his bakery sends you with a bun to the shepherd (2) who blocks the gap in the tree line; the old man at
// the well (3) has a riddle and the key to the shed (gems). Behind the shepherd: meadow + exit.
const char *const VILLAGE[VILLAGE_H] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T.....*...............b.....X..T",  // 1
    "T..b.......TT.............*....T",  // 2
    "T.....................TT.......T",  // 3
    "T..*.......b......*............T",  // 4
    "TTTTTTTTTTTTTTT2TTTTTTTTTTTTTTTT",  // 5
    "T..............................T",  // 6
    "T..#######..............#####..T",  // 7
    "T..#_____#.....,,,,.....#_+_#..T",  // 8
    "T..#_____#.....,~~,.....#___#..T",  // 9
    "T..#_____#.....,~~,3....#+__#..T",  // 10
    "T..##___##.....,,,,.....##D##..T",  // 11
    "T.......1......................T",  // 12
    "T....b.......c.........b.......T",  // 13
    "T.......TT..........TT.........T",  // 14
    "T..............................T",  // 15
    "T...b.....................*....T",  // 16
    "T.........TTT.......T..........T",  // 17
    "T..T...........S.........c.....T",  // 18
    "T.......*...........TT.........T",  // 19
    "T..............................T",  // 20
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 21
};

static const Treasure VILLAGE_TREASURES[] = {
    {13, 13, Obj::POTION, 2}, {22, 1, Obj::GEM, 0}, {25, 18, Obj::HEART, 1},
};

static const Treasure TEST_TREASURES[] = {
    {52, 5, Obj::HEART, 1},  {48, 10, Obj::POTION, 3}, {54, 10, Obj::SUIT, 0},  {40, 18, Obj::POTION, 2},
    {41, 18, Obj::HEART, 1}, {42, 6, Obj::HEART, 2},   {48, 46, Obj::POTION, 3}, {4, 16, Obj::POTION, 2},
    {18, 18, Obj::POTION, 1}, {26, 1, Obj::HEART, 1},  {28, 1, Obj::POTION, 2},
};

static const Treasure MINE_TREASURES[] = {
    {15, 3, Obj::POTION, 3}, {3, 14, Obj::HEART, 1}, {24, 13, Obj::LIFE, 0}, {36, 10, Obj::POTION, 2},
    {12, 26, Obj::HEART, 2},
};

//                                 start cap  lives           lava mine suit +/max potion heart fog
static constexpr Rules TEST_RULES = {8, 20, LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};
static constexpr Rules MINE_RULES = {8, 20, 3, 3, 4, 3, 1, 2, 1, true, 0};
static constexpr Rules VILLAGE_RULES = {8, 20, LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};

const MapDef WORLDS[WORLD_COUNT] = {
    {WORLD_TEST, TEST_WORLD, TEST_WORLD_H, TEST_RULES, TEST_TREASURES,
     (int)(sizeof(TEST_TREASURES) / sizeof(TEST_TREASURES[0])), nullptr, 0},
    {WORLD_MINES, MINE_WORLD, MINE_WORLD_H, MINE_RULES, MINE_TREASURES,
     (int)(sizeof(MINE_TREASURES) / sizeof(MINE_TREASURES[0])), nullptr, 0},
    {WORLD_VILLAGE, VILLAGE, VILLAGE_H, VILLAGE_RULES, VILLAGE_TREASURES,
     (int)(sizeof(VILLAGE_TREASURES) / sizeof(VILLAGE_TREASURES[0])), nullptr, 0},
    // [3] Josef (1 Mos 37), map 1 "Jakobs läger". Inlined (not a cross-TU const ref) so WORLDS[]
    // is constant-initialized - avoids any static-init-order dependency on otr_maps_joseph.cpp.
    {WORLD_JOSEPH, JOSEPH_CAMP_ROWS, JOSEPH_CAMP_H,
     {8, 20, LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0}, nullptr, 0, nullptr, 0},
};

}  // namespace Otr
