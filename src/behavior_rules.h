#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "ecs.h"
#include "language.h"
#include "props.h"

// Behaviors: what a unit does on its own. Each unit has a list, checked from the top: the first behavior is the
// highest priority. Every unit has the three needs behaviors, which can be switched off and moved but not deleted:
//   Eat: below BEHAVIOR_NEED_PERCENT hunger, it eats the food item that restores the most hunger.
//   Heal: below BEHAVIOR_NEED_PERCENT health, it uses the healing item that restores the most health.
//   Share: with Eat or Heal needing an item it does not carry, it takes one from a unit within SHARE_RANGE that has
//          Share on too, is of an allied faction, and needs the item less (a higher share of that stat).
// The automated behaviors are added from a prop's right-click menu and can be deleted. They are worked by the unit's
// automation (automationSystem, systems.h), one step at a time, while the unit has nothing else to do:
//   Auto-Harvest: works the nearest resource prop of a kind (a Tree, Ore) inside a claim of the unit's faction.
//   Auto-Haul: takes the items in the unit's inventory (never its worn gear) that a storage prop takes, to that prop.
//              The prop must stand inside a claim of the unit's faction and have room.
// A behavior that is on but can't be done now (no prop with room, no item) waits BEHAVIOR_RETRY_SECONDS before it is
// tried again. This header is the behavior data and the pure functions over it, so they can be checked without a world.

enum class BehaviorKind : int { Eat = 0, Heal, Share, AutoHarvest, AutoHaul, Count };

constexpr int BEHAVIOR_LIMIT = 16;               // most behaviors one unit can hold
constexpr int BEHAVIOR_NEED_PERCENT = 50;        // Eat and Heal act below this share of hunger or health
constexpr float BEHAVIOR_RETRY_SECONDS = 1.0f;   // a behavior that could not be done waits this long to try again

struct BehaviorRule {
    bool on = true;
    BehaviorKind kind = BehaviorKind::Eat;
    int propKind = -1;            // Auto-Harvest: the resource kind it works. Auto-Haul: the kind of its prop.
    Entity prop = NULL_ENTITY;    // Auto-Haul: the storage prop it takes items to
    // Not saved: real seconds before a behavior that could not be done is tried again
    float cooldown = 0.0f;
};

// The behaviors every unit starts with, all on: Eat, Heal and Share
inline std::vector<BehaviorRule> defaultBehaviors() {
    BehaviorRule eat, heal, share;
    heal.kind = BehaviorKind::Heal;
    share.kind = BehaviorKind::Share;
    return { eat, heal, share };
}

// The automated behaviors, which the automation works and which can be deleted
inline bool isAutomated(BehaviorKind k) { return k == BehaviorKind::AutoHarvest || k == BehaviorKind::AutoHaul; }

// Whether two behaviors are the same one: Auto-Harvest of the same kind, Auto-Haul to the same prop, or the same
// needs behavior. A unit holds each behavior once.
inline bool sameBehavior(const BehaviorRule& a, const BehaviorRule& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == BehaviorKind::AutoHarvest) return a.propKind == b.propKind;
    if (a.kind == BehaviorKind::AutoHaul) return a.prop == b.prop;
    return true;
}

// The name of an automated behavior's prop kind, in the language in use (empty for the needs behaviors)
inline std::string behaviorPropName(const BehaviorRule& r) {
    if (!isAutomated(r.kind) || r.propKind < 0 || r.propKind >= PROP_KIND_COUNT) return std::string();
    return lang(propInfo(r.propKind).nameKey);
}

