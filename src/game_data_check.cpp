#include "game_data_check.h"
#include "factions.h"
#include "items.h"
#include "mobs.h"
#include "props.h"
#include "useitem.h"

std::vector<std::string> gameDataProblems() {
    std::vector<std::string> out;
    const auto needItem = [&](int item, const std::string& who) {
        if (item > 0 && !findItem(item)) out.push_back(who + " names item " + std::to_string(item) + ", which is not in data/itemdb.txt");
    };

    for (const ItemDef& d : itemDatabase()) {
        const std::string who = "item " + std::to_string(d.id);
        needItem(d.cooksInto, who + "'s cooks_into");
        std::vector<UseEffect> effects;
        std::string err;
        if (!d.onUse.empty() && !parseUseScript(d.onUse, effects, err)) out.push_back(who + " has a bad on_use: " + err);
    }

    for (int i = 1; i <= MOB_KIND_COUNT; ++i) {
        const MobKind kind = (MobKind)i;
        const MobInfo& m = mobInfo(kind);
        needItem(m.meat, "mob " + m.nameKey + "'s meat");
        needItem(m.bones, "mob " + m.nameKey + "'s bones");
        if (!findFaction(mobFaction(kind))) out.push_back("mob " + m.nameKey + " has no faction " + std::to_string(mobFaction(kind)) + " in data/factions.txt");
    }

    for (int i = 0; i < PROP_KIND_COUNT; ++i) {
        const PropInfo& p = propInfo((PropKind)i);
        needItem(p.item, std::string("prop ") + p.nameKey + "'s item");
        needItem(p.input, std::string("prop ") + p.nameKey + "'s input");
        needItem(p.altItem, std::string("prop ") + p.nameKey + "'s other item");
    }
    needItem(BONES_ITEM, "BONES_ITEM");
    return out;
}
