// game_farkle.h - Farkle: six dice, push-your-luck with scoring combinations. Pure C++ (host tested).
//
// Scoring (scoreSet): single 1 = 100, single 5 = 50, three of a kind = face x 100 (ones 1000),
// four = face x 200, five = face x 300, six = face x 400, straight 1-6 = 1500, three pairs = 750.
//
// Turn flow:
//   roll()       - first roll of the turn throws all six dice.
//   toggleHold() - set aside dice from the latest roll. Every held die must score (heldValid()).
//   roll()       - the held dice are locked, their score goes to the turn total and the rest is
//                  re-thrown. If all six dice score ("hot dice") all six are thrown again.
//   bank()       - turn total + held score goes to the player's total. A player must score at
//                  least FARKLE_OPEN_THRESHOLD in one turn to get on the board.
// A roll without any scoring dice is a Farkle: the turn total is lost. The dice stay visible
// (hasFarkled()) until the UI calls passTurn().
// When a player reaches FARKLE_WIN_SCORE every other player gets one final turn; highest total wins.
#pragma once

#include <stdint.h>

#include "game_base.h"

constexpr int FARKLE_MAX_PLAYERS = 8;
constexpr int FARKLE_DICE = 6;
constexpr int FARKLE_OPEN_THRESHOLD = 500;
constexpr int FARKLE_WIN_SCORE = 10000;

struct FarklePlayer {
    char name[GAME_NAME_CAP];
    int total;
    bool opened;
};

// Trivially constructible (no member initialisers): lives in GameLive's snapshot union.
// Call reset() before use.
class FarkleGame {
public:
    void reset();                            // Keeps the roster
    bool addPlayer(const char *name);        // Before start(); max 8
    void clearPlayers();
    void start();
    void setRng(YatzyDieRng rng, void *ctx);

    int playerCount() const { return playerCount_; }
    const FarklePlayer &player(int i) const { return players_[i]; }
    int currentPlayer() const { return current_; }
    int round() const { return round_; }
    bool started() const { return started_; }
    bool finished() const { return finished_; }
    bool finalRound() const { return finalRound_; }   // Someone reached the target
    const uint8_t *dice() const { return dice_; }
    bool held(int i) const { return held_[i] || locked_[i]; }   // Selected now or set aside earlier
    bool locked(int i) const { return locked_[i]; }             // Set aside on an earlier roll
    bool canHold(int i) const;
    bool toggleHold(int i);
    int turnTotal() const { return turnTotal_; }      // Locked points this turn (excl. held)
    int heldScore() const;                            // Score of the currently held dice (0 = invalid)
    bool heldValid() const;                           // >= 1 held die and every held die scores
    bool hasFarkled() const { return farkled_; }
    bool hotDice() const;                             // All six dice score: roll all six again
    bool hasRolled() const { return rolled_; }
    bool canRoll() const;
    bool canBank() const;

    bool roll();
    bool bank();
    bool passTurn();                                  // After a Farkle: next player
    int ranking(int *order, int *places) const;

    // Best score of `n` dice (n <= 6). `used` (optional) = number of dice that contribute.
    static int scoreSet(const uint8_t *dice, int n, int *used = nullptr);

private:
    void nextPlayer();
    int heldValues(uint8_t *out) const;

    FarklePlayer players_[FARKLE_MAX_PLAYERS];
    int playerCount_;
    int current_;
    int round_;
    int finalTrigger_;      // Player who reached the target (-1 = none)
    uint8_t dice_[FARKLE_DICE];
    bool held_[FARKLE_DICE];
    bool locked_[FARKLE_DICE];
    int turnTotal_;
    bool rolled_;
    bool farkled_;
    bool finalRound_;
    bool started_;
    bool finished_;
    YatzyDieRng rng_;
    void *rngCtx_;
};
