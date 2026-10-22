#pragma once
#include <string>
#include <vector>

// One entry in the item database (data/items.sql). Fields follow the SQL columns.
struct ItemDef {
    int id = 0;
    std::string typeId;               // 'item', 'consumable', 'weapon', 'armor' or 'accessory'
    int maxStackSize = 1;             // most of this item one inventory slot can hold
    std::string name;
    std::string description;
    int maxDurability = 0;
    int currentDurability = 0;
    float weight = 0.0f;
    int buyValue = 0;
    int sellValue = 0;
    int requiredLevel = 1;
    int attackValue = 0;
    int defenseValue = 0;
};

// Reads the INSERT rows of an items.sql file. Comments, the CREATE TABLE and other statements are
// skipped. Columns are matched by name, so their order in the file doesn't matter. Quoted text may
// hold '' for an apostrophe. Returns false and sets err if a row is bad or an id repeats.
bool parseItemSql(const std::string& sql, std::vector<ItemDef>& out, std::string& err);

// The game's item database, read once from data/items.sql in the pak. Empty if the file is
// missing or can't be parsed; the reason is printed to stderr.
const std::vector<ItemDef>& itemDatabase();

// The definition with this id, or null if there is none. Id 0 means "no item".
const ItemDef* findItem(int id);
