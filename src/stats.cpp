#include "stats.h"
#include "language.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {

const std::array<StatInfo, STAT_COUNT>& infoTable() {
    static const std::array<StatInfo, STAT_COUNT> table = {{
        { "UILABEL_STRENGTH_STAT",     StatGroup::Attributes, "UILABEL_STAT_DESC_STRENGTH" },
        { "UILABEL_DEXTERITY_STAT",    StatGroup::Attributes, "UILABEL_STAT_DESC_DEXTERITY" },
        { "UILABEL_VITALITY_STAT",     StatGroup::Attributes, "UILABEL_STAT_DESC_VITALITY" },
        { "UILABEL_ENDURANCE_STAT",    StatGroup::Attributes, "UILABEL_STAT_DESC_ENDURANCE" },
        { "UILABEL_AGILITY_STAT",      StatGroup::Attributes, "UILABEL_STAT_DESC_AGILITY" },
        { "UILABEL_INTELLIGENCE_STAT", StatGroup::Attributes, "UILABEL_STAT_DESC_INTELLIGENCE" },
        { "UILABEL_WISDOM_STAT",       StatGroup::Attributes, "UILABEL_STAT_DESC_WISDOM" },
        { "UILABEL_CHARISMA_STAT",     StatGroup::Attributes, "UILABEL_STAT_DESC_CHARISMA" },
        { "UILABEL_PERCEPTION_STAT",   StatGroup::Attributes, "UILABEL_STAT_DESC_PERCEPTION" },
        { "UILABEL_ATHLETICS_STAT",    StatGroup::Attributes, "UILABEL_STAT_DESC_ATHLETICS" },
        { "UILABEL_STEALTH_STAT",      StatGroup::Attributes, "UILABEL_STAT_DESC_STEALTH" },
        { "UILABEL_MELEE_STAT",        StatGroup::Combat,     "UILABEL_STAT_DESC_MELEE" },
        { "UILABEL_PROJECTILES_STAT",  StatGroup::Combat,     "UILABEL_STAT_DESC_PROJECTILES" },
        { "UILABEL_ARCANE_STAT",       StatGroup::Combat,     "UILABEL_STAT_DESC_ARCANE" },
        { "UILABEL_BLOCK_STAT",        StatGroup::Combat,     "UILABEL_STAT_DESC_BLOCK" },
        { "UILABEL_MINING_STAT",       StatGroup::Gathering,  "UILABEL_STAT_DESC_MINING" },
        { "UILABEL_HERBALISM_STAT",    StatGroup::Gathering,  "UILABEL_STAT_DESC_HERBALISM" },
        { "UILABEL_WOODCUTTING_STAT",  StatGroup::Gathering,  "UILABEL_STAT_DESC_WOODCUTTING" },
        { "UILABEL_SKINNING_STAT",     StatGroup::Gathering,  "UILABEL_STAT_DESC_SKINNING" },
        { "UILABEL_FARMING_STAT",      StatGroup::Gathering,  "UILABEL_STAT_DESC_FARMING" },
        { "UILABEL_COOKING_STAT",      StatGroup::Crafting,   "UILABEL_STAT_DESC_COOKING" },
        { "UILABEL_SMELTING_STAT",     StatGroup::Crafting,   "UILABEL_STAT_DESC_SMELTING" },
        { "UILABEL_FORGING_STAT",      StatGroup::Crafting,   "UILABEL_STAT_DESC_FORGING" },
        { "UILABEL_CARPENTRY_STAT",    StatGroup::Crafting,   "UILABEL_STAT_DESC_CARPENTRY" },
        { "UILABEL_STONEWORKING_STAT", StatGroup::Crafting,   "UILABEL_STAT_DESC_STONEWORKING" },
        { "UILABEL_LOCKPICKING_STAT",  StatGroup::Utility,    "UILABEL_STAT_DESC_LOCKPICKING" },
        { "UILABEL_THIEVERY_STAT",     StatGroup::Utility,    "UILABEL_STAT_DESC_THIEVERY" },
        { "UILABEL_MEDICAL_STAT",      StatGroup::Utility,    "UILABEL_STAT_DESC_MEDICAL" },
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
