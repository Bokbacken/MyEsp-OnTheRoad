// otr_story_joseph.cpp - "Josef" (bibelaventyr, 1 Mosebok 37), kapitel 1: "Drommaren".
//
// Two maps (see otr_maps_joseph.cpp). The dialogue is modern spoken Swedish / English prose that
// retells Genesis 37 - it is NOT a verbatim NWT quotation; each node carries the matching Bible
// reference instead. Official figures (Jakob, Josef, Ruben, Juda) with plain, warm names.
//
//   Camp "Jakobs lager": Jakob (1) gives you his ornate robe to bring to Josef (2). Give it to
//     Josef; he tells his two dreams. Ruben (3) blocks the gap in the tree line and only steps
//     aside once Josef is ready to leave for Dotan - then the way north (exit) opens.
//   Road "Vagen till Dotan": Juda (4) tests whether you remember Josef's sheaf dream. The brothers
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
    {"brother", {"En av broderna", "One of the brothers"}, 4, 5},
    {"merchant", {"Kopmannen", "The merchant"}, 5, 6},
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

#define C_BYE {{"Hej da!", "Bye!"}, "END", nullptr, nullptr, {}}

const Node NODES[] = {
    // ---- Jakob (1) ---------------------------------------------------------------------------
    {"jakob.start", "!met_jakob", "jakob",
     {"Du maste vara den resande. Jag ar Jakob. Min son Josef ar sjutton och mitt hjartas ogonsten. "
      "Jag har sytt en brokig mantel at honom - ta den och ge honom den, ar du snall.",
      "You must be the traveller. I am Jacob. My son Joseph is seventeen, the apple of my eye. I made "
      "him an ornate robe - take it and give it to him, please."},
     "1 Mos 37:3", nullptr, "set met_jakob; give robe; quest deliver_robe active",
     {{{"Sa klart, var ar Josef?", "Of course, where is Joseph?"}, "jakob.where", nullptr, nullptr, {}},
      {{"Varfor just Josef?", "Why Joseph especially?"}, "jakob.why", nullptr, nullptr, {}},
      C_BYE}},
    {"jakob.start", "quest:deliver_robe=active", "jakob",
     {"Har du inte gett manteln till Josef an? Han brukar sta nere vid farhagen mitt i lagret.",
      "Haven't you given Joseph the robe yet? He's usually down by the sheep pen in the middle of camp."},
     nullptr, nullptr, nullptr,
     {{{"Jag gar dit nu.", "I'll go now."}, "END", nullptr, nullptr, {}},
      {{"Var star han?", "Where is he?"}, "jakob.where", nullptr, nullptr, {}}}},
    {"jakob.start", "robe_given", "jakob",
     {"Tack for att du gav Josef manteln. Se sa fin den ar pa honom! Hans broder... ja, de ser den ocksa.",
      "Thank you for giving Joseph the robe. See how fine it looks on him! His brothers... yes, they see it too."},
     nullptr, nullptr, nullptr,
     {{{"Vad menar du?", "What do you mean?"}, "jakob.why", nullptr, nullptr, {}}, C_BYE}},
    {"jakob.start", nullptr, "jakob",
     {"Valkommen i mitt lager, resande. Ma du vandra i frid.",
      "Welcome to my camp, traveller. May you walk in peace."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"jakob.where", nullptr, "jakob",
     {"Mitt i lagret, vid farhagen. Ge honom manteln fran mig.",
      "In the middle of camp, by the sheep pen. Give him the robe from me."},
     nullptr, nullptr, nullptr,
     {{{"Tack, Jakob.", "Thanks, Jacob."}, "END", nullptr, nullptr, {}}}},
    {"jakob.why", nullptr, "jakob",
     {"Josef foddes pa min alderdom. Jag vet att jag skammer bort honom. Hans aldre broder blir "
      "avundsjuka - men ett far-hjarta rar man inte over.",
      "Joseph was born in my old age. I know I spoil him. His older brothers grow jealous - but a "
      "father's heart has a will of its own."},
     "1 Mos 37:4", nullptr, "add trust 1",
     {{{"Jag forstar.", "I understand."}, "END", nullptr, nullptr, {}}}},

    // ---- Josef (2) ---------------------------------------------------------------------------
    {"josef.start", "robe_given", "josef",
     {"Tack for manteln! Jag drommde nagot i natt: vi band karvar pa akern, och min karve reste sig "
      "medan era bugade. Konstigt, va?",
      "Thanks for the robe! I had a dream last night: we were binding sheaves, and mine stood up while "
      "yours bowed down to it. Strange, right?"},
     "1 Mos 37:7", nullptr, nullptr,
     {{{"Beratta mer.", "Tell me more."}, "josef.dream2", nullptr, nullptr, {}}, C_BYE}},
    {"josef.start", "has:robe", "josef",
     {"Ah, en mantel! Ar den till mig? Fran far? Far jag se!",
      "Oh, a robe! Is it for me? From Father? Let me see!"},
     nullptr, nullptr, nullptr,
     {{{"Har, den ar din.", "Here, it's yours."}, "josef.getrobe", "has:robe",
       "take robe; set robe_given; set josef_ready; quest deliver_robe done; sound quest", {}},
      C_BYE}},
    {"josef.start", nullptr, "josef",
     {"Hej! Jag ar Josef. Mina broder vaktar hjorden, men far vill ha mig nara sig. Har du nagot till mig?",
      "Hi! I'm Joseph. My brothers tend the flock, but Father likes to keep me close. Do you have something for me?"},
     nullptr, nullptr, nullptr,
     {{{"Inte riktigt an.", "Not quite yet."}, "END", nullptr, nullptr, {}}}},
    {"josef.getrobe", nullptr, "josef",
     {"Wow, titta pa alla farger! Tack! Nu kan jag beratta: jag drommde att vi band karvar, och min "
      "karve stod rak medan era bugade for den.",
      "Wow, look at all the colours! Thank you! Now I can tell you: I dreamed we bound sheaves, and "
      "mine stood upright while yours bowed to it."},
     "1 Mos 37:7", nullptr, "add trust 1",
     {{{"Beratta mer.", "Tell me more."}, "josef.dream2", nullptr, nullptr, {}},
      {{"Sag inte det till broderna!", "Don't tell your brothers!"}, "josef.warn", nullptr, nullptr, {}}}},
    {"josef.dream2", nullptr, "josef",
     {"Jag drommde en gang till: solen, manen och elva stjarnor bugade for mig. Aven far hojde pa "
      "ogonbrynen at den.",
      "I had another dream: the sun, the moon and eleven stars bowed to me. Even Father raised an "
      "eyebrow at that one."},
     "1 Mos 37:9", nullptr, nullptr,
     {{{"Vad kan det betyda?", "What could it mean?"}, "josef.warn", nullptr, nullptr, {}}, C_BYE}},
    {"josef.warn", nullptr, "josef",
     {"Du har ratt, jag borde halla drommarna for mig sjalv. Men de kanns sa verkliga! Far vill att "
      "jag gar till broderna i Dotan. Prata med Ruben forst.",
      "You're right, I should keep the dreams to myself. But they feel so real! Father wants me to go "
      "to my brothers in Dothan. Talk to Reuben first."},
     "1 Mos 37:13", nullptr, nullptr,
     {{{"Jag pratar med Ruben.", "I'll talk to Reuben."}, "END", nullptr, nullptr, {}}}},

    // ---- Ruben (3) - blocks the gap in the tree line ------------------------------------------
    {"ruben.start", "josef_ready & !met_ruben", "ruben",
     {"Sa far skickar lillebror till oss i Dotan? I den dar manteln? Broderna kommer inte gilla det. "
      "Men jag vakar over honom. Kom - jag kliver at sidan, vagen norrut ar din.",
      "So Father sends the little one to us in Dothan? In that robe? The brothers won't like it. But "
      "I'll watch over him. Come - I'll step aside, the way north is yours."},
     "1 Mos 37:21", nullptr, "set met_ruben; quest dotan active",
     {{{"Tack, Ruben.", "Thanks, Reuben."}, "ruben.moves", nullptr, nullptr, {}}}},
    {"ruben.start", "met_ruben", "ruben",
     {"Vagen till Dotan ligger oppen norrut. Ga varsamt - och hall ett oga pa Josef.",
      "The road to Dothan lies open to the north. Tread carefully - and keep an eye on Joseph."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"ruben.start", nullptr, "ruben",
     {"Jag ar Ruben, aldst av broderna. Ge Josef manteln fran far forst - jag slapper ingen norrut "
      "forran han ar redo for Dotan.",
      "I'm Reuben, the eldest. Give Joseph the robe from Father first - I let no one north until he's "
      "ready for Dothan."},
     nullptr, nullptr, nullptr,
     {{{"Jag fixar det.", "I'll sort it out."}, "END", nullptr, nullptr, {}}}},
    {"ruben.moves", nullptr, nullptr,
     {"Ruben stiger at sidan. Gapet i tradraden oppnar sig, och stigen mot Dotan stracker sig norrut.",
      "Reuben steps aside. The gap in the trees opens, and the path to Dothan stretches north."},
     nullptr, nullptr, "world npc_move 3 18 6; sound quest",
     {{{"Mot Dotan!", "To Dothan!"}, "END", nullptr, nullptr, {}}}},

    // ---- Juda (4) - the dream riddle ----------------------------------------------------------
    {"juda.start", "dreams_solved", "juda",
     {"Du lyssnar bra, resande. Vi broder har mycket att sta i. Men var ar Josef nu igen...?",
      "You listen well, traveller. We brothers have much to do. But where is Joseph now...?"},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"juda.start", "!met_juda", "juda",
     {"Hall! Jag ar Juda. Sa du reser med var bortskamde lillebror? Sag mig, du som hort hans prat: "
      "vad drommde Josef om karvarna pa akern?",
      "Halt! I'm Judah. So you travel with our spoiled little brother? Tell me, you who heard his talk: "
      "what did Joseph dream about the sheaves in the field?"},
     "1 Mos 37:7", nullptr, "set met_juda",
     {{{"Era karvar bugade for hans.", "Yours bowed to his."}, "juda.right", nullptr, nullptr, {}},
      {{"Hans karve brann upp.", "His sheaf burned up."}, "juda.wrong", nullptr, nullptr, {}},
      {{"Alla karvar flog bort.", "All the sheaves flew away."}, "juda.wrong", nullptr, nullptr, {}},
      {{"Det fanns inga karvar.", "There were no sheaves."}, "juda.wrong", nullptr, nullptr, {}}}},
    {"juda.start", nullptr, "juda",
     {"Na? Vad drommde Josef om karvarna?",
      "Well? What did Joseph dream about the sheaves?"},
     nullptr, nullptr, nullptr,
     {{{"Era karvar bugade for hans.", "Yours bowed to his."}, "juda.right", nullptr, nullptr, {}},
      {{"Hans karve brann upp.", "His sheaf burned up."}, "juda.wrong", nullptr, nullptr, {}},
      {{"Alla flog bort.", "They all flew away."}, "juda.wrong", nullptr, nullptr, {}}}},
    {"juda.wrong", nullptr, "juda",
     {"Ha! Du lyssnade visst inte. Gissa igen.", "Ha! You weren't listening. Guess again."},
     nullptr, nullptr, nullptr,
     {{{"Forsok igen", "Try again"}, "juda.retry", nullptr, nullptr, {}}}},
    {"juda.retry", nullptr, "juda",
     {"Vad drommde Josef om karvarna pa akern?",
      "What did Joseph dream about the sheaves in the field?"},
     nullptr, nullptr, nullptr,
     {{{"Era karvar bugade for hans.", "Yours bowed to his."}, "juda.right", nullptr, nullptr, {}},
      {{"Hans karve brann upp.", "His sheaf burned up."}, "juda.wrong", nullptr, nullptr, {}},
      {{"De flog bort.", "They flew away."}, "juda.wrong", nullptr, nullptr, {}}}},
    {"juda.right", nullptr, "juda",
     {"Just det. Hans karve reste sig och vara bugade. Och sa solen och manen... Pojken tror att vi "
      "ska buga for honom. Det ska vi minnas. Ga och prata med broderna dar borta.",
      "Exactly. His sheaf rose and ours bowed. And then the sun and moon... The boy thinks we'll bow "
      "to him. We'll remember that. Go talk to the brothers over there."},
     "1 Mos 37:8", nullptr, "set dreams_solved; add trust 1; sound quest",
     {{{"Vad tanker ni gora?", "What will you do?"}, "END", nullptr, nullptr, {}}}},

    // ---- En av broderna (5) - the cistern -----------------------------------------------------
    {"brother.start", "in_pit", "brother",
     {"Pojken sitter i brunnen nu. Ruben vill hamta upp honom sen. Men se - en karavan narmar sig. "
      "Kopmannen dar borta kanske har en annan ide.",
      "The boy's in the cistern now. Reuben wants to pull him out later. But look - a caravan's "
      "coming. The merchant over there may have another idea."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"brother.start", "dreams_solved & !met_brother", "brother",
     {"Dar ar drommaren! Vi har fatt nog av hans stora ord. Nagra vill ta i hart. Men Ruben sager: "
      "inget blod - kasta honom i den tomma brunnen sa lange.",
      "There's the dreamer! We've had enough of his big talk. Some want to go hard. But Reuben says: "
      "no blood - throw him in the empty cistern for now."},
     "1 Mos 37:22", nullptr, "set met_brother",
     {{{"Brunnen, inte varre.", "The cistern, nothing worse."}, "brother.pit", nullptr, nullptr, {}},
      {{"Det har kanns fel.", "This feels wrong."}, "brother.doubt", nullptr, nullptr, {}}}},
    {"brother.start", nullptr, "brother",
     {"Prata med Juda forst, resande. Han vill veta att du minns vad pojken drommde.",
      "Talk to Judah first, traveller. He wants to know you remember what the boy dreamed."},
     nullptr, nullptr, nullptr,
     {{{"Jag gar till Juda.", "I'll go to Judah."}, "END", nullptr, nullptr, {}}}},
    {"brother.doubt", nullptr, "brother",
     {"Fel eller inte - sa blir det. Battre brunnen an varre. Hjalp oss, sa slipper pojken illa.",
      "Wrong or not - that's how it is. Better the cistern than worse. Help us, and the boy avoids the worst."},
     nullptr, nullptr, nullptr,
     {{{"Okej, brunnen da.", "Okay, the cistern then."}, "brother.pit", nullptr, nullptr, {}}}},
    {"brother.pit", nullptr, nullptr,
     {"Broderna sanker ner Josef i den tomma brunnen. Han ropar, men ingen svarar. Pa avstand hors "
      "bjallror - en karavan pa vag mot Egypten.",
      "The brothers lower Joseph into the empty cistern. He calls out, but no one answers. In the "
      "distance, bells - a caravan bound for Egypt."},
     "1 Mos 37:24", nullptr, "set in_pit; add trust 1",
     {{{"Vem kommer dar?", "Who's coming there?"}, "END", nullptr, nullptr, {}}}},

    // ---- Kopmannen (6) - blocks the caravan gap -----------------------------------------------
    {"merchant.start", "sold", "merchant",
     {"Avtalet ar klart. Vi tar val hand om pojken - en sadan reser langt. Vagen med karavanen ligger "
      "oppen norrut.",
      "The deal is done. We'll treat the boy well - one like him goes far. The caravan road lies open "
      "to the north."},
     nullptr, nullptr, nullptr, {C_BYE}},
    {"merchant.start", "in_pit & !met_merchant", "merchant",
     {"Frid, resande! Jag ar kopman fran Gilead, pa vag till Egypten med balsam och myrra. Broderna "
      "sager att de har en ung man att salja. Tjugo siklar silver - vad sager du?",
      "Peace, traveller! I'm a merchant from Gilead, bound for Egypt with balm and myrrh. The brothers "
      "say they have a young man to sell. Twenty shekels of silver - what do you say?"},
     "1 Mos 37:28", nullptr, "set met_merchant",
     {{{"Tjugo siklar, da.", "Twenty shekels, then."}, "merchant.deal", nullptr, nullptr, {}},
      {{"Vad hander med honom?", "What happens to him?"}, "merchant.fate", nullptr, nullptr, {}}}},
    {"merchant.start", nullptr, "merchant",
     {"Min karavan rastar har en stund. Har broderna bestamt sig an?",
      "My caravan rests here a while. Have the brothers decided yet?"},
     nullptr, nullptr, nullptr,
     {{{"Inte riktigt an.", "Not quite yet."}, "END", nullptr, nullptr, {}}}},
    {"merchant.fate", nullptr, "merchant",
     {"Egypten ar inget slut, utan en borjan. Manga som salts dit har stigit hogt. Vem vet vad som "
      "vantar en drommare?",
      "Egypt is no ending, but a beginning. Many sold there have risen high. Who knows what awaits a "
      "dreamer?"},
     "1 Mos 37:36", nullptr, nullptr,
     {{{"Vi gor affar.", "Let's make the deal."}, "merchant.deal", nullptr, nullptr, {}}}},
    {"merchant.deal", nullptr, nullptr,
     {"Silvret byter hander. Kopmannen vinkar in Josef bland kamelerna och lagret bryts upp. "
      "Karavanvagen norrut oppnas - mot Egypten, och mot nasta kapitel.",
      "Silver changes hands. The merchant waves Joseph in among the camels and the camp breaks. The "
      "caravan road north opens - toward Egypt, and the next chapter."},
     "1 Mos 37:28", nullptr, "set sold; quest dotan done; world npc_move 6 18 6; sound quest",
     {{{"Folj karavanen norrut.", "Follow the caravan north."}, "END", nullptr, nullptr, {}}}},
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
