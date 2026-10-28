#include "stats.h"
#include "language.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {

const std::array<StatInfo, STAT_COUNT>& infoTable() {
    static const std::array<StatInfo, STAT_COUNT> table = {{
        { "UILABEL_STRENGTH_STAT",     StatGroup::Attributes, "Melee damage, weight carry capacity" },
        { "UILABEL_DEXTERITY_STAT",    StatGroup::Attributes, "Attack speed" },
        { "UILABEL_VITALITY_STAT",     StatGroup::Attributes, "Max health" },
        { "UILABEL_ENDURANCE_STAT",    StatGroup::Attributes, "Max stamina" },
        { "UILABEL_AGILITY_STAT",      StatGroup::Attributes, "Dodge chance" },
        { "UILABEL_INTELLIGENCE_STAT", StatGroup::Attributes, "Mana pool" },
        { "UILABEL_WISDOM_STAT",       StatGroup::Attributes, "Learnable spell number capacity" },
        { "UILABEL_CHARISMA_STAT",     StatGroup::Attributes, "NPC trade prices, unique dialogue options" },
        { "UILABEL_PERCEPTION_STAT",   StatGroup::Attributes, "Detection range" },
        { "UILABEL_ATHLETICS_STAT",    StatGroup::Attributes, "Running speed" },
        { "UILABEL_STEALTH_STAT",      StatGroup::Attributes, "Better at avoiding sneak detection" },
        { "UILABEL_MELEE_STAT",        StatGroup::Combat,     "Melee accuracy" },
        { "UILABEL_PROJECTILES_STAT",  StatGroup::Combat,     "Ranged accuracy" },
        { "UILABEL_ARCANE_STAT",       StatGroup::Combat,     "Spell power" },
        { "UILABEL_BLOCK_STAT",        StatGroup::Combat,     "Block/parry chance" },
        { "UILABEL_MINING_STAT",       StatGroup::Gathering,  "Ore yield" },
        { "UILABEL_HERBALISM_STAT",    StatGroup::Gathering,  "Wild plant gather yield" },
        { "UILABEL_WOODCUTTING_STAT",  StatGroup::Gathering,  "Log yield" },
        { "UILABEL_SKINNING_STAT",     StatGroup::Gathering,  "Hide/meat yield" },
        { "UILABEL_FARMING_STAT",      StatGroup::Gathering,  "Crop yield" },
        { "UILABEL_COOKING_STAT",      StatGroup::Crafting,   "Food buff strength" },
        { "UILABEL_SMELTING_STAT",     StatGroup::Crafting,   "Ore smelting yield" },
        { "UILABEL_FORGING_STAT",      StatGroup::Crafting,   "Metal crafting" },
        { "UILABEL_CARPENTRY_STAT",    StatGroup::Crafting,   "Wood crafting" },
        { "UILABEL_STONEWORKING_STAT", StatGroup::Crafting,   "Stone crafting" },
        { "UILABEL_LOCKPICKING_STAT",  StatGroup::Utility,    "Lock difficulty" },
        { "UILABEL_THIEVERY_STAT",     StatGroup::Utility,    "Stealing success" },
        { "UILABEL_MEDICAL_STAT",      StatGroup::Utility,    "Healing effectiveness" },
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

std::string statName(int stat) {
    return lang(statInfo(stat).key);
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
