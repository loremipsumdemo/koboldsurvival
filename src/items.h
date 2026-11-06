#pragma once
#include <cctype>
#include <string>
#include <vector>

// One entry in the item database (data/itemdb.txt).
struct ItemDef {
    int id = 0;
    std::string typeId;               // 'item', 'consumable', 'weapon', 'armor' or 'accessory'
    int maxStackSize = 1;             // most of this item one inventory slot can hold
    // The display name, built from the file: material, then base name, then tier.
    // 'Iron', 'Short Sword', tier 4 gives "Iron Short Sword +4".
    std::string name;
    std::string baseName;             // the name as written, e.g. "Short Sword"
    std::string material;             // e.g. "Iron". Empty for items with none.
    int tier = 0;                     // shown as +N after the name. 0 for none.
    // Where gear goes: "head", "torso", "legs", "feet", "accessory", "main_hand", "off_hand",
    // "either_hand" or "two_hand". Empty for items that are not gear.
    std::string equipSlot;
    std::string description;
    float weight = 0.0f;
    int buyValue = 0;
    int sellValue = 0;
    int requiredLevel = 1;
    int attackValue = 0;
    int defenseValue = 0;
    std::string onUse;               // right-click script from the inventory, e.g. "health +50; hunger +10". Empty for none.
    // Words that sort the item for the props that take only some items, lower case: raw_food (the Raw Food Storage),
    // water (the Water Tank), ore. Empty for none.
    std::vector<std::string> tags;
    int cooksInto = 0;               // the item a campfire cooks this one into (cooks_into), or 0 if it can't be cooked
};

// Whether an item left on the ground goes after a while: raw meat, bones, and anything eaten for hunger
bool spoilsOnGround(int item);
inline bool hasTag(const ItemDef& d, const std::string& tag) {
    for (const std::string& t : d.tags) {
        if (t == tag) return true;
    }
    return false;
}

// Reads the blocks of an itemdb.txt file: [id], then key = value lines. Returns false and sets err if a block is bad.
bool parseItemDb(const std::string& text, std::vector<ItemDef>& out, std::string& err);

// The game's item database, read once from data/itemdb.txt in the pak. Empty if the file is
// missing or can't be parsed; the reason is printed to stderr.
const std::vector<ItemDef>& itemDatabase();

// The definition with this id, or null if there is none. Id 0 means "no item".
const ItemDef* findItem(int id);

// The item's name and description in the language in use. A name the language lacks falls back to the English one.
std::string itemDisplayName(const ItemDef& d);
// The display name of an item by its id, or "Unknown item" for an id that is not in the item list
std::string itemNameOf(int item);
std::string itemDisplayDescription(const ItemDef& d);

