#include "game_pig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t pigDefaultRng(void *) { return (uint8_t)(rand() % 6 + 1); }

void PigGame::reset(PigVariant v)
{
    variant_ = v;
    if (playerCount_ < 0 || playerCount_ > PIG_MAX_PLAYERS) playerCount_ = 0;
    current_ = 0;
    round_ = 1;
    winner_ = -1;
    memset(dice_, 0, sizeof(dice_));
    turnTotal_ = 0;
    rolls_ = 0;
    busted_ = lostAll_ = false;
    started_ = finished_ = false;
    if (!rng_) rng_ = pigDefaultRng;
}

bool PigGame::addPlayer(const char *name)
{
    if (started_ && !finished_) return false;
    if (!name || playerCount_ >= PIG_MAX_PLAYERS) return false;
    PigPlayer &p = players_[playerCount_++];
    snprintf(p.name, sizeof(p.name), "%s", name);
    p.total = 0;
    return true;
}

void PigGame::clearPlayers()
{
    if (!started_ || finished_) {
        playerCount_ = 0;
        started_ = finished_ = false;
    }
}

void PigGame::start()
{
    if (playerCount_ <= 0) return;
    reset(variant_);
    for (int i = 0; i < playerCount_; i++) players_[i].total = 0;
    started_ = true;
}

void PigGame::setRng(YatzyDieRng rng, void *ctx)
{
    rng_ = rng ? rng : pigDefaultRng;
    rngCtx_ = ctx;
}

void PigGame::nextPlayer()
{
    turnTotal_ = 0;
    rolls_ = 0;
    busted_ = lostAll_ = false;
    memset(dice_, 0, sizeof(dice_));
    if (++current_ >= playerCount_) {
        current_ = 0;
        round_++;
    }
}

bool PigGame::roll()
{
    if (!canRoll()) return false;
    const int n = numDice();
    int ones = 0, sum = 0;
    for (int i = 0; i < n; i++) {
        dice_[i] = GameBase::clampDie(rng_(rngCtx_));
        if (dice_[i] == 1) ones++;
        sum += dice_[i];
    }
    if (n == 1) dice_[1] = 0;
    rolls_++;
    if (ones > 0) {
        busted_ = true;
        turnTotal_ = 0;
        if (n == 2 && ones == 2) {   // Double ones: the whole score is lost
            lostAll_ = true;
            players_[current_].total = 0;
        }
        return true;
    }
    turnTotal_ += sum;
    return true;
}

bool PigGame::bank()
{
    if (!canBank()) return false;
    PigPlayer &p = players_[current_];
    p.total += turnTotal_;
    if (p.total >= PIG_WIN_SCORE) {
        finished_ = true;
        winner_ = current_;
        turnTotal_ = 0;
        return true;
    }
    nextPlayer();
    return true;
}

bool PigGame::passTurn()
{
    if (!started_ || finished_ || !busted_) return false;
    nextPlayer();
    return true;
}

int PigGame::ranking(int *order, int *places) const
{
    return GameBase::rankPlayers(playerCount_, order, places, [this](int p) { return players_[p].total; });
}
