#include "game_yatzy.h"

#include <stdlib.h>
#include <string.h>

using C = YatzyCategory;

namespace {

const YatzyCategory kStandardOrder[] = {
    C::ONES, C::TWOS, C::THREES, C::FOURS, C::FIVES, C::SIXES,
    C::ONE_PAIR, C::TWO_PAIRS, C::THREE_OF_A_KIND, C::FOUR_OF_A_KIND,
    C::SMALL_STRAIGHT, C::LARGE_STRAIGHT, C::FULL_HOUSE, C::CHANCE, C::YATZY,
};

const YatzyCategory kMaxiOrder[] = {
    C::ONES, C::TWOS, C::THREES, C::FOURS, C::FIVES, C::SIXES,
    C::ONE_PAIR, C::TWO_PAIRS, C::THREE_PAIRS, C::THREE_OF_A_KIND, C::FOUR_OF_A_KIND, C::FIVE_OF_A_KIND,
    C::SMALL_STRAIGHT, C::LARGE_STRAIGHT, C::FULL_STRAIGHT,
    C::FULL_HOUSE, C::VILLA, C::TOWER, C::CHANCE, C::YATZY,
};

struct Names {
    const char *full;
    const char *shortName;
};

// Swedish protocol names (UTF-8).
const Names kNames[YATZY_CATEGORY_COUNT] = {
    {"Ettor", "1:or"}, {"Tvåor", "2:or"}, {"Treor", "3:or"},
    {"Fyror", "4:or"}, {"Femmor", "5:or"}, {"Sexor", "6:or"},
    {"Ett par", "1 par"}, {"Två par", "2 par"}, {"Tre par", "3 par"},
    {"Tretal", "Tretal"}, {"Fyrtal", "Fyrtal"}, {"Femtal", "Femtal"},
    {"Liten stege", "L.stege"}, {"Stor stege", "S.stege"}, {"Full stege", "F.stege"},
    {"Kåk", "Kåk"}, {"Hus", "Hus"}, {"Torn", "Torn"},
    {"Chans", "Chans"}, {"Yatzy", "Yatzy"},
};

// Rough expected value of each category; used to judge the cost of a choice.
const int8_t kExpected[YATZY_CATEGORY_COUNT] = {
    2, 5, 8, 11, 14, 17,     // upper
    8, 15, 20,               // pairs
    12, 10, 8,               // n of a kind
    8, 8, 7,                 // straights
    15, 15, 13,              // full house, villa, tower
    28, 10,                  // chance, yatzy
};

void countDice(const uint8_t *dice, int n, int counts[7])
{
    for (int i = 0; i < 7; i++) counts[i] = 0;
    for (int i = 0; i < n; i++) {
        if (dice[i] >= 1 && dice[i] <= 6) counts[dice[i]]++;
    }
}

int highestWithCount(const int counts[7], int need, int exclude = 0)
{
    for (int v = 6; v >= 1; v--) {
        if (v != exclude && counts[v] >= need) return v;
    }
    return 0;
}

bool hasRun(const int counts[7], int from, int to)
{
    for (int v = from; v <= to; v++) {
        if (counts[v] < 1) return false;
    }
    return true;
}

// Best "a x big + b x small" with a != b, e.g. full house (3,2), tower (4,2), villa (3,3).
int bestCombo(const int counts[7], int big, int small)
{
    int best = 0;
    for (int a = 1; a <= 6; a++) {
        if (counts[a] < big) continue;
        for (int b = 1; b <= 6; b++) {
            if (b == a || counts[b] < small) continue;
            int s = a * big + b * small;
            if (s > best) best = s;
        }
    }
    return best;
}

uint8_t defaultRng(void *) { return (uint8_t)(rand() % 6 + 1); }

}  // namespace

