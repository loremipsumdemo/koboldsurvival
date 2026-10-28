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
    int maxDurability = 0;
    int currentDurability = 0;
    float weight = 0.0f;
    int buyValue = 0;
    int sellValue = 0;
    int requiredLevel = 1;
    int attackValue = 0;
    int defenseValue = 0;
    std::string onUse;               // right-click script from the inventory, e.g. "health +50; hunger +10". Empty for none.
};

// Reads the blocks of an itemdb.txt file: [id], then key = value lines. Returns false and sets err if a block is bad.
bool parseItemDb(const std::string& text, std::vector<ItemDef>& out, std::string& err);

// The game's item database, read once from data/itemdb.txt in the pak. Empty if the file is
// missing or can't be parsed; the reason is printed to stderr.
const std::vector<ItemDef>& itemDatabase();

// Replaces the database with these items and stops the pak from being read. For tests, which
// supply their own definitions. Any pointer from findItem before this call is no longer valid.
void replaceItemDatabase(std::vector<ItemDef> items);

// The definition with this id, or null if there is none. Id 0 means "no item".
const ItemDef* findItem(int id);

// Two capital letters for an item shown as a color block: the first letters of its first two
// words, leaving out the +tier. "Iron Short Sword +4" gives "IS", "Bread" gives "BR".
inline std::string itemInitials(const std::string& name) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < name.size()) {
        while (i < name.size() && name[i] == ' ') ++i;
        size_t start = i;
        while (i < name.size() && name[i] != ' ') ++i;
        if (i > start && name[start] != '+') words.push_back(name.substr(start, i - start));
    }
    auto up = [](char c) { return (char)std::toupper((unsigned char)c); };
    if (words.empty()) return "?";
    if (words.size() == 1) {
        std::string out;
        out += up(words[0][0]);
        if (words[0].size() > 1) out += up(words[0][1]);
        return out;
    }
    return std::string(1, up(words[0][0])) + up(words[1][0]);
}
