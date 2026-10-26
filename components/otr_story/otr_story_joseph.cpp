// otr_story_joseph.cpp - "Josef" (bibeläventyr, 1 Mosebok 37), kapitel 1: "Drömmaren".
//
// Two maps (see otr_maps_joseph.cpp). The dialogue is modern spoken Swedish / English prose that
// retells Genesis 37 - it is NOT a verbatim NWT quotation; each node carries the matching Bible
// reference instead. Official figures (Jakob, Josef, Ruben, Juda) with plain, warm names.
//
//   Camp "Jakobs läger": Jakob (1) gives you his ornate robe to bring to Josef (2). Josef rests in
//     his tent - a house with a locked door - so you must first find the key hidden in a far corner
//     of the camp, unlock the door, and only then give him the robe. Ruben (3) blocks the gap in
//     the tree line and only steps aside once Josef is ready to leave for Dotan - then the way
//     north (exit) opens.
//   Road "Vägen till Dotan": Juda (4) tests whether you remember Josef's sheaf dream. The brothers
//     (5) lower Josef into the empty cistern (no blood - Ruben's plan), and a Gilead merchant (6),
//     who blocks the caravan gap, buys him for twenty shekels. The sale opens the road north = the
//     chapter is complete and the player walks to the exit.
#include "otr_stories.h"

