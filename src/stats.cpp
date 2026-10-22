#include "stats.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {

const std::array<StatInfo, STAT_COUNT>& infoTable() {
    static const std::array<StatInfo, STAT_COUNT> table = {{
        { "Strength",     StatGroup::Attributes, "Melee damage, weight carry capacity" },
        { "Dexterity",    StatGroup::Attributes, "Attack speed" },
        { "Vitality",     StatGroup::Attributes, "Max health" },
        { "Endurance",    StatGroup::Attributes, "Max stamina" },
        { "Agility",      StatGroup::Attributes, "Dodge chance" },
        { "Intelligence", StatGroup::Attributes, "Mana pool" },
        { "Wisdom",       StatGroup::Attributes, "Learnable spell number capacity" },
        { "Charisma",     StatGroup::Attributes, "NPC trade prices, unique dialogue options" },
        { "Perception",   StatGroup::Attributes, "Detection range" },
        { "Athletics",    StatGroup::Attributes, "Running speed" },
        { "Stealth",      StatGroup::Attributes, "Better at avoiding sneak detection" },
        { "Melee",        StatGroup::Combat,     "Melee accuracy" },
        { "Projectiles",  StatGroup::Combat,     "Ranged accuracy" },
        { "Arcane",       StatGroup::Combat,     "Spell power" },
        { "Block",        StatGroup::Combat,     "Block/parry chance" },
        { "Mining",       StatGroup::Gathering,  "Ore yield" },
        { "Herbalism",    StatGroup::Gathering,  "Wild plant gather yield" },
        { "Woodcutting",  StatGroup::Gathering,  "Log yield" },
        { "Skinning",     StatGroup::Gathering,  "Hide/meat yield" },
        { "Farming",      StatGroup::Gathering,  "Crop yield" },
        { "Cooking",      StatGroup::Crafting,   "Food buff strength" },
        { "Smelting",     StatGroup::Crafting,   "Ore smelting yield" },
        { "Forging",      StatGroup::Crafting,   "Metal crafting" },
        { "Carpentry",    StatGroup::Crafting,   "Wood crafting" },
        { "Stoneworking", StatGroup::Crafting,   "Stone crafting" },
        { "Lockpicking",  StatGroup::Utility,    "Lock difficulty" },
        { "Thievery",     StatGroup::Utility,    "Stealing success" },
        { "Medical",      StatGroup::Utility,    "Healing effectiveness" },
    }};
    return table;
}

// table[L] is the XP to reach level L, for L = 1..99. Built once from the formula:
//   XP(L) = floor( 1/4 * sum for l = 1 .. L-1 of floor( l + 300 * 2^(l/7) ) )
const std::array<int, MAX_LEVEL + 1>& xpTable() {
    static const std::array<int, MAX_LEVEL + 1> table = [] {
        std::array<int, MAX_LEVEL + 1> out{};
        long long sum = 0;   // raw values of every level below the one being filled
        for (int level = 1; level <= MAX_LEVEL; ++level) {
            out[level] = (int)(sum / 4);
            sum += (long long)std::floor(level + 300.0 * std::pow(2.0, level / 7.0));
        }
        return out;
    }();
    return table;
}

} // namespace

const StatInfo& statInfo(int stat) {
    return infoTable()[std::clamp(stat, 0, STAT_COUNT - 1)];
}

int xpForLevel(int level) {
    return xpTable()[std::clamp(level, 1, MAX_LEVEL)];
}

int levelForXp(int xp) {
    for (int level = MAX_LEVEL; level > 1; --level) {
        if (xp >= xpTable()[level]) return level;
    }
    return 1;
}

int addXp(Stats& s, int stat, int amount) {
    if (stat < 0 || stat >= STAT_COUNT || amount <= 0) return 0;
    const int before = levelOf(s, stat);
    const int cap = xpForLevel(MAX_LEVEL);
    s.xp[stat] = (int)std::min<long long>(cap, (long long)s.xp[stat] + amount);
    return levelOf(s, stat) - before;
}
