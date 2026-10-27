// otr_maps_joseph.cpp - "Josef" (bibeläventyr, 1 Mosebok 37), kapitel 1: två kartor.
//
// Kapitel 1 ("Drömmaren"): Josefs berättelse enligt 1 Mos 37. Spelaren är en resande som
// hjälper till i Jakobs läger i Hebron och följer sedan med mot Dotan.
//
//   Karta 1 "Jakobs läger" (JOSEPH_CAMP): ett inhägnat läger som man måste slingra sig genom.
//     Jakob (1) står nära starten och ger den brokiga manteln. Josef (2) vilar inlåst i sitt
//     tält - ett hus (#) med en låst dörr (D) på sydväggen. Nyckeln (k) ligger gömd i ett hörn
//     långt bort (nere till höger), så spelaren måste leta rätt på den, låsa upp dörren och
//     först därefter ge Josef manteln. Ruben (3) vaktar det enda gapet i den norra trädraden
//     tills Josef är redo att gå till sina bröder; då stiger han åt sidan och vägen till Dotan
//     (utgången X) öppnas.
//   Karta 2 "Vägen till Dotan" (JOSEPH_ROAD): en helt annan, öppnare karavanväg som kröker sig
//     mellan stenbarriärer (R) runt en stor torr brunn (den tomma cisternen - en sandring kring
//     vatten) mitt på kartan. Juda (4) ger drömgåtan, en av bröderna (5) vill Josef illa men han
//     sänks i brunnen i stället, och köpmannen (6) i karavanen köper Josef och för honom mot
//     Egypten. När köpet är klart öppnas karavanvägen (utgången X) = kapitel 1 klart.
//
// Nyckel/dörr-pusslet och de blockerande personerna använder enbart motorns befintliga mekanik
// (Obj::KEY, Obj::DOOR, keys_-räknaren och Obj::NPC) - inga nya spar-fält, så kartorna är
// bakåtkompatibla med sparsystem v2. Grafiken är avsiktligt platshållare (samma stil som de
// andra världarna) tills den riktiga 32x32-serien för Josef är godkänd; spel-logiken är
// oberoende av grafiken. Kartorna är ritade och verifierade (tools/design_joseph_maps.py samt
// tests/test_adventure.cpp): exakt ett 'S' och ett 'X' per karta, framkomlig väg till nyckeln,
// Josef oåtkomlig tills dörren låsts upp, och fri väg till utgången när personen i gapet flyttar.
#include "otr_maps.h"

namespace Otr {

const char *const JOSEPH_CAMP_ROWS[30] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T...........................X..........T",  // 1
    "T......................................T",  // 2
    "T......................................T",  // 3
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTT3TTTTTTTTTTT",  // 4  Ruben (3) i gapet
    "T......................................T",  // 5
    "T...TTT...T...........TTTTT............T",  // 6
    "T.........T...b........................T",  // 7
    "T...TT....T.......TT..........T...TTTT.T",  // 8
    "T.........T...................T........T",  // 9
    "T.........T......#######......T........T",  // 10 husets norra vägg
    "T.........T......#_____#......T........T",  // 11
    "T.........T......#__2__#..b...T........T",  // 12 Josef (2) inne i huset
    "T.........T......#_____#......T........T",  // 13
    "T................###D###...............T",  // 14 låst dörr (D) mot söder
    "T....TTTTTT...................T........T",  // 15
    "T.........T......,,,,,........T........T",  // 16 brunn (sand + vatten)
    "T...1.....T......,~~~,........T........T",  // 17 Jakob (1)
    "T.........T......,~~~,........T...TTT..T",  // 18
    "T.........T......,,,,,........T........T",  // 19
    "T.........T.............TTTT..T........T",  // 20
    "T.........T............................T",  // 21
    "T.....b...T...b........................T",  // 22
    "T.........T............................T",  // 23
    "T.........T......................b.....T",  // 24
    "T....Q....T............................T",  // 25 checkpoint
    "T.........T........................k...T",  // 26 nyckeln (k) i hörnet
    "T...S..................................T",  // 27 start
    "T......................................T",  // 28
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 29
};

const char *const JOSEPH_ROAD_ROWS[30] = {
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 0
    "T..........X...........................T",  // 1
    "T......................................T",  // 2
    "T......................................T",  // 3
    "TTTTTTTTTTT6TTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 4  köpmannen (6) i gapet
    "T......................................T",  // 5
    "T...................b..................T",  // 6
    "T......4...............................T",  // 7  Juda (4)
    "T......................................T",  // 8
    "T............RRRRRR....RRRRRR..........T",  // 9  stenbarriärer (chikaner)
    "T.....b...................b............T",  // 10
    "T.............,,,,,,,,,................T",  // 11 cistern: sandring ...
    "T.............,~~~,~~~,................T",  // 12
    "T.............,~~~~~~~,................T",  // 13
    "T.............,~~~~~~~,................T",  // 14
    "T.............,~~~~~~~,.........5......T",  // 15 ... med vatten; en broder (5)
    "T.............,~~~,~~~,................T",  // 16
    "T.............,,,,,,,,,................T",  // 17
    "T........RRRRRR...........RRRRRR.......T",  // 18
    "T......................................T",  // 19
    "T.............R......R.................T",  // 20
    "T.............R......R.................T",  // 21
    "T.....*.......R......R...........+.....T",  // 22 ädelstenar (valfria)
    "T.............R......R.................T",  // 23
    "T.....b.......R......R........b........T",  // 24
    "T.............R......R.................T",  // 25
    "T......................................T",  // 26
    "T..................S...................T",  // 27 start
    "T......................................T",  // 28
    "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",  // 29
};


//                                    start cap  lives            lava mine suit +/max potion heart fog
static constexpr Rules JOSEPH_RULES = {8, 20, LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};

// The camp map is WORLDS[WORLD_JOSEPH-1] (inlined in otr_maps.cpp); its MapDef.id (4) is both the
// world id (see ui_adventure.cpp, which passes WORLDS[idx].id as the world id) and the camp map's
// stable save id. The road is the second map of the same world, with its own stable save id (5);
// Adventure::Session loads it on travel after the camp exit is reached.
const MapDef JOSEPH_ROAD_DEF = {5, JOSEPH_ROAD_ROWS, JOSEPH_ROAD_H, JOSEPH_RULES, nullptr, 0};

}  // namespace Otr
