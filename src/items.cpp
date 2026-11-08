#include "items.h"
#include "textcase.h"
#include "language.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include <utility>
#include "pak.h"

namespace {
bool parseIntText(const std::string& v, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (*end != '\0') return false;
    out = (int)n;
    return true;
}

bool parseFloatText(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (*end != '\0') return false;
    out = f;
    return true;
}

bool setField(ItemDef& item, const std::string& key, const std::string& val, std::string& err) {
    const std::string where = " on item " + std::to_string(item.id);
    auto needInt = [&](int& field) {
        if (parseIntText(val, field)) return true;
        err = "bad number for " + key + where + ": '" + val + "'";
        return false;
    };
    auto needFloat = [&](float& field) {
        if (parseFloatText(val, field)) return true;
        err = "bad number for " + key + where + ": '" + val + "'";
        return false;
    };

    if (key == "type") item.typeId = val;
    else if (key == "name") item.baseName = val;
    else if (key == "description") item.description = val;
    else if (key == "material") item.material = val;
    else if (key == "equip_slot") item.equipSlot = val;
    else if (key == "on_use") item.onUse = val;
    else if (key == "tags") {
        item.tags.clear();
        size_t at = 0;
        while (at <= val.size()) {
            size_t comma = val.find(',', at);
            if (comma == std::string::npos) comma = val.size();
            std::string tag = val.substr(at, comma - at);
            while (!tag.empty() && std::isspace((unsigned char)tag.back())) tag.pop_back();
            size_t lead = 0;
            while (lead < tag.size() && std::isspace((unsigned char)tag[lead])) ++lead;
            tag = tag.substr(lead);
            tag = toLower(tag);
            if (!tag.empty()) item.tags.push_back(tag);
            at = comma + 1;
        }
    }
    else if (key == "max_stack_size") return needInt(item.maxStackSize);
    else if (key == "weight") return needFloat(item.weight);
    else if (key == "buy_value") return needInt(item.buyValue);
    else if (key == "sell_value") return needInt(item.sellValue);
    else if (key == "required_level") return needInt(item.requiredLevel);
    else if (key == "attack_value") return needInt(item.attackValue);
    else if (key == "defense_value") return needInt(item.defenseValue);
    else if (key == "tier") return needInt(item.tier);
    else if (key == "cooks_into") return needInt(item.cooksInto);
    else {
        err = "unknown key '" + key + "'" + where;
        return false;
    }
    return true;
}

bool finishItem(ItemDef& item, std::string& err) {
    const std::string where = " on item " + std::to_string(item.id);
    if (item.typeId != "item" && item.typeId != "consumable" && item.typeId != "weapon" &&
        item.typeId != "armor" && item.typeId != "accessory") {
        err = "unknown type '" + item.typeId + "'" + where;
        return false;
    }
    if (item.baseName.empty()) { err = "no name" + where; return false; }
    if (item.tier < 0) { err = "negative tier" + where; return false; }
    if (!item.equipSlot.empty()) {
        static const char* const slots[] = {
            "head", "torso", "legs", "feet", "accessory", "main_hand", "off_hand", "either_hand", "two_hand",
        };
        bool known = false;
        for (const char* s : slots) if (item.equipSlot == s) known = true;
        if (!known) { err = "unknown equip_slot '" + item.equipSlot + "'" + where; return false; }
    }
    if (item.maxStackSize < 1) { err = "max_stack_size must be at least 1" + where; return false; }

    item.name = item.material.empty() ? item.baseName : item.material + " " + item.baseName;
    if (item.tier > 0) item.name += " +" + std::to_string(item.tier);
    return true;
}
}

bool parseItemDb(const std::string& text, std::vector<ItemDef>& out, std::string& err) {
    out.clear();
    err.clear();
    std::unordered_set<int> ids;
    bool open = false;
    ItemDef current;
    int line = 0;

    auto closeBlock = [&]() {
        if (!open) return true;
        open = false;
        if (!finishItem(current, err)) return false;
        out.push_back(current);
        return true;
    };

    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        ++line;
        const std::string s = trim(raw);
        if (s.empty() || s[0] == '#') continue;
        const std::string where = " (line " + std::to_string(line) + ")";

        if (s.front() == '[') {
            if (s.back() != ']') { err = "bad item header" + where; return false; }
            if (!closeBlock()) { err += where; return false; }
            current = ItemDef{};
            if (!parseIntText(trim(s.substr(1, s.size() - 2)), current.id) || current.id <= 0) {
                err = "item id must be a positive number" + where;
                return false;
            }
            if (!ids.insert(current.id).second) {
                err = "item id " + std::to_string(current.id) + " appears twice" + where;
                return false;
            }
            open = true;
            continue;
        }

        const size_t eq = s.find('=');
        if (eq == std::string::npos) { err = "expected key = value" + where; return false; }
        if (!open) { err = "value before any item header" + where; return false; }
        const std::string key = trim(s.substr(0, eq));
        const std::string val = trim(s.substr(eq + 1));
        if (!setField(current, key, val, err)) { err += where; return false; }
    }
    if (!closeBlock()) return false;
    return true;
}

namespace {
std::vector<ItemDef> itemTable;
std::vector<const ItemDef*> itemIndex;
}

bool loadItemDbText(const std::string& text, std::string& err) {
    std::vector<ItemDef> parsed;
    if (!parseItemDb(text, parsed, err)) return false;
    itemTable = std::move(parsed);
    itemIndex.clear();
    for (const ItemDef& d : itemTable) {
        if ((size_t)d.id >= itemIndex.size()) itemIndex.resize((size_t)d.id + 1, nullptr);
        itemIndex[(size_t)d.id] = &d;
    }
    return true;
}

const std::vector<ItemDef>& itemDatabase() { return itemTable; }

bool spoilsOnGround(int item) {
    const ItemDef* d = findItem(item);
    return d && (hasTag(*d, "raw_food") || hasTag(*d, "bones") || d->onUse.find("hunger") != std::string::npos);
}

int itemTagged(const char* tag) {
    for (const ItemDef& d : itemTable) {
        if (hasTag(d, tag)) return d.id;
    }
    return 0;
}

const ItemDef* findItem(int id) {
    id = baseItemId(id);
    return id > 0 && (size_t)id < itemIndex.size() ? itemIndex[(size_t)id] : nullptr;
}

namespace {
std::string upperAscii(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return s;
}
}

std::string itemDisplayName(const ItemDef& d) {
    const std::string key = "ITEM_" + std::to_string(d.id) + "_NAME";
    std::string base = lang(key);
    if (base == key) base = d.baseName;
    std::string out = base;
    if (!d.material.empty()) {
        const std::string matKey = "MISC_MATERIAL_" + upperAscii(d.material);
        std::string material = lang(matKey);
        if (material == matKey) material = d.material;
        out = langf("MISC_ITEM_NAME_FORMAT", { material, base });
    }
    if (d.tier > 0) out += " +" + std::to_string(d.tier);
    return out;
}

std::string itemDisplayDescription(const ItemDef& d) {
    const std::string key = "ITEM_" + std::to_string(d.id) + "_DESC";
    const std::string text = lang(key);
    return text == key ? d.description : text;
}

std::string itemNameOf(int item) {
    const ItemDef* d = findItem(item);
    if (!d) return std::string(lang("MISC_UNKNOWN_ITEM"));
    return isStolen(item) ? langf("MISC_ITEM_STOLEN_NAME", { itemDisplayName(*d) }) : itemDisplayName(*d);
}
