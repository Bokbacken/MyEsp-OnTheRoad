// otr_story_village.cpp - "Byn": the dialogue demo of v0.11.0 (no Bible text).
//
// Bullbert the baker (1) gives you a bun for Fårhild the shepherd (2), who blocks the gap in the
// tree line until she has had breakfast. Old Brunolf at the well (3) has a riddle; solve it and
// you get the key to the shed. Friendship (counter) unlocks his joke.
#include "otr_stories.h"

namespace Otr {
namespace Story {

namespace {

const Npc NPCS[] = {
    {"baker", {"Bullbert", "Bunbert"}, 0, 1},
    {"shepherd", {"Fårhild", "Woolhild"}, 1, 2},
    {"old", {"Gamle Brunolf", "Old Brunolf"}, 2, 3},
};

const char *const FLAGS[] = {"met_baker", "met_shepherd", "met_old", "riddle_solved"};
const char *const COUNTERS[] = {"friends"};
const Named ITEMS[] = {
    {"bun", {"Bulle", "Bun"}},
};
const Named QUESTS[] = {
    {"breakfast", {"Frukost till Fårhild", "Breakfast for Woolhild"}},
};

// Choices used in more than one node
#define C_BYE {{"Hej då!", "Bye!"}, "END", nullptr, nullptr, {}}
#define C_GIVE_BUN                                                                          \
    {{"Här, ta en bulle!", "Here, have a bun!"}, "shepherd.thanks", "has:bun", "take bun", \
     {"Kräver: bulle", "Needs: bun"}}
#define C_JOKE                                                                                  \
    {{"Berätta något kul!", "Tell me something funny!"}, "old.joke", "friends>=3", nullptr, \
     {"Kräver: fler vänner i byn", "Needs: more friends in town"}}

const Node NODES[] = {
    // ---- Bullbert the baker ------------------------------------------------------------------
    {"baker.start", "!met_baker", "baker",
     {"Hallå där! Jag är Bullbert, byns bagare. Mina bullar är så fluffiga att dom nästan flyger iväg.",
      "Hi there! I'm Bunbert, the town baker. My buns are so fluffy they almost fly away."},
     nullptr, nullptr, "set met_baker",
     {{{"Kan jag få en bulle?", "Can I have a bun?"}, "baker.bun", nullptr, nullptr, {}},
      {{"Vem bor här i byn?", "Who lives in this town?"}, "baker.who", nullptr, nullptr, {}},
      C_BYE}},
    {"baker.start", "quest:breakfast=active", "baker",
     {"Har du inte gett bullen till Fårhild än? Den blir inte varmare av att ligga i fickan.",
      "Haven't you given the bun to Woolhild yet? It won't get any warmer in your pocket."},
     nullptr, nullptr, nullptr,
     {{{"Jag springer dit nu!", "I'm running there now!"}, "END", nullptr, nullptr, {}},
      {{"Var står hon nu igen?", "Where is she again?"}, "baker.where", nullptr, nullptr, {}}}},
    {"baker.start", "quest:breakfast=done", "baker",
     {"Fårhild sa att bullen var toppen! Du är en riktig kompis.",
      "Woolhild said the bun was amazing! You're a real friend."},
     nullptr, nullptr, nullptr,
     {{{"Vem bor här i byn?", "Who lives in this town?"}, "baker.who", nullptr, nullptr, {}}, C_BYE}},
    {"baker.start", nullptr, "baker",
     {"Välkommen tillbaka! Ugnen är varm och humöret ännu varmare.",
      "Welcome back! The oven is hot and my mood is even hotter."},
     nullptr, nullptr, nullptr,
     {{{"Kan jag få en bulle?", "Can I have a bun?"}, "baker.bun", "quest:breakfast=none", nullptr, {}},
      {{"Vem bor här i byn?", "Who lives in this town?"}, "baker.who", nullptr, nullptr, {}},
      C_BYE}},
    {"baker.bun", nullptr, "baker",
     {"Absolut! Men gör mig en tjänst: ge den till herden Fårhild. Hon står i luckan i trädraden och vägrar flytta sig innan hon fått frukost.",
      "Sure! But do me a favour: give it to Woolhild the shepherd. She stands in the gap in the trees and won't move until she's had breakfast."},
     nullptr, nullptr, "give bun; quest breakfast active; add friends 1",
     {{{"Klart, jag fixar det!", "Sure, I'm on it!"}, "END", nullptr, nullptr, {}},
      {{"Varför flyttar hon inte?", "Why won't she move?"}, "baker.why", nullptr, nullptr, {}}}},
    {"baker.why", nullptr, "baker",
     {"Hon säger att fåren blir nervösa om hon går därifrån hungrig. Fåren håller med, tror jag.",
      "She says the sheep get nervous if she leaves hungry. The sheep seem to agree."},
     nullptr, nullptr, nullptr,
     {{{"Okej, jag går dit.", "Okay, I'm going."}, "END", nullptr, nullptr, {}}}},
    {"baker.where", nullptr, "baker",
     {"Norrut, i luckan i den långa trädraden. Följ doften av hungrigt får.",
      "Up north, in the gap in the long row of trees. Follow the smell of hungry sheep."},
     nullptr, nullptr, nullptr,
     {{{"Tack!", "Thanks!"}, "END", nullptr, nullptr, {}}}},
    {"baker.who", nullptr, "baker",
     {"Fårhild vaktar fåren i norr. Gamle Brunolf sitter vid brunnen och älskar gåtor. Och jag bakar. Det är hela byn!",
      "Woolhild guards the sheep up north. Old Brunolf sits by the well and loves riddles. And I bake. That's the whole town!"},
     nullptr, nullptr, nullptr,
     {{{"Tillbaka", "Back"}, "baker.start", nullptr, nullptr, {}}, C_BYE}},

    // ---- Fårhild the shepherd ----------------------------------------------------------------
    {"shepherd.start", "quest:breakfast=done", "shepherd",
     {"Mätt och glad! Vägen norrut är fri. Utgången ligger där borta i hörnet.",
      "Full and happy! The way north is clear. The exit is over there in the corner."},
     nullptr, nullptr, nullptr,
     {C_BYE}},
    {"shepherd.start", "!met_shepherd", "shepherd",
     {"Stopp! Ingen går förbi Fårhild på tom mage. Min mage, alltså.",
      "Stop! Nobody gets past Woolhild on an empty stomach. My stomach, that is."},
     nullptr, nullptr, "set met_shepherd",
     {C_GIVE_BUN,
      {{"Varför står du här?", "Why are you standing here?"}, "shepherd.why", nullptr, nullptr, {}},
      {{"Okej, hej då.", "Okay, bye."}, "END", nullptr, nullptr, {}}}},
    {"shepherd.start", nullptr, "shepherd",
     {"Fortfarande hungrig. Fåren tittar på mig med medlidande.",
      "Still hungry. The sheep look at me with pity."},
     nullptr, nullptr, nullptr,
     {C_GIVE_BUN, C_BYE}},
    {"shepherd.why", nullptr, "shepherd",
     {"Fåren har bestämt att jag ska stå här. Dom är fler än jag, så dom vinner alla omröstningar.",
      "The sheep decided that I stand here. There are more of them, so they win every vote."},
     nullptr, nullptr, nullptr,
     {C_GIVE_BUN, {{"Jag ska fixa något.", "I'll find something."}, "END", nullptr, nullptr, {}}}},
    {"shepherd.thanks", nullptr, "shepherd",
     {"Mmm! Bullbert har gjort det igen. Okej, okej, jag flyttar på mig.",
      "Mmm! Bunbert did it again. Okay, okay, I'll move."},
     nullptr, nullptr, "quest breakfast done; add friends 2; sound quest",
     {{{"Tack, Fårhild!", "Thanks, Woolhild!"}, "shepherd.moves", nullptr, nullptr, {}}}},
    {"shepherd.moves", nullptr, nullptr,
     {"Fårhild kliver åt sidan med bullsmulor på hakan. Fåren bräker gillande.",
      "Woolhild steps aside with crumbs on her chin. The sheep bleat approvingly."},
     nullptr, nullptr, "world npc_move 2 16 6",
     {{{"Vidare!", "Onwards!"}, "END", nullptr, nullptr, {}}}},

    // ---- Old Brunolf at the well -------------------------------------------------------------
    {"old.start", "!met_old", "old",
     {"Mm? Åh, en besökare! Jag är Gamle Brunolf. Jag har suttit vid den här brunnen i sextio år. Brunnen har inte sagt ett ord.",
      "Hm? Oh, a visitor! I'm Old Brunolf. I've sat by this well for sixty years. The well hasn't said a word."},
     nullptr, nullptr, "set met_old",
     {{{"Kan jag hjälpa dig?", "Can I help you?"}, "old.offer", nullptr, nullptr, {}}, C_BYE}},
    {"old.start", "riddle_solved", "old",
     {"Där är du ju, gåtmästaren! Jag funderar fortfarande på hur du kunde den.",
      "There you are, riddle master! I'm still wondering how you got it."},
     nullptr, nullptr, nullptr,
     {C_JOKE, C_BYE}},
    {"old.start", nullptr, "old",
     {"Välkommen tillbaka. Sugen på en gåta?", "Welcome back. Up for a riddle?"},
     nullptr, nullptr, nullptr,
     {{{"Ja, kör!", "Yes, go!"}, "old.riddle", nullptr, nullptr, {}}, C_JOKE, C_BYE}},
    {"old.offer", nullptr, "old",
     {"Hjälpa? Nej då. Men en gåta kan du få. Klarar du den får du något fint.",
      "Help? Oh no. But you can have a riddle. Solve it and you'll get something nice."},
     nullptr, nullptr, nullptr,
     {{{"Kör!", "Go!"}, "old.riddle", nullptr, nullptr, {}},
      {{"Kanske sen.", "Maybe later."}, "END", nullptr, nullptr, {}}}},
    {"old.riddle", nullptr, "old",
     {"Lyssna noga nu. Vad blir blötare ju mer det torkar?",
      "Listen carefully. What gets wetter the more it dries?"},
     "Brunolfs gåtbok, s. 3", nullptr, nullptr,
     {{{"En fisk", "A fish"}, "old.wrong", nullptr, nullptr, {}},
      {{"En handduk", "A towel"}, "old.right", nullptr, nullptr, {}},
      {{"Brunnen", "The well"}, "old.wrong", nullptr, nullptr, {}},
      {{"Dina strumpor", "Your socks"}, "old.wrong", nullptr, nullptr, {}}}},
    {"old.wrong", nullptr, "old",
     {"Haha, nej! Men bra försök. Vill du gissa igen?", "Haha, no! But nice try. Want to guess again?"},
     nullptr, nullptr, nullptr,
     {{{"Ja!", "Yes!"}, "old.riddle", nullptr, nullptr, {}},
      {{"Nej, hej då.", "No, bye."}, "END", nullptr, nullptr, {}}}},
    {"old.right", nullptr, "old",
     {"Rätt! En handduk! Här, ta nyckeln till skjulet. Jag har ändå glömt vad som finns där inne.",
      "Right! A towel! Here, take the key to the shed. I've forgotten what's in there anyway."},
     nullptr, nullptr, "set riddle_solved; add friends 1; world keys 1; sound key",
     {{{"Tack så mycket!", "Thank you so much!"}, "END", nullptr, nullptr, {}}}},
    {"old.joke", nullptr, "old",
     {"Varför är fåren alltid så lugna? Dom har ullgott humör! ... Fårhild skrattade i alla fall.",
      "Why are sheep always so calm? They're never baa-d tempered! ... Woolhild laughed, at least."},
     nullptr, nullptr, nullptr,
     {{{"Haha, den var dålig!", "Haha, that was bad!"}, "END", nullptr, nullptr, {}}}},
};

#undef C_BYE
#undef C_GIVE_BUN
#undef C_JOKE

template <typename T, int N> constexpr int countOf(const T (&)[N]) { return N; }

}  // namespace

const Script VILLAGE_STORY = {
    NPCS, countOf(NPCS), NODES, countOf(NODES), FLAGS, countOf(FLAGS), COUNTERS, countOf(COUNTERS),
    ITEMS, countOf(ITEMS), QUESTS, countOf(QUESTS),
};

}  // namespace Story
}  // namespace Otr