// ---------------------------------------------------------------------------------------
namespace Yatzy {

int diceCount(YatzyVariant v) { return v == YatzyVariant::MAXI ? 6 : 5; }
int bonusThreshold(YatzyVariant v) { return v == YatzyVariant::MAXI ? 84 : 63; }
int bonusPoints(YatzyVariant v) { return v == YatzyVariant::MAXI ? 100 : 50; }

const YatzyCategory *categories(YatzyVariant v, int &count)
{
    if (v == YatzyVariant::MAXI) {
        count = (int)(sizeof(kMaxiOrder) / sizeof(kMaxiOrder[0]));
        return kMaxiOrder;
    }
    count = (int)(sizeof(kStandardOrder) / sizeof(kStandardOrder[0]));
    return kStandardOrder;
}

bool isUpper(YatzyCategory c) { return (int)c <= (int)C::SIXES; }

bool usesCategory(YatzyVariant v, YatzyCategory c)
{
    int n;
    const YatzyCategory *list = categories(v, n);
    for (int i = 0; i < n; i++) {
        if (list[i] == c) return true;
    }
    return false;
}

const char *categoryName(YatzyCategory c, YatzyVariant v)
{
    if (c == C::YATZY && v == YatzyVariant::MAXI) return "Maxi Yatzy";
    return (int)c < YATZY_CATEGORY_COUNT ? kNames[(int)c].full : "?";
}

const char *categoryShortName(YatzyCategory c, YatzyVariant v)
{
    if (c == C::YATZY && v == YatzyVariant::MAXI) return "Maxi";
    return (int)c < YATZY_CATEGORY_COUNT ? kNames[(int)c].shortName : "?";
}

const char *variantName(YatzyVariant v, YatzyMode m)
{
    if (v == YatzyVariant::MAXI) return m == YatzyMode::ORDERED ? "Maxi-Yatzy i ordning" : "Maxi-Yatzy";
    return m == YatzyMode::ORDERED ? "Yatzy i ordning" : "Yatzy";
}

int calculateScore(YatzyVariant v, YatzyCategory c, const uint8_t *dice)
{
    if (!dice || !usesCategory(v, c)) return 0;
    const int n = diceCount(v);
    int counts[7];
    countDice(dice, n, counts);
    int sum = 0;
    for (int i = 0; i < n; i++) sum += dice[i];

    switch (c) {
    case C::ONES: case C::TWOS: case C::THREES:
    case C::FOURS: case C::FIVES: case C::SIXES: {
        int face = (int)c + 1;
        return counts[face] * face;
    }
    case C::ONE_PAIR: return 2 * highestWithCount(counts, 2);
    case C::TWO_PAIRS: {
        int a = highestWithCount(counts, 2);
        int b = a ? highestWithCount(counts, 2, a) : 0;
        return (a && b) ? 2 * (a + b) : 0;
    }
    case C::THREE_PAIRS: {
        int pairs = 0, s = 0;
        for (int f = 1; f <= 6; f++) {
            if (counts[f] >= 2) { pairs++; s += 2 * f; }
        }
        return pairs >= 3 ? s : 0;
    }
    case C::THREE_OF_A_KIND: return 3 * highestWithCount(counts, 3);
    case C::FOUR_OF_A_KIND: return 4 * highestWithCount(counts, 4);
    case C::FIVE_OF_A_KIND: return 5 * highestWithCount(counts, 5);
    case C::SMALL_STRAIGHT: return hasRun(counts, 1, 5) ? 15 : 0;
    case C::LARGE_STRAIGHT: return hasRun(counts, 2, 6) ? 20 : 0;
    case C::FULL_STRAIGHT: return hasRun(counts, 1, 6) ? 21 : 0;
    case C::FULL_HOUSE: return bestCombo(counts, 3, 2);
    case C::VILLA: return bestCombo(counts, 3, 3);
    case C::TOWER: return bestCombo(counts, 4, 2);
    case C::CHANCE: return sum;
    case C::YATZY: return highestWithCount(counts, n) ? (v == YatzyVariant::MAXI ? 100 : 50) : 0;
    default: return 0;
    }
}

}  // namespace Yatzy

// ---------------------------------------------------------------------------------------
YatzyGame::YatzyGame()
{
    memset(players_, 0, sizeof(players_));
    memset(dice_, 0, sizeof(dice_));
    memset(held_, 0, sizeof(held_));
    rng_ = defaultRng;
}