namespace Otr {
namespace Story {

namespace {

const Npc NPCS[] = {
    {"jakob", {"Jakob", "Jacob"}, 0, 1},
    {"josef", {"Josef", "Joseph"}, 1, 2},
    {"ruben", {"Ruben", "Reuben"}, 2, 3},
    {"juda", {"Juda", "Judah"}, 3, 4},
    {"brother", {"En av bröderna", "One of the brothers"}, 4, 5},
    {"merchant", {"Köpmannen", "The merchant"}, 5, 6},
};

const char *const FLAGS[] = {
    "met_jakob", "robe_given", "josef_ready", "met_ruben",
    "met_juda", "dreams_solved", "met_brother", "in_pit", "met_merchant", "sold",
};
const char *const COUNTERS[] = {"trust"};
const Named ITEMS[] = {
    {"robe", {"Brokig mantel", "Ornate robe"}},
};
const Named QUESTS[] = {
    {"deliver_robe", {"Manteln till Josef", "The robe for Joseph"}},
    {"dotan", {"Mot Dotan", "To Dothan"}},
};

#define C_BYE {{"Hej då!", "Bye!"}, "END", nullptr, nullptr, {}}

const Node NODES[] = {
    // ---- Jakob (1) ---------------------------------------------------------------------------
    {"jakob.start", "!met_jakob", "jakob",
     {"Du måste vara den resande. Jag är Jakob. Min son Josef är sjutton och mitt hjärtas ögonsten. "
      "Jag har sytt en brokig mantel åt honom - ta den och ge honom den, är du snäll.",
      "You must be the traveller. I am Jacob. My son Joseph is seventeen, the apple of my eye. I made "
      "him an ornate robe - take it and give it to him, please."},
     "1 Mos 37:3", nullptr, "set met_jakob; give robe; quest deliver_robe active",
     {{{"Så klart, var är Josef?", "Of course, where is Joseph?"}, "jakob.where", nullptr, nullptr, {}},
      {{"Varför just Josef?", "Why Joseph especially?"}, "jakob.why", nullptr, nullptr, {}},
      C_BYE}},
    {"jakob.start", "quest:deliver_robe=active", "jakob",
     {"Har du inte gett manteln till Josef än? Han vilar i sitt tält mitt i lägret, men dörren är "
      "låst. Nyckeln brukar ligga i ett hörn längst bort - leta rätt på den först.",
      "Haven't you given Joseph the robe yet? He's resting in his tent in the middle of camp, but the "
      "door is locked. The key is usually in a far corner - find it first."},
     nullptr, nullptr, nullptr,
     {{{"Jag letar efter nyckeln.", "I'll look for the key."}, "END", nullptr, nullptr, {}},
      {{"Var står tältet?", "Where is the tent?"}, "jakob.where", nullptr, nullptr, {}}}},
    {"jakob.start", "robe_given", "jakob",
     {"Tack för att du gav Josef manteln. Se så fin den är på honom! Hans bröder... ja, de ser den också.",
      "Thank you for giving Joseph the robe. See how fine it looks on him! His brothers... yes, they see it too."},
     nullptr, nullptr, nullptr,
     {{{"Vad menar du?", "What do you mean?"}, "jakob.why", nullptr, nullptr, {}}, C_BYE}},
    {"jakob.start", nullptr, "jakob",
     {"Välkommen i mitt läger, resande. Må du vandra i frid.",
      "Welcome to my camp, traveller. May you walk in peace."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"jakob.where", nullptr, "jakob",
     {"Josef vilar i sitt tält mitt i lägret - huset med den låsta dörren. Hitta nyckeln i ett hörn, "
      "lås upp och ge honom manteln från mig.",
      "Joseph rests in his tent in the middle of camp - the house with the locked door. Find the key in "
      "a corner, unlock it and give him the robe from me."},
     nullptr, nullptr, nullptr,
     {{{"Tack, Jakob.", "Thanks, Jacob."}, "END", nullptr, nullptr, {}}}},
    {"jakob.why", nullptr, "jakob",
     {"Josef föddes på min ålderdom. Jag vet att jag skämmer bort honom. Hans äldre bröder blir "
      "avundsjuka - men ett far-hjärta rår man inte över.",
      "Joseph was born in my old age. I know I spoil him. His older brothers grow jealous - but a "
      "father's heart has a will of its own."},
     "1 Mos 37:4", nullptr, "add trust 1",
     {{{"Jag förstår.", "I understand."}, "END", nullptr, nullptr, {}}}},

    // ---- Josef (2) - inside the locked tent (house); reachable only after the key ------------
    {"josef.start", "robe_given", "josef",
     {"Tack för manteln! Jag drömde något i natt: vi band kärvar på åkern, och min kärve reste sig "
      "medan era bugade. Konstigt, va?",
      "Thanks for the robe! I had a dream last night: we were binding sheaves, and mine stood up while "
      "yours bowed down to it. Strange, right?"},
     "1 Mos 37:7", nullptr, nullptr,
     {{{"Berätta mer.", "Tell me more."}, "josef.dream2", nullptr, nullptr, {}}, C_BYE}},
    {"josef.start", "has:robe", "josef",
     {"Åh, en mantel! Är den till mig? Från far? Får jag se!",
      "Oh, a robe! Is it for me? From Father? Let me see!"},
     nullptr, nullptr, nullptr,
     {{{"Här, den är din.", "Here, it's yours."}, "josef.getrobe", "has:robe",
       "take robe; set robe_given; set josef_ready; quest deliver_robe done; sound quest", {}},
      C_BYE}},
    {"josef.start", nullptr, "josef",
     {"Hej! Jag är Josef. Mina bröder vaktar hjorden, men far vill ha mig nära sig. Har du något till mig?",
      "Hi! I'm Joseph. My brothers tend the flock, but Father likes to keep me close. Do you have something for me?"},
     nullptr, nullptr, nullptr,
     {{{"Inte riktigt än.", "Not quite yet."}, "END", nullptr, nullptr, {}}}},
    {"josef.getrobe", nullptr, "josef",
     {"Wow, titta på alla färger! Tack! Nu kan jag berätta: jag drömde att vi band kärvar, och min "
      "kärve stod rak medan era bugade för den.",
      "Wow, look at all the colours! Thank you! Now I can tell you: I dreamed we bound sheaves, and "
      "mine stood upright while yours bowed to it."},
     "1 Mos 37:7", nullptr, "add trust 1",
     {{{"Berätta mer.", "Tell me more."}, "josef.dream2", nullptr, nullptr, {}},
      {{"Säg inte det till bröderna!", "Don't tell your brothers!"}, "josef.warn", nullptr, nullptr, {}}}},
    {"josef.dream2", nullptr, "josef",
     {"Jag drömde en gång till: solen, månen och elva stjärnor bugade för mig. Även far höjde på "
      "ögonbrynen åt den.",
      "I had another dream: the sun, the moon and eleven stars bowed to me. Even Father raised an "
      "eyebrow at that one."},
     "1 Mos 37:9", nullptr, nullptr,
     {{{"Vad kan det betyda?", "What could it mean?"}, "josef.warn", nullptr, nullptr, {}}, C_BYE}},
    {"josef.warn", nullptr, "josef",
     {"Du har rätt, jag borde hålla drömmarna för mig själv. Men de känns så verkliga! Far vill att "
      "jag går till bröderna i Dotan. Prata med Ruben först.",
      "You're right, I should keep the dreams to myself. But they feel so real! Father wants me to go "
      "to my brothers in Dothan. Talk to Reuben first."},
     "1 Mos 37:13", nullptr, nullptr,
     {{{"Jag pratar med Ruben.", "I'll talk to Reuben."}, "END", nullptr, nullptr, {}}}},

    // ---- Ruben (3) - blocks the gap in the tree line ------------------------------------------
    {"ruben.start", "josef_ready & !met_ruben", "ruben",
     {"Så far skickar lillebror till oss i Dotan? I den där manteln? Bröderna kommer inte gilla det. "
      "Men jag vakar över honom. Kom - jag kliver åt sidan, vägen norrut är din.",
      "So Father sends the little one to us in Dothan? In that robe? The brothers won't like it. But "
      "I'll watch over him. Come - I'll step aside, the way north is yours."},
     "1 Mos 37:21", nullptr, "set met_ruben; quest dotan active",
     {{{"Tack, Ruben.", "Thanks, Reuben."}, "ruben.moves", nullptr, nullptr, {}}}},
    {"ruben.start", "met_ruben", "ruben",
     {"Vägen till Dotan ligger öppen norrut. Gå varsamt - och håll ett öga på Josef.",
      "The road to Dothan lies open to the north. Tread carefully - and keep an eye on Joseph."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"ruben.start", nullptr, "ruben",
     {"Jag är Ruben, äldst av bröderna. Ge Josef manteln från far först - jag släpper ingen norrut "
      "förrän han är redo för Dotan.",
      "I'm Reuben, the eldest. Give Joseph the robe from Father first - I let no one north until he's "
      "ready for Dothan."},
     nullptr, nullptr, nullptr,
     {{{"Jag fixar det.", "I'll sort it out."}, "END", nullptr, nullptr, {}}}},
    {"ruben.moves", nullptr, nullptr,
     {"Ruben stiger åt sidan. Gapet i trädraden öppnar sig, och stigen mot Dotan sträcker sig norrut.",
      "Reuben steps aside. The gap in the trees opens, and the path to Dothan stretches north."},
     nullptr, nullptr, "world npc_move 3 28 6; sound quest",
     {{{"Mot Dotan!", "To Dothan!"}, "END", nullptr, nullptr, {}}}},

    // ---- Juda (4) - the dream riddle ----------------------------------------------------------
    {"juda.start", "dreams_solved", "juda",
     {"Du lyssnar bra, resande. Vi bröder har mycket att stå i. Men var är Josef nu igen...?",
      "You listen well, traveller. We brothers have much to do. But where is Joseph now...?"},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"juda.start", "!met_juda", "juda",
     {"Håll! Jag är Juda. Så du reser med vår bortskämde lillebror? Säg mig, du som hört hans prat: "
      "vad drömde Josef om kärvarna på åkern?",
      "Halt! I'm Judah. So you travel with our spoiled little brother? Tell me, you who heard his talk: "
      "what did Joseph dream about the sheaves in the field?"},
     "1 Mos 37:7", nullptr, "set met_juda",
     {{{"Era kärvar bugade för hans.", "Yours bowed to his."}, "juda.right", nullptr, nullptr, {}},
      {{"Hans kärve brann upp.", "His sheaf burned up."}, "juda.wrong", nullptr, nullptr, {}},
      {{"Alla kärvar flög bort.", "All the sheaves flew away."}, "juda.wrong", nullptr, nullptr, {}},
      {{"Det fanns inga kärvar.", "There were no sheaves."}, "juda.wrong", nullptr, nullptr, {}}}},
    {"juda.start", nullptr, "juda",
     {"Nå? Vad drömde Josef om kärvarna?",
      "Well? What did Joseph dream about the sheaves?"},
     nullptr, nullptr, nullptr,
     {{{"Era kärvar bugade för hans.", "Yours bowed to his."}, "juda.right", nullptr, nullptr, {}},
      {{"Hans kärve brann upp.", "His sheaf burned up."}, "juda.wrong", nullptr, nullptr, {}},
      {{"Alla flög bort.", "They all flew away."}, "juda.wrong", nullptr, nullptr, {}}}},
    {"juda.wrong", nullptr, "juda",
     {"Ha! Du lyssnade visst inte. Gissa igen.", "Ha! You weren't listening. Guess again."},
     nullptr, nullptr, nullptr,
     {{{"Försök igen", "Try again"}, "juda.retry", nullptr, nullptr, {}}}},
    {"juda.retry", nullptr, "juda",
     {"Vad drömde Josef om kärvarna på åkern?",
      "What did Joseph dream about the sheaves in the field?"},
     nullptr, nullptr, nullptr,
     {{{"Era kärvar bugade för hans.", "Yours bowed to his."}, "juda.right", nullptr, nullptr, {}},
      {{"Hans kärve brann upp.", "His sheaf burned up."}, "juda.wrong", nullptr, nullptr, {}},
      {{"De flög bort.", "They flew away."}, "juda.wrong", nullptr, nullptr, {}}}},
    {"juda.right", nullptr, "juda",
     {"Just det. Hans kärve reste sig och våra bugade. Och så solen och månen... Pojken tror att vi "
      "ska buga för honom. Det ska vi minnas. Gå och prata med bröderna där borta.",
      "Exactly. His sheaf rose and ours bowed. And then the sun and moon... The boy thinks we'll bow "
      "to him. We'll remember that. Go talk to the brothers over there."},
     "1 Mos 37:8", nullptr, "set dreams_solved; add trust 1; sound quest",
     {{{"Vad tänker ni göra?", "What will you do?"}, "END", nullptr, nullptr, {}}}},

    // ---- En av bröderna (5) - the cistern ------------------------------------------------------
    {"brother.start", "in_pit", "brother",
     {"Pojken sitter i brunnen nu. Ruben vill hämta upp honom sen. Men se - en karavan närmar sig. "
      "Köpmannen där borta kanske har en annan idé.",
      "The boy's in the cistern now. Reuben wants to pull him out later. But look - a caravan's "
      "coming. The merchant over there may have another idea."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"brother.start", "dreams_solved & !met_brother", "brother",
     {"Där är drömmaren! Vi har fått nog av hans stora ord. Några vill ta i hårt. Men Ruben säger: "
      "inget blod - kasta honom i den tomma brunnen så länge.",
      "There's the dreamer! We've had enough of his big talk. Some want to go hard. But Reuben says: "
      "no blood - throw him in the empty cistern for now."},
     "1 Mos 37:22", nullptr, "set met_brother",
     {{{"Brunnen, inte värre.", "The cistern, nothing worse."}, "brother.pit", nullptr, nullptr, {}},
      {{"Det här känns fel.", "This feels wrong."}, "brother.doubt", nullptr, nullptr, {}}}},
    {"brother.start", nullptr, "brother",
     {"Prata med Juda först, resande. Han vill veta att du minns vad pojken drömde.",
      "Talk to Judah first, traveller. He wants to know you remember what the boy dreamed."},
     nullptr, nullptr, nullptr,
     {{{"Jag går till Juda.", "I'll go to Judah."}, "END", nullptr, nullptr, {}}}},
    {"brother.doubt", nullptr, "brother",
     {"Fel eller inte - så blir det. Bättre brunnen än värre. Hjälp oss, så slipper pojken illa.",
      "Wrong or not - that's how it is. Better the cistern than worse. Help us, and the boy avoids the worst."},
     nullptr, nullptr, nullptr,
     {{{"Okej, brunnen då.", "Okay, the cistern then."}, "brother.pit", nullptr, nullptr, {}}}},
    {"brother.pit", nullptr, nullptr,
     {"Bröderna sänker ner Josef i den tomma brunnen. Han ropar, men ingen svarar. På avstånd hörs "
      "bjällror - en karavan på väg mot Egypten.",
      "The brothers lower Joseph into the empty cistern. He calls out, but no one answers. In the "
      "distance, bells - a caravan bound for Egypt."},
     "1 Mos 37:24", nullptr, "set in_pit; add trust 1",
     {{{"Vem kommer där?", "Who's coming there?"}, "END", nullptr, nullptr, {}}}},

    // ---- Köpmannen (6) - blocks the caravan gap ------------------------------------------------
    {"merchant.start", "sold", "merchant",
     {"Avtalet är klart. Vi tar väl hand om pojken - en sådan reser långt. Vägen med karavanen ligger "
      "öppen norrut.",
      "The deal is done. We'll treat the boy well - one like him goes far. The caravan road lies open "
      "to the north."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"merchant.start", "in_pit & !met_merchant", "merchant",
     {"Frid, resande! Jag är köpman från Gilead, på väg till Egypten med balsam och myrra. Bröderna "
      "säger att de har en ung man att sälja. Tjugo siklar silver - vad säger du?",
      "Peace, traveller! I'm a merchant from Gilead, bound for Egypt with balm and myrrh. The brothers "
      "say they have a young man to sell. Twenty shekels of silver - what do you say?"},
     "1 Mos 37:28", nullptr, "set met_merchant",
     {{{"Tjugo siklar, då.", "Twenty shekels, then."}, "merchant.deal", nullptr, nullptr, {}},
      {{"Vad händer med honom?", "What happens to him?"}, "merchant.fate", nullptr, nullptr, {}}}},
    {"merchant.start", nullptr, "merchant",
     {"Min karavan rastar här en stund. Har bröderna bestämt sig än?",
      "My caravan rests here a while. Have the brothers decided yet?"},
     nullptr, nullptr, nullptr,
     {{{"Inte riktigt än.", "Not quite yet."}, "END", nullptr, nullptr, {}}}},
    {"merchant.fate", nullptr, "merchant",
     {"Egypten är inget slut, utan en början. Många som sålts dit har stigit högt. Vem vet vad som "
      "väntar en drömmare?",
      "Egypt is no ending, but a beginning. Many sold there have risen high. Who knows what awaits a "
      "dreamer?"},
     "1 Mos 37:36", nullptr, nullptr,
     {{{"Vi gör affär.", "Let's make the deal."}, "merchant.deal", nullptr, nullptr, {}}}},
    {"merchant.deal", nullptr, nullptr,
     {"Silvret byter händer. Köpmannen vinkar in Josef bland kamelerna och lägret bryts upp. "
      "Karavanvägen norrut öppnas - mot Egypten, och mot nästa kapitel.",
      "Silver changes hands. The merchant waves Joseph in among the camels and the camp breaks. The "
      "caravan road north opens - toward Egypt, and the next chapter."},
     "1 Mos 37:28", nullptr, "set sold; quest dotan done; world npc_move 6 11 6; sound quest",
     {{{"Följ karavanen norrut.", "Follow the caravan north."}, "END", nullptr, nullptr, {}}}},
};

#undef C_BYE

template <typename T, int N> constexpr int countOf(const T (&)[N]) { return N; }

}  // namespace

const Script JOSEPH_STORY = {
    NPCS, countOf(NPCS), NODES, countOf(NODES), FLAGS, countOf(FLAGS), COUNTERS, countOf(COUNTERS),
    ITEMS, countOf(ITEMS), QUESTS, countOf(QUESTS),
};

}  // namespace Story
}  // namespace Otr
