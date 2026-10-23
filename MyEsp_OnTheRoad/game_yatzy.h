// game_yatzy.h - Swedish Yatzy and Maxi-Yatzy: data model, scoring and turn logic.
//
// Pure C++ (no Arduino/LVGL dependencies) so it can be unit tested on the host:
//   g++ -std=c++17 -I MyEsp_OnTheRoad tests/test_yatzy.cpp MyEsp_OnTheRoad/game_yatzy.cpp
//
// Rules implemented (most common Swedish protocol):
//  Yatzy      - 5 dice, 3 rolls, 15 categories. Upper bonus 50 at >= 63.
//               Ett par, Två par, Tretal, Fyrtal, Liten stege (1-5 = 15), Stor stege (2-6 = 20),
//               Kåk (3+2, sum), Chans (sum), Yatzy (50).
//  Maxi-Yatzy - 6 dice, 3 rolls, 20 categories. Upper bonus 100 at >= 84.
//               Adds Tre par, Femtal, Full stege (1-6 = 21), Hus (3+3, sum), Torn (4+2, sum),
//               Maxi Yatzy (100). Unused rolls are saved for later turns ("sparade kast").
//  Pairs/sets in one combination must have different values (a four-of-a-kind is not two pairs).
//  ORDERED mode ("Yatzy i ordning"): categories must be filled top-to-bottom.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class YatzyVariant : uint8_t { STANDARD = 0, MAXI = 1 };
enum class YatzyMode : uint8_t { NORMAL = 0, ORDERED = 1 };

enum class YatzyCategory : uint8_t {
    ONES = 0, TWOS, THREES, FOURS, FIVES, SIXES,          // Upper section
    ONE_PAIR, TWO_PAIRS, THREE_PAIRS,                     // THREE_PAIRS: Maxi only
    THREE_OF_A_KIND, FOUR_OF_A_KIND, FIVE_OF_A_KIND,      // FIVE_OF_A_KIND: Maxi only
    SMALL_STRAIGHT, LARGE_STRAIGHT, FULL_STRAIGHT,        // FULL_STRAIGHT: Maxi only
    FULL_HOUSE, VILLA, TOWER,                             // VILLA (Hus), TOWER (Torn): Maxi only
    CHANCE, YATZY,                                        // YATZY = "Maxi Yatzy" in Maxi
    COUNT
};

constexpr int YATZY_CATEGORY_COUNT = (int)YatzyCategory::COUNT;
constexpr int YATZY_MAX_PLAYERS = 8;
constexpr int YATZY_MAX_DICE = 6;
constexpr int YATZY_NAME_CAP = 24;           // UTF-8 bytes incl. terminator
constexpr int YATZY_ROLLS_PER_TURN = 3;
constexpr int16_t YATZY_EMPTY = -1;

// --- Static rule helpers ----------------------------------------------------------------
namespace Yatzy {

int diceCount(YatzyVariant v);                       // 5 or 6
int bonusThreshold(YatzyVariant v);                  // 63 or 84
int bonusPoints(YatzyVariant v);                     // 50 or 100
// Categories of a variant in protocol order (also the ORDERED-mode order).
const YatzyCategory *categories(YatzyVariant v, int &count);
bool isUpper(YatzyCategory c);
bool usesCategory(YatzyVariant v, YatzyCategory c);
const char *categoryName(YatzyCategory c, YatzyVariant v);   // Swedish UTF-8
const char *categoryShortName(YatzyCategory c, YatzyVariant v);
const char *variantName(YatzyVariant v, YatzyMode m);

// Score of the dice for a category (0 if the combination is not present).
// dice: values 1..6, exactly diceCount(v) entries.
int calculateScore(YatzyVariant v, YatzyCategory c, const uint8_t *dice);

}  // namespace Yatzy

struct YatzySuggestion {
    YatzyCategory category;
    int score;
    int weight;      // Heuristic rank (higher = better choice)
};

struct YatzyPlayer {
    char name[YATZY_NAME_CAP];
    int16_t scores[YATZY_CATEGORY_COUNT];   // YATZY_EMPTY when not yet used
    uint8_t savedRolls;                     // Maxi: rolls carried over to later turns
};

// Random source returning a die value 1..6. Defaults to rand().
typedef uint8_t (*YatzyDieRng)(void *ctx);

class YatzyGame {
public:
    YatzyGame();

    // --- Setup ---
    void reset(YatzyVariant v, YatzyMode m, bool savedRolls = true);
    bool addPlayer(const char *name);               // Before start(); max 8
    bool removePlayer(int index);
    bool renamePlayer(int index, const char *name);
    void clearPlayers();
    void start();                                   // Clears scores, first player's turn
    void restartSamePlayers();                      // New game, same variant/mode/players
    void setRng(YatzyDieRng rng, void *ctx);

    // --- Turn ---
    bool canRoll() const;
    // Rolls all dice that are not held. Returns false if no roll is left or the game is over.
    bool roll();
    bool toggleHold(int die);
    bool setHold(int die, bool held);
    void releaseAll();
    // True if the current player may write `c` now (has rolled, unused, ORDERED rule).
    bool canScore(YatzyCategory c) const;
    // Writes the current dice to category c and advances to the next player.
    bool score(YatzyCategory c);
    // Score the current dice would give in c (ignores whether c is available).
    int potentialScore(YatzyCategory c) const;
    // Available categories sorted by heuristic weight (best first). Returns count.
    int getSuggestedCategories(YatzySuggestion *out, int max) const;
    // Next category in ORDERED mode for player p (COUNT if all filled).
    YatzyCategory nextOrderedCategory(int p) const;

    // --- State ---
    YatzyVariant variant() const { return variant_; }
    YatzyMode mode() const { return mode_; }
    bool savedRollsRule() const { return savedRollsRule_; }
    int playerCount() const { return playerCount_; }
    const YatzyPlayer &player(int i) const { return players_[i]; }
    int currentPlayer() const { return current_; }
    int round() const { return round_; }            // 1-based
    int totalRounds() const;
    int diceCount() const { return Yatzy::diceCount(variant_); }
    uint8_t die(int i) const { return dice_[i]; }
    const uint8_t *dice() const { return dice_; }
    bool held(int i) const { return held_[i]; }
    int rollsUsed() const { return rollsUsed_; }
    int rollsLeft() const;                          // Including saved rolls
    bool hasRolled() const { return rollsUsed_ > 0; }
    bool started() const { return started_; }
    bool finished() const { return finished_; }

    int upperSum(int p) const;
    int bonus(int p) const;                         // 0 until the threshold is reached
    int total(int p) const;
    // Players ordered by total (desc). Returns count; ties share the same place in `places`.
    int ranking(int *order, int *places) const;

private:
    void beginTurn();
    uint8_t rollDie();

    YatzyVariant variant_ = YatzyVariant::STANDARD;
    YatzyMode mode_ = YatzyMode::NORMAL;
    bool savedRollsRule_ = true;
    YatzyPlayer players_[YATZY_MAX_PLAYERS];
    int playerCount_ = 0;
    int current_ = 0;
    int round_ = 1;
    uint8_t dice_[YATZY_MAX_DICE];
    bool held_[YATZY_MAX_DICE];
    int rollsUsed_ = 0;
    int turnRollBudget_ = YATZY_ROLLS_PER_TURN;
    bool started_ = false;
    bool finished_ = false;
    YatzyDieRng rng_ = nullptr;
    void *rngCtx_ = nullptr;
};
