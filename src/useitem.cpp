#include <SDL3/SDL.h>
#include "useitem.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "textcase.h"
#include "language.h"

namespace {
bool parseStatement(const std::string& raw, UseEffect& e, std::string& err) {
    const std::string s = trim(raw);

    size_t i = 0;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) ++i;
    const std::string name = toLower(s.substr(0, i));
    if (name == "health") e.stat = UseEffect::Stat::Health;
    else if (name == "hunger") e.stat = UseEffect::Stat::Hunger;
    else if (name == "attack") e.stat = UseEffect::Stat::Attack;
    else if (name.empty()) { err = "expected health, hunger or attack in \"" + s + "\""; return false; }
    else { err = "unknown stat \"" + name + "\" in \"" + s + "\" (use health, hunger or attack)"; return false; }

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
    if (i - digitsAt > 6) { err = "the number is too large in \"" + s + "\""; return false; }

    e.amount = std::stoi(s.substr(digitsAt, i - digitsAt));
    if (negative) e.amount = -e.amount;

    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i == s.size()) {
        if (e.stat == UseEffect::Stat::Attack) {
            err = "attack needs a duration, such as \"for 60s\", in \"" + s + "\"";
            return false;
        }
        return true;
    }
    if (s.compare(i, 3, "for") != 0) { err = "unexpected text after the number in \"" + s + "\""; return false; }
    i += 3;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    const size_t secondsAt = i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
    if (i == secondsAt) { err = "expected a number of seconds in \"" + s + "\""; return false; }
    const int seconds = std::stoi(s.substr(secondsAt, i - secondsAt));
    if (i < s.size() && (s[i] == 's' || s[i] == 'S')) ++i;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i != s.size()) { err = "unexpected text after the duration in \"" + s + "\""; return false; }
    if (e.stat != UseEffect::Stat::Attack) { err = "only attack can last a time, in \"" + s + "\""; return false; }
    e.seconds = (float)seconds;
    return true;
}
}

bool parseUseScript(const std::string& script, std::vector<UseEffect>& out, std::string& err) {
    out.clear();
    err.clear();

    std::string piece;
    auto finish = [&]() -> bool {
        if (trim(piece).empty()) {
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

int applyUse(const std::vector<UseEffect>& effects, const UseTarget& t) {
    int restored = 0;
    for (const UseEffect& e : effects) {
        switch (e.stat) {
        case UseEffect::Stat::Health:
            if (t.needs) {
                const int before = t.needs->hp;
                const int amount = e.amount > 0 ? (int)std::lround(e.amount * t.healScale) : e.amount;
                t.needs->hp = std::clamp(before + amount, 0, t.needs->maxHp);
                restored += std::max(0, t.needs->hp - before);
                if (t.needs->hp != before) t.needs->changed |= NEEDS_HP_CHANGED;
            }
            break;
        case UseEffect::Stat::Hunger:
            if (t.needs) {
                const int before = t.needs->hunger;
                t.needs->hunger = std::clamp(before + e.amount, 0, t.needs->maxHunger);
                if (t.needs->hunger != before) t.needs->changed |= NEEDS_HUNGER_CHANGED;
            }
            break;
        case UseEffect::Stat::Attack:
            if (t.combat && e.seconds > 0.0f) {
                t.combat->buffAttack = (float)e.amount;
                t.combat->buffLeft = e.seconds;
            }
            break;
        }
    }
    return restored;
}

int restoreAmount(const ItemDef& def, UseEffect::Stat stat) {
    if (def.typeId != "consumable" || def.onUse.empty()) return 0;
    std::vector<UseEffect> effects;
    std::string err;
    if (!parseUseScript(def.onUse, effects, err)) return 0;
    int total = 0;
    for (const UseEffect& e : effects) {
        if (e.stat == stat && e.amount > 0) total += e.amount;
    }
    return total;
}

UseResult useStackOn(World& w, Entity consumer, Entity target, int slot) {
    Inventory* inv = w.get<Inventory>(consumer);
    if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return UseResult::Empty;

    ItemStack& s = inv->slots[slot];
    if (s.item == 0 || s.count <= 0) return UseResult::Empty;

    const ItemDef* def = findItem(s.item);
    if (!def || def->typeId != "consumable" || def->onUse.empty()) return UseResult::NotUsable;
    if (outCold(w, target) || outCold(w, consumer)) return UseResult::NotUsable;

    std::vector<UseEffect> effects;
    std::string err;
    if (!parseUseScript(def->onUse, effects, err)) {
        SDL_Log("Bad use script on %s: %s", def->name.c_str(), err.c_str());
        return UseResult::NotUsable;
    }
    if (effects.empty()) return UseResult::NotUsable;
    if (w.has<Mob>(target) || w.has<Mob>(consumer)) {
        for (const UseEffect& e : effects) {
            if (e.stat == UseEffect::Stat::Health && e.amount > 0) return UseResult::NotUsable;
        }
    }

    UseTarget t;
    t.needs = w.get<Needs>(target);
    t.combat = w.get<Combat>(target);
    if (const Stats* st = w.get<Stats>(consumer)) {
        t.healScale = 1.0f + 0.01f * (float)(levelOf(*st, StatId::Medical) - 1);
    }
    const int hpBefore = t.needs ? t.needs->hp : 0, hungerBefore = t.needs ? t.needs->hunger : 0;
    const int restored = applyUse(effects, t);
    std::vector<UseEffect::Stat> shown;
    int attack = 0;
    for (const UseEffect& e : effects) {
        if (e.stat == UseEffect::Stat::Attack) attack += e.amount;
        if (std::find(shown.begin(), shown.end(), e.stat) == shown.end()) shown.push_back(e.stat);
    }
    auto nominal = [&](UseEffect::Stat st) {
        int sum = 0;
        for (const UseEffect& e : effects) sum += e.stat == st ? e.amount : 0;
        return sum;
    };
    auto show = [&](UseEffect::Stat st, const char* labelKey, int change) {
        const bool down = change < 0 || (change == 0 && nominal(st) < 0);
        const std::string num = (down ? "-" : "+") + std::to_string(change < 0 ? -change : change);
        queueUnitLabel(target, langf("MISC_EFFECT_LABEL", { lang(labelKey), num }), down);
    };
    for (UseEffect::Stat st : shown) {
        if (st == UseEffect::Stat::Health && t.needs) show(st, "UILABEL_HEALTH", t.needs->hp - hpBefore);
        else if (st == UseEffect::Stat::Hunger && t.needs) show(st, "UILABEL_HUNGER", t.needs->hunger - hungerBefore);
        else if (st == UseEffect::Stat::Attack && t.combat) show(st, "UILABEL_ATTACK", attack);
    }
    queueUnitLabel(consumer, itemLossLabelFor(1, s.item), true);
    {
        bool hurt = false, food = false;
        for (const UseEffect& e : effects) {
            if (e.amount < 0) hurt = true;
            if (e.stat == UseEffect::Stat::Hunger) food = true;
        }
        queueSoundFrom(w, hurt ? Sfx::Poison : food ? Sfx::Eat : Sfx::Drink, target);
    }
    if (--s.count <= 0) s = ItemStack{};
    grantXp(w, consumer, StatId::Medical, (float)restored, "healing");
    return UseResult::Used;
}

UseResult useStackInSlot(World& w, Entity unit, int slot) {
    return useStackOn(w, unit, unit, slot);
}
