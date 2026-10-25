#pragma once
#include "components.h"

// Advances a unit's health and hunger by `hours` whole in-game hours. Each hour lowers hunger by
// one, or, if hunger is already zero, lowers health by one instead. Neither goes below zero.
inline void tickNeeds(Needs& n, int hours) {
    for (int i = 0; i < hours; ++i) {
        if (n.hunger > 0) {
            --n.hunger;
            n.changed |= NEEDS_HUNGER_CHANGED;
        } else if (n.hp > 0) {
            --n.hp;
            n.changed |= NEEDS_HP_CHANGED;
        } else {
            break;   // both empty: nothing more can change
        }
    }
}

// In-game seconds for one point of regained health: a tenth of an in-game hour
constexpr double REGEN_GAME_SECONDS = 360.0;

// Gives a unit back health for the in-game time that passed. Only a unit that is standing, below its
// maximum, out of combat, and with hunger over half regains anything. When it can't, its progress toward
// the next point is kept, so a fight pauses regen rather than restarting it. Full health clears it.
inline void regenHealth(Needs& n, bool inCombat, double gameSeconds) {
    if (n.hp >= n.maxHp) {
        n.healSeconds = 0.0;
        return;
    }
    const bool eligible = !inCombat && n.hp > 0 && n.hunger * 2 > n.maxHunger;
    if (!eligible || gameSeconds <= 0.0) return;
    n.healSeconds += gameSeconds;
    while (n.healSeconds >= REGEN_GAME_SECONDS && n.hp < n.maxHp) {
        ++n.hp;
        n.changed |= NEEDS_HP_CHANGED;
        n.healSeconds -= REGEN_GAME_SECONDS;
    }
    if (n.hp >= n.maxHp) n.healSeconds = 0.0;
}
