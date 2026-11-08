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

    for (int i = 1; i <= mobKindCount(); ++i) {
        const MobKind kind = (MobKind)i;
        const MobInfo& m = mobInfo(kind);
        needItem(m.meat, "mob " + m.key + "'s meat");
        needItem(m.bones, "mob " + m.key + "'s bones");
        if (!findFaction(mobFaction(kind))) out.push_back("mob " + m.key + " has no faction " + std::to_string(mobFaction(kind)) + " in data/factions.txt");
    }

    for (int i = 0; i < propKindCount(); ++i) {
        const PropInfo& p = propInfo((PropKind)i);
        needItem(p.item, "prop " + p.key + "'s item");
        needItem(p.input, "prop " + p.key + "'s input");
        needItem(p.altItem, "prop " + p.key + "'s other item");
    }
    for (const char* tag : { ITEM_TAG_COMMON_BONES, ITEM_TAG_GOLD_COIN, ITEM_TAG_IRON_ORE, ITEM_TAG_WOOD_LOG, ITEM_TAG_STONE,
                             ITEM_TAG_IRON_INGOT, ITEM_TAG_DRAGON_CRYSTAL }) {
        int tagged = 0;
        for (const ItemDef& d : itemDatabase()) tagged += hasTag(d, tag) ? 1 : 0;
        if (tagged != 1) out.push_back(std::string("the tag ") + tag + " must be on one item in data/itemdb.txt, not " + std::to_string(tagged));
    }
    return out;
}