void YatzyGame::reset(YatzyVariant v, YatzyMode m, bool savedRolls)
{
    variant_ = v;
    mode_ = m;
    savedRollsRule_ = savedRolls;
    started_ = finished_ = false;
    current_ = 0;
    round_ = 1;
    rollsUsed_ = 0;
    for (int i = 0; i < playerCount_; i++) {
        for (auto &s : players_[i].scores) s = YATZY_EMPTY;
        players_[i].savedRolls = 0;
    }
}

bool YatzyGame::addPlayer(const char *name)
{
    if (started_ || playerCount_ >= YATZY_MAX_PLAYERS) return false;
    YatzyPlayer &p = players_[playerCount_];
    memset(&p, 0, sizeof(p));
    for (auto &s : p.scores) s = YATZY_EMPTY;
    playerCount_++;
    if (!renamePlayer(playerCount_ - 1, name)) {
        playerCount_--;
        return false;
    }
    return true;
}

bool YatzyGame::removePlayer(int index)
{
    if (started_ || index < 0 || index >= playerCount_) return false;
    for (int i = index; i < playerCount_ - 1; i++) players_[i] = players_[i + 1];
    playerCount_--;
    return true;
}

bool YatzyGame::renamePlayer(int index, const char *name)
{
    if (index < 0 || index >= playerCount_ || !name || !name[0]) return false;
    // Copy whole UTF-8 sequences only so truncation never splits a character.
    char *dst = players_[index].name;
    size_t used = 0;
    for (const char *p = name; *p;) {
        uint8_t lead = (uint8_t)*p;
        size_t len = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
        if (used + len >= (size_t)YATZY_NAME_CAP) break;
        size_t avail = 0;
        while (avail < len && p[avail]) avail++;
        if (avail < len) break;
        memcpy(dst + used, p, len);
        used += len;
        p += len;
    }
    dst[used] = '\0';
    return used > 0;
}

void YatzyGame::clearPlayers()
{
    if (!started_ || finished_) {
        playerCount_ = 0;
        started_ = finished_ = false;
    }
}

void YatzyGame::start()
{
    if (playerCount_ == 0) return;
    reset(variant_, mode_, savedRollsRule_);
    started_ = true;
    beginTurn();
}

void YatzyGame::restartSamePlayers()
{
    started_ = false;
    start();
}

void YatzyGame::setRng(YatzyDieRng rng, void *ctx)
{
    rng_ = rng ? rng : defaultRng;
    rngCtx_ = ctx;
}

void YatzyGame::beginTurn()
{
    rollsUsed_ = 0;
    memset(held_, 0, sizeof(held_));
    memset(dice_, 0, sizeof(dice_));
    YatzyPlayer &p = players_[current_];
    turnRollBudget_ = YATZY_ROLLS_PER_TURN;
    if (variant_ == YatzyVariant::MAXI && savedRollsRule_) {
        turnRollBudget_ += p.savedRolls;
        p.savedRolls = 0;
    }
}

uint8_t YatzyGame::rollDie()
{
    uint8_t v = rng_(rngCtx_);
    return (v >= 1 && v <= 6) ? v : (uint8_t)(v % 6 + 1);
}

int YatzyGame::rollsLeft() const { return started_ && !finished_ ? turnRollBudget_ - rollsUsed_ : 0; }

bool YatzyGame::canRoll() const
{
    if (!started_ || finished_ || rollsLeft() <= 0) return false;
    if (rollsUsed_ == 0) return true;
    for (int i = 0; i < diceCount(); i++) {
        if (!held_[i]) return true;
    }
    return false;   // Everything is held: nothing to roll
}

bool YatzyGame::roll()
{
    if (!canRoll()) return false;
    for (int i = 0; i < diceCount(); i++) {
        if (rollsUsed_ == 0 || !held_[i]) dice_[i] = rollDie();
    }
    if (rollsUsed_ == 0) memset(held_, 0, sizeof(held_));
    rollsUsed_++;
    return true;
}

bool YatzyGame::toggleHold(int die)
{
    if (die < 0 || die >= diceCount()) return false;
    return setHold(die, !held_[die]);
}

bool YatzyGame::setHold(int die, bool held)
{
    if (!started_ || finished_ || rollsUsed_ == 0 || die < 0 || die >= diceCount()) return false;
    held_[die] = held;
    return true;
}

void YatzyGame::releaseAll() { memset(held_, 0, sizeof(held_)); }

