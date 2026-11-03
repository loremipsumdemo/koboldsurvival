#pragma once
#include <cmath>
#include "components.h"
#include "props.h"
#include "stats.h"

// Rested and Comfort, per in-game hour. Rested falls while awake and out of bed, and rises lying in a bed or bedroll
// (asleep or out cold). Comfort rises in any occupiable prop without a lock (a bed, bedroll, chair, bench, stool), more
// in a bed, and falls anywhere else.
constexpr double RESTED_LOSS_PER_HOUR = 4.0;
constexpr double COMFORT_LOSS_PER_HOUR = 2.0;
inline double restedGainPerHour(int propKind) {
    if (propKind == PROP_BED) return 25.0;
    if (propKind == PROP_BEDROLL) return 15.0;
    return 0.0;
}
inline double comfortGainPerHour(int propKind) {
    if (propKind < 0 || propKind >= PROP_KIND_COUNT) return 0.0;
    const PropInfo& info = propInfo(propKind);
    if (info.use != PropUse::Occupy || info.lockLevel > 0) return 0.0;   // not a cage
    return propKind == PROP_BED ? 20.0 : 10.0;
}

// A unit knocked out with at least this share of Rested (and some hunger left) gets no Doom: it lies out for
// RESTED_WAKE_HOURS in-game hours and wakes by itself. Waking with a hostile unit within RESTED_HOSTILE_RANGE tiles,
// it plays dead until given an order or until they have gone.
constexpr int RESTED_SAFE_PERCENT = 50;
constexpr float RESTED_WAKE_HOURS = 1.0f;
constexpr float RESTED_HOSTILE_RANGE = 12.0f;

// The most Rested a unit can have: 100, plus its Vitality level (1 to 99)
inline int maxRestedFor(const Stats& s) {
    return 100 + std::max(1, std::min(MAX_LEVEL, levelOf(s, STAT_VITALITY)));
}
// Rested lost each time a unit comes round from being knocked out (not when Doom falls to 0 in a bed or bedroll)
constexpr int RESTED_WAKE_COST = 40;
// Leave Bed: a unit lying in a bed or bedroll at full health and fully rested gets up after this many real seconds
constexpr float LEAVE_BED_SECONDS = 30.0f;
// Rescue: how far a unit looks for one of its faction lying out cold, in tiles
constexpr float RESCUE_RANGE = 40.0f;

// A mob heals this many times as fast as a person, and needs no food to do it
constexpr double MOB_REGEN_FACTOR = 4.0;

// Moves a need by perHour for the hours that passed, keeping the part of a point in `part`, within 0..maximum
inline bool driftNeed(int& value, int maximum, double& part, double perHour, double hours) {
    part += perHour * hours;
    const double whole = std::trunc(part);
    if (whole == 0.0) return false;
    part -= whole;
    const int before = value;
    value = std::max(0, std::min(maximum, value + (int)whole));
    if (value == 0 || value == maximum) part = 0.0;
    return value != before;
}

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
// A mob (mob) needs no food for it, and heals MOB_REGEN_FACTOR times as fast.
inline void regenHealth(Needs& n, bool inCombat, double gameSeconds, bool mob = false) {
    if (n.hp >= n.maxHp) {
        n.healSeconds = 0.0;
        return;
    }
    if (mob) gameSeconds *= MOB_REGEN_FACTOR;
    const bool eligible = !inCombat && n.hp > 0 && (mob || n.hunger * 2 > n.maxHunger);
    if (!eligible || gameSeconds <= 0.0) return;
    n.healSeconds += gameSeconds;
    while (n.healSeconds >= REGEN_GAME_SECONDS && n.hp < n.maxHp) {
        ++n.hp;
        n.changed |= NEEDS_HP_CHANGED;
        n.healSeconds -= REGEN_GAME_SECONDS;
    }
    if (n.hp >= n.maxHp) n.healSeconds = 0.0;
}
