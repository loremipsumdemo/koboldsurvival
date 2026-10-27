#pragma once
#include <algorithm>
#include <array>
#include <string>

// Unit stats: 11 attributes, 4 combat, 5 resource gathering, 5 crafting and 3 utility skills. Each
// has a level from 1 to 99. The level comes from the total XP in that stat, which uses the
// RuneScape formula, so level 92 is about half the XP of level 99.

enum StatId : int {
    STAT_STRENGTH = 0,
    STAT_DEXTERITY,
    STAT_VITALITY,
    STAT_ENDURANCE,
    STAT_AGILITY,
    STAT_INTELLIGENCE,
    STAT_WISDOM,
    STAT_CHARISMA,
    STAT_PERCEPTION,
    STAT_ATHLETICS,
    STAT_STEALTH,
    STAT_MELEE,
    STAT_PROJECTILES,
    STAT_ARCANE,
    STAT_BLOCK,
    STAT_MINING,
    STAT_HERBALISM,
    STAT_WOODCUTTING,
    STAT_SKINNING,
    STAT_FARMING,
    STAT_COOKING,
    STAT_SMELTING,
    STAT_FORGING,
    STAT_CARPENTRY,
    STAT_STONEWORKING,
    STAT_LOCKPICKING,
    STAT_THIEVERY,
    STAT_MEDICAL,
    STAT_COUNT
};

enum class StatGroup { Attributes, Combat, Gathering, Crafting, Utility };

struct StatInfo {
    const char* name;
    StatGroup group;
    const char* effect;     // what the stat will do once the feature it belongs to exists
};

constexpr int MAX_LEVEL = 99;

// The name, group and effect of a stat
const StatInfo& statInfo(int stat);

// Total XP needed to reach a level, 1..99. Level 1 needs 0 XP. Levels outside the range are clamped.
int xpForLevel(int level);

// The highest level (1..99) that a total of xp reaches
int levelForXp(int xp);

// Total XP for each stat, in StatId order
struct Stats {
    std::array<int, STAT_COUNT> xp{};
};

// The stats of a unit with no saved stats: attributes at level 10, skills at level 1
inline Stats defaultStats() {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) {
        s.xp[i] = xpForLevel(statInfo(i).group == StatGroup::Attributes ? 10 : 1);
    }
    return s;
}

// Current level of one stat
inline int levelOf(const Stats& s, int stat) {
    return levelForXp(s.xp[stat]);
}

// How far a stat is from its current level to the next: 0 at the XP for the current level, 1 at the XP for the
// next. A stat at level 99 is full.
inline float levelProgress(const Stats& s, int stat) {
    const int lv = levelOf(s, stat);
    if (lv >= MAX_LEVEL) return 1.0f;
    const int lo = xpForLevel(lv), hi = xpForLevel(lv + 1);
    if (hi <= lo) return 0.0f;
    return std::clamp((float)(s.xp[stat] - lo) / (float)(hi - lo), 0.0f, 1.0f);
}

// Adds XP to one stat, capped at the XP for level 99. Returns how many levels it gained.
int addXp(Stats& s, int stat, int amount);

// How much a unit can carry, in weight. Strength sets it: 40 at level 1, and 4 more for each level.
inline float carryLimit(const Stats& s) {
    return 40.0f + 4.0f * (float)(levelOf(s, STAT_STRENGTH) - 1);
}
