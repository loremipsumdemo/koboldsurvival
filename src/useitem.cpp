#include "useitem.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "inventory.h"
#include "items.h"

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Reads one statement such as "health +50" or "hunger -10"
bool parseStatement(const std::string& raw, UseEffect& e, std::string& err) {
    const std::string s = trim(raw);

    size_t i = 0;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) ++i;
    const std::string name = lower(s.substr(0, i));
    if (name == "health") e.stat = UseEffect::Stat::Health;
    else if (name == "hunger") e.stat = UseEffect::Stat::Hunger;
    else if (name.empty()) { err = "expected health or hunger in \"" + s + "\""; return false; }
    else { err = "unknown stat \"" + name + "\" in \"" + s + "\" (use health or hunger)"; return false; }

    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i >= s.size() || (s[i] != '+' && s[i] != '-')) {
        err = "expected + or - after " + name + " in \"" + s + "\"";
        return false;
    }
    const bool negative = s[i] == '-';
    ++i;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;

    const size_t digitsAt = i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
    if (i == digitsAt) { err = "expected a number after the sign in \"" + s + "\""; return false; }
    if (i != s.size()) { err = "unexpected text after the number in \"" + s + "\""; return false; }
    if (i - digitsAt > 6) { err = "the number is too large in \"" + s + "\""; return false; }

    e.amount = std::stoi(s.substr(digitsAt, i - digitsAt));
    if (negative) e.amount = -e.amount;
    return true;
}

} // namespace

bool parseUseScript(const std::string& script, std::vector<UseEffect>& out, std::string& err) {
    out.clear();
    err.clear();

    std::string piece;
    auto finish = [&]() -> bool {
        if (trim(piece).empty()) {   // an empty statement, such as one after a trailing ';', is fine
            piece.clear();
            return true;
        }
        UseEffect e;
        if (!parseStatement(piece, e, err)) return false;
        out.push_back(e);
        piece.clear();
        return true;
    };

    for (char c : script) {
        if (c == ';' || c == '\n') {
            if (!finish()) return false;
        } else {
            piece += c;
        }
    }
    return finish();
}

void applyUseEffects(const std::vector<UseEffect>& effects, Needs& needs) {
    for (const UseEffect& e : effects) {
        if (e.stat == UseEffect::Stat::Health) {
            needs.hp = std::clamp(needs.hp + e.amount, 0, needs.maxHp);
        } else {
            needs.hunger = std::clamp(needs.hunger + e.amount, 0, needs.maxHunger);
        }
    }
}

UseResult useStackInSlot(World& w, Entity unit, int slot) {
    Inventory* inv = w.get<Inventory>(unit);
    if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return UseResult::Empty;

    ItemStack& s = inv->slots[slot];
    if (s.item == 0 || s.count <= 0) return UseResult::Empty;

    const ItemDef* def = findItem(s.item);
    if (!def || def->typeId != "consumable" || def->onUse.empty()) return UseResult::NotUsable;

    std::vector<UseEffect> effects;
    std::string err;
    if (!parseUseScript(def->onUse, effects, err)) {
        std::fprintf(stderr, "items: bad use script on %s: %s\n", def->name.c_str(), err.c_str());
        return UseResult::NotUsable;
    }
    if (effects.empty()) return UseResult::NotUsable;

    if (Needs* needs = w.get<Needs>(unit)) applyUseEffects(effects, *needs);
    if (--s.count <= 0) s = ItemStack{};
    return UseResult::Used;
}
