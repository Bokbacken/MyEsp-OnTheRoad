#include "game_scc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t sccDefaultRng(void *) { return (uint8_t)(rand() % 6 + 1); }

static void clearTurn(uint8_t *dice, bool *held, SccRole *role)
{
    memset(dice, 0, SCC_DICE);
    memset(held, 0, SCC_DICE * sizeof(bool));
    for (int i = 0; i < SCC_DICE; i++) role[i] = SccRole::NONE;
}

void SccGame::reset(int rounds)
{
    rounds_ = rounds < 1 ? 1 : (rounds > SCC_MAX_ROUNDS ? SCC_MAX_ROUNDS : rounds);
    if (playerCount_ < 0 || playerCount_ > SCC_MAX_PLAYERS) playerCount_ = 0;
    current_ = 0;
    round_ = 1;
    clearTurn(dice_, held_, role_);
    rollsLeft_ = SCC_MAX_ROLLS;
    ship_ = captain_ = crew_ = false;
    started_ = finished_ = false;
    if (!rng_) rng_ = sccDefaultRng;
}

bool SccGame::addPlayer(const char *name)
{
    if (started_ && !finished_) return false;
    if (!name || playerCount_ >= SCC_MAX_PLAYERS) return false;
    SccPlayer &p = players_[playerCount_++];
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "%s", name);
    return true;
}

void SccGame::clearPlayers()
{
    if (!started_ || finished_) {
        playerCount_ = 0;
        started_ = finished_ = false;
    }
}

void SccGame::start()
{
    if (playerCount_ <= 0) return;
    reset(rounds_);
    for (int i = 0; i < playerCount_; i++) {
        memset(players_[i].scores, 0, sizeof(players_[i].scores));
        players_[i].scoreCount = 0;
        players_[i].total = 0;
    }
    started_ = true;
}

void SccGame::setRng(YatzyDieRng rng, void *ctx)
{
    rng_ = rng ? rng : sccDefaultRng;
    rngCtx_ = ctx;
}

int SccGame::cargoScore() const
{
    if (!crew_) return 0;
    int s = 0;
    for (int i = 0; i < SCC_DICE; i++)
        if (role_[i] == SccRole::NONE) s += dice_[i];
    return s;
}

bool SccGame::claim(uint8_t face, SccRole r)
{
    for (int i = 0; i < SCC_DICE; i++) {
        if (role_[i] == SccRole::NONE && dice_[i] == face) {
            role_[i] = r;
            held_[i] = false;
            return true;
        }
    }
    return false;
}

void SccGame::assignRoles()
{
    if (!ship_) ship_ = claim(6, SccRole::SHIP);
    if (ship_ && !captain_) captain_ = claim(5, SccRole::CAPTAIN);
    if (captain_ && !crew_) crew_ = claim(4, SccRole::CREW);
}

bool SccGame::canHold(int i) const
{
    return started_ && !finished_ && crew_ && rollsLeft_ > 0 && i >= 0 && i < SCC_DICE &&
           role_[i] == SccRole::NONE;
}

bool SccGame::toggleHold(int i)
{
    if (!canHold(i)) return false;
    held_[i] = !held_[i];
    return true;
}

bool SccGame::roll()
{
    if (!canRoll()) return false;
    for (int i = 0; i < SCC_DICE; i++) {
        if (role_[i] != SccRole::NONE || held_[i]) continue;
        dice_[i] = GameBase::clampDie(rng_(rngCtx_));
    }
    rollsLeft_--;
    assignRoles();
    if (rollsLeft_ == 0) memset(held_, 0, sizeof(held_));
    return true;
}

bool SccGame::endTurn()
{
    if (!canEnd()) return false;
    SccPlayer &p = players_[current_];
    int s = cargoScore();
    if (p.scoreCount < SCC_MAX_ROUNDS) p.scores[p.scoreCount++] = (int8_t)s;
    p.total += s;

    clearTurn(dice_, held_, role_);
    rollsLeft_ = SCC_MAX_ROLLS;
    ship_ = captain_ = crew_ = false;
    if (++current_ >= playerCount_) {
        current_ = 0;
        if (round_ >= rounds_) finished_ = true;
        else round_++;
    }
    return true;
}

int SccGame::ranking(int *order, int *places) const
{
    return GameBase::rankPlayers(playerCount_, order, places, [this](int p) { return players_[p].total; });
}
