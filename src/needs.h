#pragma once
#include <cmath>
#include "components.h"
#include "props.h"
#include "stats.h"

constexpr double RESTED_LOSS_PER_HOUR = 4.0;
constexpr double COMFORT_LOSS_PER_HOUR = 2.0;
inline double restedGainPerHour(PropKind propKind) { return propInfo(propKind).restedPerHour; }
inline double comfortGainPerHour(PropKind propKind) { return propInfo(propKind).comfortPerHour; }

constexpr double CONSCIOUS_TICKS_PER_HOUR = 100.0;
constexpr float RESTED_HOSTILE_RANGE = 12.0f;

inline int maxRestedFor(const Stats& s) {
    return 50 + std::max(1, std::min(MAX_LEVEL, levelOf(s, StatId::Vitality)));
}
constexpr float LEAVE_BED_SECONDS = 30.0f;
constexpr float RESCUE_RANGE = 40.0f;
constexpr float REST_SEARCH_RANGE = 50.0f;

constexpr double MOB_REGEN_FACTOR = 4.0;

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

inline void tickNeeds(Needs& n, int hours) {
    for (int i = 0; i < hours; ++i) {
        if (n.hunger > 0) {
            --n.hunger;
            n.changed |= NEEDS_HUNGER_CHANGED;
        } else if (n.hp > 0) {
            --n.hp;
            n.changed |= NEEDS_HP_CHANGED;
        } else {
            break;
        }
    }
}

constexpr double REGEN_GAME_SECONDS = 360.0;

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
