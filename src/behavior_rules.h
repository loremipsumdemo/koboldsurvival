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
//   Eat: below its percent of hunger (BEHAVIOR_NEED_PERCENT unless changed with the row's - and +), it eats the food
//        item that restores the most hunger.
//   Heal: below its percent of health, it uses the healing item that restores the most health.
//   Share: with Eat or Heal needing an item it does not carry, it takes one from a unit within SHARE_RANGE that has
//          Share on too, is of an allied faction, and needs the item less (a higher share of that stat). With no
//          such unit that close but one within SHARE_SEEK_RANGE, it walks over to it first.
//   Rest: below its percent of Rested, it walks to the nearest free bed or bedroll and lies in it until fully rested,
//         then gets up. It is worked by the automation like the automated behaviors, so it waits for orders and fights
//         as they do, but it is a needs behavior: every unit (not a mob) has it, and it comes after Share at first.
//   Leave Bed: lying in a bed or bedroll at full health and fully rested for LEAVE_BED_SECONDS, it gets up.
//   Rescue: one of its own faction lying out cold on the ground (not a corpse, nobody carrying or fetching it) within
//           RESCUE_RANGE: it picks that unit up and carries it to the nearest free bed or bedroll. Worked by the
//           automation like Rest.
// The automated behaviors are added from a prop's right-click menu and can be deleted. They are worked by the unit's
// automation (automationSystem, systems.h), one step at a time, while the unit has nothing else to do:
//   Auto-Harvest: works the nearest resource prop of a kind (a Tree, Ore) inside a claim of the unit's faction.
//   Auto-Haul: takes the items in the unit's inventory (never its worn gear) that a storage prop takes, to that prop.
//              The prop must stand inside a claim of the unit's faction and have room.
//   Patrol: walks about inside the claim it stands in, when that claim is its faction's, pausing PATROL_PAUSE between
//           walks. The units of the other factions have it last on their lists, so they patrol when nothing else
//           needs doing.
//   Roam: wanders the open map, anywhere walkable outside the flags' claims, ROAM_STEP_MIN to ROAM_STEP_MAX tiles at a
//         time with a pause between. Wild mobs (mobs.h) have it.
//   Patrol Spawn: walks about within SPAWN_PATROL_RADIUS of where it was placed (SpawnHome), pausing as Patrol does.
//         Past SPAWN_LEASH_RANGE from there it gives up any chase and walks home (patrolSpawnSystem). The Gorlicorn
//         has it in place of Roam.
// A behavior that is on but can't be done now (no prop with room, no item) waits BEHAVIOR_RETRY_SECONDS before it is
// tried again, and keeps why (BehaviorWait), for its row's hover text. This header is the behavior data and the pure
// functions over it, so they can be checked without a world.

// The numbers are kept in saves, so a new kind goes at the end
enum class BehaviorKind : int { Eat = 0, Heal, Share, AutoHarvest, AutoHaul, Patrol, Roam, Rest, LeaveBed, Rescue, PatrolSpawn, Count };

// Why a behavior could not be done the last time it was tried
enum class BehaviorWait : int {
    None = 0,
    NoItem,          // Eat or Heal: no item to use, and none to share
    WalkingToShare,  // Eat or Heal: on its way to a unit that can share one
    NoProp,          // Auto-Harvest: no prop of its kind in the claim with room for another worker
    CarryFull,       // Auto-Harvest: no room to carry another
    LoadWaiting,     // Auto-Harvest: it carries a load for an Auto-Haul to take first
    PropGone,        // Auto-Haul: its prop is not there any more
    OutsideClaim,    // Auto-Haul: its prop is not in a claim of the unit's faction
    Locked,          // Auto-Haul: its prop is locked to the unit
    NothingToHaul,   // Auto-Haul: the unit carries nothing the prop takes
    PropFull,        // Auto-Haul: the prop has no room for what the unit carries
    NotInClaim,      // Patrol: the unit is not inside a claim of its faction
    NowhereToRoam,   // Roam: no open walkable ground near it outside the claims
    NoBed,           // Rest: no free bed or bedroll it can get into
};

// Patrol and Roam: how far a walk goes, and the pause between walks (real seconds, a random time in the range)
constexpr float PATROL_PAUSE_MIN = 4.0f, PATROL_PAUSE_MAX = 12.0f;
constexpr float ROAM_PAUSE_MIN = 5.0f, ROAM_PAUSE_MAX = 20.0f;
constexpr float ROAM_STEP_MIN = 6.0f, ROAM_STEP_MAX = 18.0f;

