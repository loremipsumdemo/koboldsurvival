#pragma once
#include "components.h"

// Advances a unit's health and hunger by `hours` whole in-game hours. Each hour lowers hunger by
// one, or, if hunger is already zero, lowers health by one instead. Neither goes below zero.
inline void tickNeeds(Needs& n, int hours) {
    for (int i = 0; i < hours; ++i) {
        if (n.hunger > 0) --n.hunger;
        else if (n.hp > 0) --n.hp;
        else break;   // both empty: nothing more can change
    }
}
