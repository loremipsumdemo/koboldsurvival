#pragma once
#include <cctype>
#include <string>
#include <vector>

struct ItemDef {
    int id = 0;
    std::string typeId;
    int maxStackSize = 1;
    std::string name;
    std::string baseName;
    std::string material;
    int tier = 0;
    std::string equipSlot;
    std::string description;
    float weight = 0.0f;
    int buyValue = 0;
    int sellValue = 0;
    int requiredLevel = 1;
    int attackValue = 0;
    int defenseValue = 0;
    std::string onUse;
    std::vector<std::string> tags;
    int cooksInto = 0;
};

bool spoilsOnGround(int item);
inline bool hasTag(const ItemDef& d, const std::string& tag) {
    for (const std::string& t : d.tags) {
        if (t == tag) return true;
    }
    return false;
}

bool parseItemDb(const std::string& text, std::vector<ItemDef>& out, std::string& err);

// Replaces the items. On a mistake the items are left as they were
bool loadItemDbText(const std::string& text, std::string& err);

const std::vector<ItemDef>& itemDatabase();

const ItemDef* findItem(int id);

// The items the game asks for by name. Each is the tag of exactly one item in data/itemdb.txt.
inline constexpr const char* ITEM_TAG_COMMON_BONES = "common_bones";
inline constexpr const char* ITEM_TAG_GOLD_COIN = "gold_coin";
inline constexpr const char* ITEM_TAG_IRON_ORE = "iron_ore";
inline constexpr const char* ITEM_TAG_WOOD_LOG = "wood_log";
inline constexpr const char* ITEM_TAG_STONE = "stone";
inline constexpr const char* ITEM_TAG_IRON_INGOT = "iron_ingot";
inline constexpr const char* ITEM_TAG_DRAGON_CRYSTAL = "dragon_crystal";
// The first item with this tag, or 0
int itemTagged(const char* tag);

std::string itemDisplayName(const ItemDef& d);
std::string itemNameOf(int item);
std::string itemDisplayDescription(const ItemDef& d);

