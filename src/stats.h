#pragma once
#include "enum_index.h"
#include <algorithm>
#include <array>
#include <string>
#include "language.h"

enum class StatId : int {
    None = -1,
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
    const char* key;
    StatGroup group;
    const char* effect;
    int icon;
};

constexpr int MAX_LEVEL = 99;

const StatInfo& statInfo(StatId stat);

std::string statName(StatId stat);

int xpForLevel(int level);

int levelForXp(int xp);

struct StatXp : std::array<int, STAT_COUNT> {
    using std::array<int, STAT_COUNT>::operator[];
    int& operator[](StatId s) { return (*this)[idx(s)]; }
    const int& operator[](StatId s) const { return (*this)[idx(s)]; }
};

struct Stats {
    StatXp xp{};
};

inline Stats defaultStats() {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) {
        s.xp[i] = xpForLevel(statInfo((StatId)i).group == StatGroup::Attributes ? 10 : 1);
    }
    return s;
}

inline int levelOf(const Stats& s, StatId stat) {
    return levelForXp(s.xp[stat]);
}

inline float levelProgress(const Stats& s, StatId stat) {
    const int lv = levelOf(s, stat);
    if (lv >= MAX_LEVEL) return 1.0f;
    const int lo = xpForLevel(lv), hi = xpForLevel(lv + 1);
    if (hi <= lo) return 0.0f;
    return std::clamp((float)(s.xp[stat] - lo) / (float)(hi - lo), 0.0f, 1.0f);
}

int addXp(Stats& s, StatId stat, int amount);

inline float carryLimit(const Stats& s) {
    return 40.0f + 4.0f * (float)(levelOf(s, StatId::Strength) - 1);
}