// The row text for a behavior
inline std::string behaviorName(const BehaviorRule& r) {
    const std::string propName = behaviorPropName(r);
    switch (r.kind) {
    case BehaviorKind::Eat: return langf("UILABEL_BEHAVIOR_EAT", { std::to_string(BEHAVIOR_NEED_PERCENT) });
    case BehaviorKind::Heal: return langf("UILABEL_BEHAVIOR_HEAL", { std::to_string(BEHAVIOR_NEED_PERCENT) });
    case BehaviorKind::Share: return lang("UILABEL_BEHAVIOR_SHARE");
    case BehaviorKind::AutoHarvest: return langf("UILABEL_BEHAVIOR_AUTO_HARVEST", { propName });
    case BehaviorKind::AutoHaul: return langf("UILABEL_BEHAVIOR_AUTO_HAUL", { propName });
    case BehaviorKind::Count: break;
    }
    return "?";
}

// The hover text for a behavior: what it does
inline std::string describeBehavior(const BehaviorRule& r) {
    const std::string propName = behaviorPropName(r);
    switch (r.kind) {
    case BehaviorKind::Eat: return langf("UILABEL_BEHAVIOR_EAT_TIP", { std::to_string(BEHAVIOR_NEED_PERCENT) });
    case BehaviorKind::Heal: return langf("UILABEL_BEHAVIOR_HEAL_TIP", { std::to_string(BEHAVIOR_NEED_PERCENT) });
    case BehaviorKind::Share: return lang("UILABEL_BEHAVIOR_SHARE_TIP");
    case BehaviorKind::AutoHarvest: return langf("UILABEL_BEHAVIOR_AUTO_HARVEST_TIP", { propName });
    case BehaviorKind::AutoHaul: return langf("UILABEL_BEHAVIOR_AUTO_HAUL_TIP", { propName });
    case BehaviorKind::Count: break;
    }
    return "?";
}

// A stat as a whole percent of its maximum, 0 when there is no maximum
inline int percentOf(int value, int maximum) {
    if (maximum <= 0) return 0;
    return std::clamp(value * 100 / maximum, 0, 100);
}

// Builds a behavior from the numbers in a save file. Anything out of range is brought into range. The prop of an
// Auto-Haul is found by the caller.
inline BehaviorRule behaviorFromFields(int on, int kind, int propKind) {
    BehaviorRule r;
    r.on = on != 0;
    r.kind = (BehaviorKind)std::clamp(kind, 0, (int)BehaviorKind::Count - 1);
    r.propKind = isAutomated(r.kind) ? propKind : -1;
    return r;
}

// Puts back any needs behavior a list is missing, at the end and on, so every unit keeps Eat, Heal and Share
inline void ensureNeedsBehaviors(std::vector<BehaviorRule>& rules) {
    for (const BehaviorRule& d : defaultBehaviors()) {
        bool have = false;
        for (const BehaviorRule& r : rules) have = have || r.kind == d.kind;
        if (!have) rules.push_back(d);
    }
}

// Adds a behavior to the end of a list, unless the list already holds it (then it is only switched on) or is full.
// False when it is full.
inline bool addBehavior(std::vector<BehaviorRule>& rules, const BehaviorRule& add) {
    for (BehaviorRule& r : rules) {
        if (sameBehavior(r, add)) {
            r.on = true;
            r.cooldown = 0.0f;
            return true;
        }
    }
    if ((int)rules.size() >= BEHAVIOR_LIMIT) return false;
    rules.push_back(add);
    return true;
}

// Moves a behavior up (delta -1) or down (delta +1) the priority list. False when it is already at that end.
inline bool moveBehavior(std::vector<BehaviorRule>& rules, size_t index, int delta) {
    if (index >= rules.size()) return false;
    const long to = (long)index + delta;
    if (to < 0 || to >= (long)rules.size()) return false;
    std::swap(rules[index], rules[(size_t)to]);
    return true;
}

// Deletes the behavior at index. False if there is none there, or it is a needs behavior, which every unit keeps.
inline bool removeBehavior(std::vector<BehaviorRule>& rules, size_t index) {
    if (index >= rules.size() || !isAutomated(rules[index].kind)) return false;
    rules.erase(rules.begin() + (long)index);
    return true;
}