YatzyCategory YatzyGame::nextOrderedCategory(int p) const
{
    int n;
    const YatzyCategory *list = Yatzy::categories(variant_, n);
    for (int i = 0; i < n; i++) {
        if (players_[p].scores[(int)list[i]] == YATZY_EMPTY) return list[i];
    }
    return C::COUNT;
}

bool YatzyGame::canScore(YatzyCategory c) const
{
    if (!started_ || finished_ || rollsUsed_ == 0) return false;
    if ((int)c >= YATZY_CATEGORY_COUNT || !Yatzy::usesCategory(variant_, c)) return false;
    if (players_[current_].scores[(int)c] != YATZY_EMPTY) return false;
    if (mode_ == YatzyMode::ORDERED && c != nextOrderedCategory(current_)) return false;
    return true;
}

int YatzyGame::potentialScore(YatzyCategory c) const { return Yatzy::calculateScore(variant_, c, dice_); }

bool YatzyGame::score(YatzyCategory c)
{
    if (!canScore(c)) return false;
    YatzyPlayer &p = players_[current_];
    p.scores[(int)c] = (int16_t)potentialScore(c);
    if (variant_ == YatzyVariant::MAXI && savedRollsRule_) {
        p.savedRolls = (uint8_t)(turnRollBudget_ - rollsUsed_);
    }
    current_++;
    if (current_ >= playerCount_) {
        current_ = 0;
        round_++;
    }
    if (round_ > totalRounds()) {
        finished_ = true;
        rollsUsed_ = 0;
        return true;
    }
    beginTurn();
    return true;
}

int YatzyGame::getSuggestedCategories(YatzySuggestion *out, int max) const
{
    if (!out || max <= 0 || !hasRolled() || finished_) return 0;
    int n, count = 0;
    const YatzyCategory *list = Yatzy::categories(variant_, n);
    const int upperPar = variant_ == YatzyVariant::MAXI ? 4 : 3;   // 84/21 resp. 63/21
    for (int i = 0; i < n; i++) {
        YatzyCategory c = list[i];
        if (!canScore(c)) continue;
        int s = potentialScore(c);
        int w;
        if (Yatzy::isUpper(c)) {
            int face = (int)c + 1;
            // Reward reaching "par" for the bonus, penalise falling short.
            w = s * 10 + (s - upperPar * face) * 15;
        } else {
            w = s * 10 + (s - kExpected[(int)c]) * 10;
        }
        if (s == 0) w = -kExpected[(int)c] * 10;   // Striking: prefer the cheapest box
        // Keep the best `max` entries, sorted (insertion sort; lists are tiny).
        int j;
        if (count < max) j = count++;
        else if (w > out[max - 1].weight) j = max - 1;
        else continue;
        while (j > 0 && out[j - 1].weight < w) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = {c, s, w};
    }
    return count;
}

int YatzyGame::totalRounds() const
{
    int n;
    Yatzy::categories(variant_, n);
    return n;
}

int YatzyGame::upperSum(int p) const
{
    int s = 0;
    for (int c = 0; c <= (int)C::SIXES; c++) {
        if (players_[p].scores[c] > 0) s += players_[p].scores[c];
    }
    return s;
}

int YatzyGame::bonus(int p) const
{
    return upperSum(p) >= Yatzy::bonusThreshold(variant_) ? Yatzy::bonusPoints(variant_) : 0;
}

int YatzyGame::total(int p) const
{
    int s = 0;
    for (int c = 0; c < YATZY_CATEGORY_COUNT; c++) {
        if (players_[p].scores[c] > 0) s += players_[p].scores[c];
    }
    return s + bonus(p);
}

int YatzyGame::ranking(int *order, int *places) const
{
    for (int i = 0; i < playerCount_; i++) order[i] = i;
    // Stable insertion sort by total, descending.
    for (int i = 1; i < playerCount_; i++) {
        int v = order[i], j = i;
        while (j > 0 && total(order[j - 1]) < total(v)) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    for (int i = 0; i < playerCount_; i++) {
        places[i] = (i > 0 && total(order[i]) == total(order[i - 1])) ? places[i - 1] : i + 1;
    }
    return playerCount_;
}
