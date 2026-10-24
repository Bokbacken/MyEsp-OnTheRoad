// game_pig.h - Pig ("Gris"): push-your-luck with one or two dice. Pure C++ (host tested).
//
// ONE_DIE : roll one die and add it to the turn total. A 1 busts (turn total lost, turn over).
// TWO_DICE: roll two dice and add the sum. Any single 1 busts; double ones wipe the player's
//           whole score (total = 0) as well.
// Bank at any time after the first roll: the turn total goes to the player's score.
// First player to reach PIG_WIN_SCORE when banking wins immediately. No holding of dice.
//
// After a bust the dice stay visible (hasBusted() == true) until the UI calls passTurn(),
// so both the console and the web can show what happened before the next player starts.
#pragma once

#include <stdint.h>

#include "game_base.h"

constexpr int PIG_MAX_PLAYERS = 8;
constexpr int PIG_NAME_CAP = GAME_NAME_CAP;
constexpr int PIG_WIN_SCORE = 100;

struct PigPlayer {
    char name[PIG_NAME_CAP];
    int total;
};

// Trivially constructible (no member initialisers): lives in GameLive's snapshot union.
// Call reset() before use.
class PigGame {
public:
    void reset(PigVariant v);                // Keeps the roster
    bool addPlayer(const char *name);        // Before start(); max 8
    void clearPlayers();
    void start();                            // Clears scores, first player's turn
    void setRng(YatzyDieRng rng, void *ctx);

    int playerCount() const { return playerCount_; }
    const PigPlayer &player(int i) const { return players_[i]; }
    int total(int p) const { return players_[p].total; }
    int currentPlayer() const { return current_; }
    int round() const { return round_; }     // 1-based
    bool started() const { return started_; }
    bool finished() const { return finished_; }
    int winner() const { return finished_ ? winner_ : -1; }
    PigVariant variant() const { return variant_; }
    int numDice() const { return variant_ == PigVariant::TWO_DICE ? 2 : 1; }
    const uint8_t *dice() const { return dice_; }
    int turnTotal() const { return turnTotal_; }
    int rollsThisTurn() const { return rolls_; }
    bool hasBusted() const { return busted_; }
    bool lostAll() const { return busted_ && lostAll_; }   // Double ones (TWO_DICE)
    bool hasRolled() const { return rolls_ > 0; }
    bool canRoll() const { return started_ && !finished_ && !busted_; }
    bool canBank() const { return started_ && !finished_ && !busted_ && rolls_ > 0 && turnTotal_ > 0; }

    bool roll();       // false if rolling is not allowed now
    bool bank();       // false if banking is not allowed now
    bool passTurn();   // After a bust: next player. false if not busted.
    int ranking(int *order, int *places) const;

private:
    void nextPlayer();

    PigVariant variant_;
    PigPlayer players_[PIG_MAX_PLAYERS];
    int playerCount_;
    int current_;
    int round_;
    int winner_;
    uint8_t dice_[2];
    int turnTotal_;
    int rolls_;
    bool busted_;
    bool lostAll_;
    bool started_;
    bool finished_;
    YatzyDieRng rng_;
    void *rngCtx_;
};
