#pragma once
#include "components.h"

// Advances a unit's health and hunger by `minutes` of in-game time. Each whole minute that passes
// lowers hunger by one, or, if hunger is already zero, lowers health by one instead. Neither goes
// below zero. Partial minutes carry over to the next call.
inline void tickNeeds(Needs& n, float minutes) {
    if (minutes <= 0.0f) return;
    n.hungerProgress += minutes;
    while (n.hungerProgress >= 1.0f) {
        n.hungerProgress -= 1.0f;
        if (n.hunger > 0) --n.hunger;
        else if (n.hp > 0) --n.hp;
    }
}
