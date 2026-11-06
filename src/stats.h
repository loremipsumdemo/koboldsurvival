#pragma once
#include "enum_index.h"
#include <algorithm>
#include <array>
#include <string>
#include "language.h"

// Unit stats: 7 attributes, 4 combat, 5 resource gathering, 5 crafting and 3 utility skills. Each
// has a level from 1 to 99. The level comes from the total XP in that stat, which uses the
// RuneScape formula, so level 92 is about half the XP of level 99.

enum class StatId : int {
    None = -1,   // no stat: a prop that trains none
    Strength = 0,
    Dexterity,
    Vitality,
    Agility,
    Intelligence,
    Wisdom,
    Charisma,
    Melee,
    Projectiles,
    Arcane,
    Block,
    Mining,
    Herbalism,
    Woodcutting,
    Skinning,
    Farming,
    Cooking,
    Smelting,
    Forging,
    Carpentry,
    Stoneworking,
    Lockpicking,
    Thievery,
    Medical,
    Count
};
constexpr int STAT_COUNT = idx(StatId::Count);

enum class StatGroup { Attributes, Combat, Gathering, Crafting, Utility };

struct StatInfo {
    const char* key;        // the language key for its name, e.g. UILABEL_STRENGTH_STAT
    StatGroup group;
    const char* effect;     // what the stat will do once the feature it belongs to exists
    int icon;               // its picture, data/icon/icon_<n>.png
};

constexpr int MAX_LEVEL = 99;

// The name, group and effect of a stat
const StatInfo& statInfo(StatId stat);

// The name of a stat in the language in use
std::string statName(StatId stat);

// Total XP needed to reach a level, 1..99. Level 1 needs 0 XP. Levels outside the range are clamped.
int xpForLevel(int level);

// The highest level (1..99) that a total of xp reaches
int levelForXp(int xp);

// Total XP for each stat, in StatId order
// The XP of each stat. It takes a StatId or a stat number as its index.
struct StatXp : std::array<int, STAT_COUNT> {
    using std::array<int, STAT_COUNT>::operator[];
    int& operator[](StatId s) { return (*this)[idx(s)]; }
    const int& operator[](StatId s) const { return (*this)[idx(s)]; }
};

struct Stats {
    StatXp xp{};
};

// The stats of a unit with no saved stats: attributes at level 10, skills at level 1
inline Stats defaultStats() {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) {
        s.xp[i] = xpForLevel(statInfo((StatId)i).group == StatGroup::Attributes ? 10 : 1);
    }
    return s;
}

// Current level of one stat
inline int levelOf(const Stats& s, StatId stat) {
    return levelForXp(s.xp[stat]);
}

// How far a stat is from its current level to the next: 0 at the XP for the current level, 1 at the XP for the
// next. A stat at level 99 is full.
inline float levelProgress(const Stats& s, StatId stat) {
    const int lv = levelOf(s, stat);
    if (lv >= MAX_LEVEL) return 1.0f;
    const int lo = xpForLevel(lv), hi = xpForLevel(lv + 1);
    if (hi <= lo) return 0.0f;
    return std::clamp((float)(s.xp[stat] - lo) / (float)(hi - lo), 0.0f, 1.0f);
}

// Adds XP to one stat, capped at the XP for level 99. Returns how many levels it gained.
int addXp(Stats& s, StatId stat, int amount);

// How much a unit can carry, in weight. Strength sets it: 40 at level 1, and 4 more for each level.
inline float carryLimit(const Stats& s) {
    return 40.0f + 4.0f * (float)(levelOf(s, StatId::Strength) - 1);
}
