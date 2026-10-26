// otr_maps_joseph.cpp - "Josef" (bibelaventyr, 1 Mosebok 37), kapitel 1: tva kartor.
//
// Kapitel 1 ("Drommaren"): Josefs berattelse enligt 1 Mos 37. Spelaren ar en resande som
// hjalper till i Jakobs lager i Hebron och foljer sedan med mot Dotan.
//   Karta 1 "Jakobs lager" (JOSEPH_CAMP): hamta den brokiga manteln hos Jakob (1) och ge den
//     till Josef (2). Ruben (3) vaktar gapet i tradraden norrut tills Josef ar redo att ga till
//     sina broder; da stiger han at sidan och vagen till Dotan (utgangen X) oppnas.
//   Karta 2 "Vagen till Dotan" (JOSEPH_ROAD): Juda (4) ger dromgatan, en av broderna (5) vill
//     Josef illa men han kastas i gropen i stallet, och kopmannen (6) i karavanen koper Josef
//     och for honom mot Egypten. Nar kopet ar klart oppnas karavanvagen (utgangen X) = kapitel 1 klart.
//
// Grafiken ar avsiktligt platshallare (samma stil som de andra varldarna) tills den riktiga
// 32x32-serien for Josef ar godkand; spel-logiken ar oberoende av grafiken.
// Kartorna ar genererade och verifierade (gen_joseph_maps.py): exakt ett 'S' och ett 'X'
// per karta, blockerande personer i gapen, och framkomlig vag nar de stiger at sidan.
#include "otr_maps.h"

namespace Otr {

const char *const JOSEPH_CAMP_ROWS[30] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T...................X..................T",  // 1
    "T......................................T",  // 2
    "T......................................T",  // 3
    "TTTTTTTTTTTTTTTTTTTT3TTTTTTTTTTTTTTTTTTT",  // 4
    "T......................................T",  // 5
    "T......................................T",  // 6
    "T.......###.................###........T",  // 7
    "T.......#_#.................#_#...+....T",  // 8
    "T.......#_#...T.............#_#........T",  // 9
    "T.........1.............T..............T",  // 10
    "T......................................T",  // 11
    "T.................,~~,.................T",  // 12
    "T.........b.......,~~,.....b...........T",  // 13
    "T.................,,,,.................T",  // 14
    "T....TT..........................T.....T",  // 15
    "T......*............2....b........T....T",  // 16
    "T..............T.......................T",  // 17
    "T......................................T",  // 18
    "T......................................T",  // 19
    "T.......###.................###........T",  // 20
    "T.......#_#.................#_#........T",  // 21
    "T.......#_#.......###.......#_#........T",  // 22
    "T.................#_#..................T",  // 23
    "T.....Q.......b...#_#..................T",  // 24
    "T...........T.................T........T",  // 25
    "T.....S................................T",  // 26
    "T......................................T",  // 27
    "T......................................T",  // 28
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 29
};

const char *const JOSEPH_ROAD_ROWS[30] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T...................X..................T",  // 1
    "T......................................T",  // 2
    "T......................................T",  // 3
    "TTTTTTTTTTTTTTTTTTTT6TTTTTTTTTTTTTTTTTTT",  // 4
    "T......................................T",  // 5
    "T......................................T",  // 6
    "T......................................T",  // 7
    "T.....TTT......................TTT.....T",  // 8
    "T......................................T",  // 9
    "T................,,,,,,,...............T",  // 10
    "T................,~~~~~,...............T",  // 11
    "T................,~~~~~,...............T",  // 12
    "T.............b..,~~~~~,..b............T",  // 13
    "T................,,,,,,,...5...........T",  // 14
    "T......................................T",  // 15
    "T........b....................b........T",  // 16
    "T......................................T",  // 17
    "T...........4..........................T",  // 18
    "T......................................T",  // 19
    "T.........TT................TT.........T",  // 20
    "T......................................T",  // 21
    "T......*....b..............b....+......T",  // 22
    "T......................................T",  // 23
    "T..............T....Q....T.............T",  // 24
    "T......................................T",  // 25
    "T......................................T",  // 26
    "T...................S..................T",  // 27
    "T......................................T",  // 28
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 29
};


//                                    start cap  lives            lava mine suit +/max potion heart fog
static constexpr Rules JOSEPH_RULES = {8, 20, LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false};

// The camp map is WORLDS[WORLD_JOSEPH-1] (inlined in otr_maps.cpp); its MapDef.id (4) is both the
// world id (see ui_adventure.cpp, which passes WORLDS[idx].id as the world id) and the camp map's
// stable save id. The road is the second map of the same world, with its own stable save id (5);
// Adventure::Session loads it on travel after the camp exit is reached.
const MapDef JOSEPH_ROAD_DEF = {5, JOSEPH_ROAD_ROWS, JOSEPH_ROAD_H, JOSEPH_RULES, nullptr, 0};

}  // namespace Otr
