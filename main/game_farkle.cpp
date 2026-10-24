#include "game_farkle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t farkleDefaultRng(void *) { return (uint8_t)(rand() % 6 + 1); }

int FarkleGame::scoreSet(const uint8_t *dice, int n, int *used)
{
    int c[7] = {0};
    int valid = 0;
    for (int i = 0; i < n && i < FARKLE_DICE; i++) {
        if (dice[i] >= 1 && dice[i] <= 6) {
            c[dice[i]]++;
            valid++;
        }
    }

    // Kinds + single ones/fives
    int score = 0, u = 0;
    for (int f = 1; f <= 6; f++) {
        int k = c[f];
        if (k >= 3) {
            score += (f == 1 ? 1000 : f * 100) * (k - 2);
            u += k;
        } else if (f == 1) {
            score += 100 * k;
            u += k;
        } else if (f == 5) {
            score += 50 * k;
            u += k;
        }
    }

    if (valid == 6) {
        bool straight = true;
        int pairs = 0;
        for (int f = 1; f <= 6; f++) {
            if (c[f] != 1) straight = false;
            if (c[f] == 2) pairs++;
        }
        if (straight && 1500 > score) { score = 1500; u = 6; }
        if (pairs == 3 && 750 > score) { score = 750; u = 6; }
    }
    if (used) *used = u;
    return score;
}

void FarkleGame::reset()
{
    if (playerCount_ < 0 || playerCount_ > FARKLE_MAX_PLAYERS) playerCount_ = 0;
    current_ = 0;
    round_ = 1;
    finalTrigger_ = -1;
    memset(dice_, 0, sizeof(dice_));
    memset(held_, 0, sizeof(held_));
    memset(locked_, 0, sizeof(locked_));
    turnTotal_ = 0;
    rolled_ = farkled_ = finalRound_ = false;
    started_ = finished_ = false;
    if (!rng_) rng_ = farkleDefaultRng;
}

bool FarkleGame::addPlayer(const char *name)
{
    if (started_ && !finished_) return false;
    if (!name || playerCount_ >= FARKLE_MAX_PLAYERS) return false;
    FarklePlayer &p = players_[playerCount_++];
    snprintf(p.name, sizeof(p.name), "%s", name);
    p.total = 0;
    p.opened = false;
    return true;
}

void FarkleGame::clearPlayers()
{
    if (!started_ || finished_) {
        playerCount_ = 0;
        started_ = finished_ = false;
    }
}

void FarkleGame::start()
{
    if (playerCount_ <= 0) return;
    reset();
    for (int i = 0; i < playerCount_; i++) {
        players_[i].total = 0;
        players_[i].opened = false;
    }
    started_ = true;
}

void FarkleGame::setRng(YatzyDieRng rng, void *ctx)
{
    rng_ = rng ? rng : farkleDefaultRng;
    rngCtx_ = ctx;
}

int FarkleGame::heldValues(uint8_t *out) const
{
    int n = 0;
    for (int i = 0; i < FARKLE_DICE; i++)
        if (held_[i] && !locked_[i]) out[n++] = dice_[i];
    return n;
}

int FarkleGame::heldScore() const
{
    uint8_t v[FARKLE_DICE];
    int n = heldValues(v), used = 0;
    if (n == 0) return 0;
    int s = scoreSet(v, n, &used);
    return used == n ? s : 0;
}

bool FarkleGame::heldValid() const { return heldScore() > 0; }

bool FarkleGame::hotDice() const
{
    if (!rolled_ || farkled_ || !heldValid()) return false;
    for (int i = 0; i < FARKLE_DICE; i++)
        if (!held_[i] && !locked_[i]) return false;
    return true;
}

bool FarkleGame::canHold(int i) const
{
    return started_ && !finished_ && rolled_ && !farkled_ && i >= 0 && i < FARKLE_DICE && !locked_[i];
}

bool FarkleGame::toggleHold(int i)
{
    if (!canHold(i)) return false;
    held_[i] = !held_[i];
    return true;
}

bool FarkleGame::canRoll() const
{
    if (!started_ || finished_ || farkled_) return false;
    return !rolled_ || heldValid();
}

bool FarkleGame::canBank() const
{
    if (!started_ || finished_ || farkled_ || !rolled_ || !heldValid()) return false;
    return players_[current_].opened || turnTotal_ + heldScore() >= FARKLE_OPEN_THRESHOLD;
}

bool FarkleGame::roll()
{
    if (!canRoll()) return false;
    if (rolled_) {
        turnTotal_ += heldScore();
        bool all = true;
        for (int i = 0; i < FARKLE_DICE; i++) {
            if (held_[i]) locked_[i] = true;
            held_[i] = false;
            if (!locked_[i]) all = false;
        }
        if (all) memset(locked_, 0, sizeof(locked_));   // Hot dice: all six again
    }
    uint8_t freeVals[FARKLE_DICE];
    int nFree = 0;
    for (int i = 0; i < FARKLE_DICE; i++) {
        if (locked_[i]) continue;
        dice_[i] = GameBase::clampDie(rng_(rngCtx_));
        freeVals[nFree++] = dice_[i];
    }
    rolled_ = true;
    if (scoreSet(freeVals, nFree) == 0) {
        farkled_ = true;
        turnTotal_ = 0;
    }
    return true;
}

void FarkleGame::nextPlayer()
{
    memset(held_, 0, sizeof(held_));
    memset(locked_, 0, sizeof(locked_));
    turnTotal_ = 0;
    rolled_ = farkled_ = false;
    if (++current_ >= playerCount_) {
        current_ = 0;
        round_++;
    }
    if (finalRound_ && current_ == finalTrigger_) finished_ = true;
}

bool FarkleGame::bank()
{
    if (!canBank()) return false;
    FarklePlayer &p = players_[current_];
    p.total += turnTotal_ + heldScore();
    p.opened = true;
    if (!finalRound_ && p.total >= FARKLE_WIN_SCORE) {
        finalRound_ = true;
        finalTrigger_ = current_;
        if (playerCount_ == 1) {
            finished_ = true;
            memset(held_, 0, sizeof(held_));
            turnTotal_ = 0;
            return true;
        }
    }
    nextPlayer();
    return true;
}

bool FarkleGame::passTurn()
{
    if (!started_ || finished_ || !farkled_) return false;
    nextPlayer();
    return true;
}

int FarkleGame::ranking(int *order, int *places) const
{
    return GameBase::rankPlayers(playerCount_, order, places, [this](int p) { return players_[p].total; });
}