constexpr int BEHAVIOR_LIMIT = 16;               // most behaviors one unit can hold
constexpr int BEHAVIOR_NEED_PERCENT = 50;        // Eat and Heal act below this share of hunger or health, at first
constexpr int BEHAVIOR_PERCENT_STEP = 10;        // the row's - and + change it by this much
constexpr int BEHAVIOR_PERCENT_MIN = 10, BEHAVIOR_PERCENT_MAX = 90;
constexpr float BEHAVIOR_RETRY_SECONDS = 1.0f;   // a behavior that could not be done waits this long to try again

struct BehaviorRule {
    bool on = true;
    BehaviorKind kind = BehaviorKind::Eat;
    int propKind = -1;            // Auto-Harvest: the resource kind it works. Auto-Haul: the kind of its prop.
    Entity prop = NULL_ENTITY;    // Auto-Haul: the storage prop it takes items to
    int percent = BEHAVIOR_NEED_PERCENT;   // Eat, Heal and Rest: they act below this share of hunger, health or Rested
    // Not saved: real seconds before a behavior that could not be done is tried again, and why it could not be
    float cooldown = 0.0f;
    BehaviorWait why = BehaviorWait::None;
};

// Whether a behavior has a percent to set: Eat, Heal and Rest
inline bool hasPercent(BehaviorKind k) { return k == BehaviorKind::Eat || k == BehaviorKind::Heal || k == BehaviorKind::Rest; }
// Steps a behavior's percent by delta steps, kept within its range. False when it has no percent or is at that end.
inline bool stepPercent(BehaviorRule& r, int delta) {
    if (!hasPercent(r.kind)) return false;
    const int to = std::clamp(r.percent + delta * BEHAVIOR_PERCENT_STEP, BEHAVIOR_PERCENT_MIN, BEHAVIOR_PERCENT_MAX);
    if (to == r.percent) return false;
    r.percent = to;
    return true;
}

// The text for why a behavior is waiting, or "" when it is not. other: the unit it is walking to, for WalkingToShare.
inline std::string waitReason(const BehaviorRule& r, const std::string& other = std::string()) {
    switch (r.why) {
    case BehaviorWait::None: return std::string();
    case BehaviorWait::NoItem: return lang(r.kind == BehaviorKind::Eat ? "UILABEL_WAIT_NO_FOOD" : "UILABEL_WAIT_NO_HEALING");
    case BehaviorWait::WalkingToShare: return langf("UILABEL_WAIT_WALKING_TO_SHARE", { other });
    case BehaviorWait::NoProp: return langf("UILABEL_WAIT_NO_PROP", { r.propKind >= 0 && r.propKind < PROP_KIND_COUNT ? lang(propInfo(r.propKind).nameKey) : std::string() });
    case BehaviorWait::CarryFull: return lang("UILABEL_WAIT_CARRY_FULL");
    case BehaviorWait::LoadWaiting: return lang("UILABEL_WAIT_LOAD_WAITING");
    case BehaviorWait::PropGone: return lang("UILABEL_WAIT_PROP_GONE");
    case BehaviorWait::OutsideClaim: return lang("UILABEL_WAIT_OUTSIDE_CLAIM");
    case BehaviorWait::Locked: return lang("UILABEL_WAIT_LOCKED");
    case BehaviorWait::NothingToHaul: return lang("UILABEL_WAIT_NOTHING_TO_HAUL");
    case BehaviorWait::PropFull: return lang("UILABEL_WAIT_PROP_FULL");
    case BehaviorWait::NotInClaim: return lang("UILABEL_WAIT_NOT_IN_CLAIM");
    case BehaviorWait::NowhereToRoam: return lang("UILABEL_WAIT_NOWHERE_TO_ROAM");
    case BehaviorWait::NoBed: return lang("UILABEL_WAIT_NO_BED");
    }
    return std::string();
}

// The behaviors every unit starts with, all on: Eat, Heal, Share, Rest, Leave Bed and Rescue
inline std::vector<BehaviorRule> defaultBehaviors() {
    BehaviorRule eat, heal, share, rest, leave, rescue;
    heal.kind = BehaviorKind::Heal;
    share.kind = BehaviorKind::Share;
    rest.kind = BehaviorKind::Rest;
    leave.kind = BehaviorKind::LeaveBed;
    rescue.kind = BehaviorKind::Rescue;
    return { eat, heal, share, rest, leave, rescue };
}

