// game_scc.h - Ship, Captain and Crew ("Skepp, kapten och besattning"). Pure C++ (host tested).
//
// Five dice, up to three rolls per turn. A 6 (ship), a 5 (captain) and a 4 (crew) must be
// collected in that order; they are set aside automatically as soon as they appear (several in
// the same roll are fine as long as the order can be satisfied). Once all three are aboard, the
// two remaining dice are the cargo: their sum is the score. The player may keep re-rolling the
// cargo (holding none, one or both) while rolls remain, or end the turn (endTurn()).
// Without ship + captain + crew after the last roll the turn scores 0.
// The engine never ends a turn by itself: the UI calls endTurn() (automatically after the third
// roll, or when the player presses "Spara"). Most points after N rounds wins.
#pragma once

#include <stdint.h>

#include "game_base.h"

constexpr int SCC_DICE = 5;
constexpr int SCC_MAX_ROLLS = 3;
constexpr int SCC_DEFAULT_ROUNDS = 3;
constexpr int SCC_MAX_ROUNDS = 10;
constexpr int SCC_MAX_PLAYERS = 8;

struct SccPlayer {
    char name[GAME_NAME_CAP];
    int8_t scores[SCC_MAX_ROUNDS];   // Score per finished round (0..12)
    int scoreCount;
    int total;
};

enum class SccRole : uint8_t { NONE = 0, SHIP = 1, CAPTAIN = 2, CREW = 3 };

// Trivially constructible (no member initialisers): lives in GameLive's snapshot union.
// Call reset() before use.
class SccGame {
public:
    void reset(int rounds = SCC_DEFAULT_ROUNDS);   // Keeps the roster
    bool addPlayer(const char *name);
    void clearPlayers();
    void start();
    void setRng(YatzyDieRng rng, void *ctx);

    int playerCount() const { return playerCount_; }
    const SccPlayer &player(int i) const { return players_[i]; }
    int currentPlayer() const { return current_; }
    int round() const { return round_; }            // 1-based
    int totalRounds() const { return rounds_; }
    bool started() const { return started_; }
    bool finished() const { return finished_; }
    const uint8_t *dice() const { return dice_; }
    bool held(int i) const { return held_[i] || role_[i] != SccRole::NONE; }
    SccRole role(int i) const { return role_[i]; }
    bool hasShip() const { return ship_; }
    bool hasCaptain() const { return captain_; }
    bool hasCrew() const { return crew_; }
    int cargoScore() const;                         // 0 until ship + captain + crew
    int rollsLeft() const { return rollsLeft_; }
    bool hasRolled() const { return rollsLeft_ < SCC_MAX_ROLLS; }
    bool canRoll() const { return started_ && !finished_ && rollsLeft_ > 0; }
    bool canHold(int i) const;                      // Only cargo dice, only with rolls left
    bool canEnd() const { return started_ && !finished_ && hasRolled() && (rollsLeft_ == 0 || crew_); }

    bool roll();
    bool toggleHold(int i);
    bool endTurn();                                 // Records cargoScore() and moves on
    int ranking(int *order, int *places) const;

private:
    void assignRoles();
    bool claim(uint8_t face, SccRole r);

    SccPlayer players_[SCC_MAX_PLAYERS];
    int playerCount_;
    int current_;
    int round_;
    int rounds_;
    uint8_t dice_[SCC_DICE];
    bool held_[SCC_DICE];
    SccRole role_[SCC_DICE];
    int rollsLeft_;
    bool ship_, captain_, crew_;
    bool started_;
    bool finished_;
    YatzyDieRng rng_;
    void *rngCtx_;
};