// The automated behaviors, which the automation works and which can be deleted
inline bool isAutomated(BehaviorKind k) {
    return k == BehaviorKind::AutoHarvest || k == BehaviorKind::AutoHaul || k == BehaviorKind::Patrol || k == BehaviorKind::Roam ||
           k == BehaviorKind::PatrolSpawn;
}
// The behaviors the automation works step by step: the automated ones, Rest and Rescue
inline bool isAutoStep(BehaviorKind k) { return isAutomated(k) || k == BehaviorKind::Rest || k == BehaviorKind::Rescue; }
// The automated behaviors whose steps are walks to a spot on the ground, not to a prop
inline bool isWalkBehavior(BehaviorKind k) {
    return k == BehaviorKind::Patrol || k == BehaviorKind::Roam || k == BehaviorKind::PatrolSpawn;
}

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
    case BehaviorKind::Eat: return langf("UILABEL_BEHAVIOR_EAT", { std::to_string(r.percent) });
    case BehaviorKind::Heal: return langf("UILABEL_BEHAVIOR_HEAL", { std::to_string(r.percent) });
    case BehaviorKind::Share: return lang("UILABEL_BEHAVIOR_SHARE");
    case BehaviorKind::AutoHarvest: return langf("UILABEL_BEHAVIOR_AUTO_HARVEST", { propName });
    case BehaviorKind::AutoHaul: return langf("UILABEL_BEHAVIOR_AUTO_HAUL", { propName });
    case BehaviorKind::Patrol: return lang("UILABEL_BEHAVIOR_PATROL");
    case BehaviorKind::Roam: return lang("UILABEL_BEHAVIOR_ROAM");
    case BehaviorKind::Rest: return langf("UILABEL_BEHAVIOR_REST", { std::to_string(r.percent) });
    case BehaviorKind::LeaveBed: return lang("UILABEL_BEHAVIOR_LEAVE_BED");
    case BehaviorKind::Rescue: return lang("UILABEL_BEHAVIOR_RESCUE");
    case BehaviorKind::PatrolSpawn: return lang("UILABEL_BEHAVIOR_PATROL_SPAWN");
    case BehaviorKind::Count: break;
    }
    return "?";
}

// The hover text for a behavior: what it does
inline std::string describeBehavior(const BehaviorRule& r) {
    const std::string propName = behaviorPropName(r);
    switch (r.kind) {
    case BehaviorKind::Eat: return langf("UILABEL_BEHAVIOR_EAT_TIP", { std::to_string(r.percent) });
    case BehaviorKind::Heal: return langf("UILABEL_BEHAVIOR_HEAL_TIP", { std::to_string(r.percent) });
    case BehaviorKind::Share: return lang("UILABEL_BEHAVIOR_SHARE_TIP");
    case BehaviorKind::AutoHarvest: return langf("UILABEL_BEHAVIOR_AUTO_HARVEST_TIP", { propName });
    case BehaviorKind::AutoHaul: return langf("UILABEL_BEHAVIOR_AUTO_HAUL_TIP", { propName });
    case BehaviorKind::Patrol: return lang("UILABEL_BEHAVIOR_PATROL_TIP");
    case BehaviorKind::Roam: return lang("UILABEL_BEHAVIOR_ROAM_TIP");
    case BehaviorKind::Rest: return langf("UILABEL_BEHAVIOR_REST_TIP", { std::to_string(r.percent) });
    case BehaviorKind::LeaveBed: return lang("UILABEL_BEHAVIOR_LEAVE_BED_TIP");
    case BehaviorKind::Rescue: return lang("UILABEL_BEHAVIOR_RESCUE_TIP");
    case BehaviorKind::PatrolSpawn: return lang("UILABEL_BEHAVIOR_PATROL_SPAWN_TIP");
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
inline BehaviorRule behaviorFromFields(int on, int kind, int propKind, int percent = BEHAVIOR_NEED_PERCENT) {
    BehaviorRule r;
    r.on = on != 0;
    r.kind = (BehaviorKind)std::clamp(kind, 0, (int)BehaviorKind::Count - 1);
    r.propKind = isAutomated(r.kind) ? propKind : -1;
    r.percent = std::clamp(percent, BEHAVIOR_PERCENT_MIN, BEHAVIOR_PERCENT_MAX);
    return r;
}

// A mob's behaviors: no needs behaviors at all. It has no hunger, can't use healing items or share, and does not
// sleep in beds (mobs.h); its automated ones (Roam) are added where it is made.
inline std::vector<BehaviorRule> mobBehaviors() {
    return {};
}

// Puts back any needs behavior a list is missing, on, after the last needs behavior it has (so before the automated
// ones), so every unit keeps Eat, Heal, Share and Rest. A mob keeps none: any it has are taken off.
inline void ensureNeedsBehaviors(std::vector<BehaviorRule>& rules, bool mob = false) {
    if (mob) {
        rules.erase(std::remove_if(rules.begin(), rules.end(), [](const BehaviorRule& r) { return !isAutomated(r.kind); }),
                    rules.end());
        return;
    }
    for (const BehaviorRule& d : defaultBehaviors()) {
        bool have = false;
        size_t after = 0;
        for (size_t i = 0; i < rules.size(); ++i) {
            have = have || rules[i].kind == d.kind;
            if (!isAutomated(rules[i].kind)) after = i + 1;
        }
        if (!have) rules.insert(rules.begin() + (long)after, d);
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
