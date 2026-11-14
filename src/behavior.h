#pragma once
#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "ecs.h"
#include "language.h"
#include "props.h"

// Saves name a behavior by its key, so one can be added or moved
enum class BehaviorKind : int { Eat = 0, Heal, Share, AutoHarvest, AutoHaul, Patrol, Roam, Rest, LeaveBed, Rescue, PatrolSpawn, AutoHaulBones,
                                AutoSupply, AutoConstruct, Group, Sleep, AutoHaulGround, AutoHaulStall, ManStall, DealWithDowned, AutoCraft, Socialize, SitDown, Play, Dance, ClearJunk, Count };
inline constexpr const char* BEHAVIOR_KEYS[] = { "Eat", "Heal", "Share", "AutoHarvest", "AutoHaul", "Patrol", "Roam", "Rest", "LeaveBed", "Rescue", "PatrolSpawn",
                                                 "AutoHaulBones", "AutoSupply", "AutoConstruct", "Group", "Sleep", "AutoHaulGround",
                                                 "AutoHaulStall", "ManStall", "DealWithDowned", "AutoCraft", "Socialize", "SitDown", "Play", "Dance", "ClearJunk" };
// Behavior groups (behavior_groups.h): a Group task stands for the whole task list of another group or of a Default
// Behavior, at its place in the list. Its propKind names which: a group's index, or one of the two refs below.
inline constexpr int GROUP_REF_MILITARY = 1000, GROUP_REF_CITIZEN = 1001;
// Building: Auto-Construct brings your blueprints their material and builds them (AutoSupply is the older name for it)
inline bool isBuildTask(BehaviorKind k) { return k == BehaviorKind::AutoSupply || k == BehaviorKind::AutoConstruct; }
// Hauling to one prop: AutoHaul carries what it takes (to an Altar, only its fuel), AutoHaulBones carries bones to an Altar
inline bool isHaul(BehaviorKind k) { return k == BehaviorKind::AutoHaul || k == BehaviorKind::AutoHaulBones; }
// A task tied to one prop, which saves name: a haul's store, or the Stall a shopkeeper mans
inline bool ruleNamesProp(BehaviorKind k) { return isHaul(k) || k == BehaviorKind::ManStall; }
static_assert(std::size(BEHAVIOR_KEYS) == (size_t)BehaviorKind::Count);
inline const char* behaviorKey(BehaviorKind k) { return BEHAVIOR_KEYS[std::clamp((int)k, 0, (int)BehaviorKind::Count - 1)]; }
// -1 if no behavior has this key
inline int behaviorNamed(const std::string& key) {
    for (int i = 0; i < (int)BehaviorKind::Count; ++i) {
        if (key == BEHAVIOR_KEYS[i]) return i;
    }
    return -1;
}

enum class BehaviorWait : int {
    None = 0,
    NoItem,
    WalkingToShare,
    NoProp,
    CarryFull,
    LoadWaiting,
    PropGone,
    OutsideClaim,
    Locked,
    NothingToHaul,
    PropFull,
    NotInClaim,
    NowhereToRoam,
    NoBed,
    NoBlueprint,
    NoLogs,
    NoStockpile,
    NoStallOrder,
    NoDowned,        // Deal with Downed: no knocked-out enemy near, or the faction's rules leave them be
    NoCraftOrder,    // Auto-Craft: no Crafting Order is short of stock with its ingredients in your stores
    NoSeat,          // Sit Down: no free seat in reach
    NoCompany,       // Socialize: no one of its faction is at recreation near it
    NoTable,         // Playing: no Game Table with a free place near
    NoDancePlace,    // Dancing: no Campfire or Altar of its faction with room to dance round
};

constexpr float PATROL_PAUSE_MIN = 4.0f, PATROL_PAUSE_MAX = 12.0f;
constexpr float ROAM_PAUSE_MIN = 5.0f, ROAM_PAUSE_MAX = 20.0f;
constexpr float ROAM_STEP_MIN = 6.0f, ROAM_STEP_MAX = 18.0f;

constexpr int BEHAVIOR_LIMIT = 16;
constexpr int BEHAVIOR_TASKS_MAX = 256;   // the longest task list: a behavior group's, and so a citizen's that follows one
constexpr int BEHAVIOR_NEED_PERCENT = 50;
constexpr float BEHAVIOR_RETRY_SECONDS = 1.0f;
// With no bed to be had (none free, or the one chosen could not be reached), the Sleep and Rest tasks are not looked at again for an
// in-game hour: a day is 30 real minutes (calendar.h), so an hour is 75 seconds
constexpr float NO_BED_RETRY_SECONDS = 75.0f;

struct BehaviorRule {
    bool on = true;
    BehaviorKind kind = BehaviorKind::Eat;
    int propKind = -1;
    ObjectId prop = NO_OBJECT;
    int percent = BEHAVIOR_NEED_PERCENT;
    float cooldown = 0.0f;
    BehaviorWait why = BehaviorWait::None;
};

inline bool hasPercent(BehaviorKind k) { return k == BehaviorKind::Eat || k == BehaviorKind::Heal || k == BehaviorKind::Rest; }
struct PercentRange {
    int lo = 0, hi = 0;
};
inline PercentRange percentRange(BehaviorKind k) {
    switch (k) {
    case BehaviorKind::Eat: return { 5, 95 };
    case BehaviorKind::Heal: return { 10, 95 };
    case BehaviorKind::Rest: return { 5, 90 };
    default: break;
    }
    return { BEHAVIOR_NEED_PERCENT, BEHAVIOR_NEED_PERCENT };
}
inline bool percentInRange(BehaviorKind k, int value) {
    const PercentRange r = percentRange(k);
    return hasPercent(k) && value >= r.lo && value <= r.hi;
}
inline bool setPercent(BehaviorRule& r, int value) {
    if (!percentInRange(r.kind, value) || value == r.percent) return false;
    r.percent = value;
    return true;
}

inline std::string waitReason(const BehaviorRule& r, const std::string& other = std::string()) {
    switch (r.why) {
    case BehaviorWait::None: return std::string();
    case BehaviorWait::NoItem: return lang(r.kind == BehaviorKind::Eat ? "UILABEL_WAIT_NO_FOOD" : "UILABEL_WAIT_NO_HEALING");
    case BehaviorWait::WalkingToShare: return langf("UILABEL_WAIT_WALKING_TO_SHARE", { other });
    case BehaviorWait::NoProp: return langf("UILABEL_WAIT_NO_PROP", { r.propKind >= 0 && r.propKind < propKindCount() ? lang(propInfo((PropKind)r.propKind).nameKey) : std::string() });
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
    case BehaviorWait::NoBlueprint: return lang("UILABEL_WAIT_NO_BLUEPRINT");
    case BehaviorWait::NoLogs: return lang("UILABEL_WAIT_NO_LOGS");
    case BehaviorWait::NoStockpile: return lang("UILABEL_WAIT_NO_STOCKPILE");
    case BehaviorWait::NoStallOrder: return lang("UILABEL_WAIT_NO_STALL_ORDER");
    case BehaviorWait::NoDowned: return lang("UILABEL_WAIT_NO_DOWNED");
    case BehaviorWait::NoCraftOrder: return lang("UILABEL_WAIT_NO_CRAFT_ORDER");
    case BehaviorWait::NoSeat: return lang("UILABEL_WAIT_NO_SEAT");
    case BehaviorWait::NoCompany: return lang("UILABEL_WAIT_NO_COMPANY");
    case BehaviorWait::NoTable: return lang("UILABEL_WAIT_NO_TABLE");
    case BehaviorWait::NoDancePlace: return lang("UILABEL_WAIT_NO_DANCE_PLACE");
    }
    return std::string();
}

inline std::vector<BehaviorRule> defaultBehaviors() {
    BehaviorRule eat, heal, share, rest, leave, rescue, stall, deal;
    heal.kind = BehaviorKind::Heal;
    share.kind = BehaviorKind::Share;
    rest.kind = BehaviorKind::Rest;
    leave.kind = BehaviorKind::LeaveBed;
    rescue.kind = BehaviorKind::Rescue;
    stall.kind = BehaviorKind::AutoHaulStall;   // on by default: carries out the orders picked for your Stalls
    deal.kind = BehaviorKind::DealWithDowned;   // does what your Faction Rules say; with the first rules, nothing
    return { eat, heal, share, rescue, rest, leave, deal, stall };
}

inline bool isAutomated(BehaviorKind k) {
    return k == BehaviorKind::AutoHarvest || isHaul(k) || k == BehaviorKind::Patrol || k == BehaviorKind::Roam ||
           k == BehaviorKind::PatrolSpawn || isBuildTask(k) || k == BehaviorKind::AutoHaulGround ||
           k == BehaviorKind::AutoHaulStall || k == BehaviorKind::ManStall ||
           k == BehaviorKind::DealWithDowned || k == BehaviorKind::AutoCraft || k == BehaviorKind::Socialize || k == BehaviorKind::SitDown ||
           k == BehaviorKind::Play || k == BehaviorKind::Dance || k == BehaviorKind::ClearJunk;
}
// Lying in a bed: Rest until rested, Sleep until morning
inline bool isBedStep(BehaviorKind k) { return k == BehaviorKind::Rest || k == BehaviorKind::Sleep; }
inline bool isAutoStep(BehaviorKind k) { return isAutomated(k) || isBedStep(k) || k == BehaviorKind::Rescue; }
inline bool isWalkBehavior(BehaviorKind k) {
    return k == BehaviorKind::Patrol || k == BehaviorKind::Roam || k == BehaviorKind::PatrolSpawn;
}

inline bool sameBehavior(const BehaviorRule& a, const BehaviorRule& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == BehaviorKind::AutoHarvest || a.kind == BehaviorKind::Group) return a.propKind == b.propKind;
    if (ruleNamesProp(a.kind)) return a.prop == b.prop;
    return true;
}

inline std::string behaviorPropName(const BehaviorRule& r) {
    if (!isAutomated(r.kind) || r.kind == BehaviorKind::AutoHaulStall || r.propKind < 0 || r.propKind >= propKindCount()) return std::string();
    return lang(propInfo((PropKind)r.propKind).nameKey);
}

// "Group 3", "Military Default" or "Citizen Default"
inline std::string groupRefName(int ref) {
    if (ref == GROUP_REF_MILITARY) return lang("UILABEL_GROUP_MILITARY_DEFAULT");
    if (ref == GROUP_REF_CITIZEN) return lang("UILABEL_GROUP_CITIZEN_DEFAULT");
    return langf("UILABEL_GROUP_NAME", { std::to_string(ref + 1) });
}

inline std::string behaviorName(const BehaviorRule& r) {
    const std::string propName = behaviorPropName(r);
    switch (r.kind) {
    case BehaviorKind::Eat: return langf("UILABEL_BEHAVIOR_EAT", { std::to_string(r.percent) });
    case BehaviorKind::Heal: return langf("UILABEL_BEHAVIOR_HEAL", { std::to_string(r.percent) });
    case BehaviorKind::Share: return lang("UILABEL_BEHAVIOR_SHARE");
    case BehaviorKind::AutoHarvest: return langf("UILABEL_BEHAVIOR_AUTO_HARVEST", { propName });
    case BehaviorKind::AutoHaul:
        return langf(r.propKind == (int)PropKind::Altar ? "UILABEL_BEHAVIOR_AUTO_HAUL_LOGS" : "UILABEL_BEHAVIOR_AUTO_HAUL", { propName });
    case BehaviorKind::AutoHaulBones: return langf("UILABEL_BEHAVIOR_AUTO_HAUL_BONES", { propName });
    case BehaviorKind::Patrol: return lang("UILABEL_BEHAVIOR_PATROL");
    case BehaviorKind::Roam: return lang("UILABEL_BEHAVIOR_ROAM");
    case BehaviorKind::Rest: return langf("UILABEL_BEHAVIOR_REST", { std::to_string(r.percent) });
    case BehaviorKind::LeaveBed: return lang("UILABEL_BEHAVIOR_LEAVE_BED");
    case BehaviorKind::Rescue: return lang("UILABEL_BEHAVIOR_RESCUE");
    case BehaviorKind::PatrolSpawn: return lang("UILABEL_BEHAVIOR_PATROL_SPAWN");
    case BehaviorKind::AutoSupply: return lang("UILABEL_BEHAVIOR_AUTO_SUPPLY");
    case BehaviorKind::AutoConstruct: return lang("UILABEL_BEHAVIOR_AUTO_CONSTRUCT");
    case BehaviorKind::Group: return langf("UILABEL_BEHAVIOR_GROUP", { groupRefName(r.propKind) });
    case BehaviorKind::Sleep: return lang("UILABEL_BEHAVIOR_SLEEP");
    case BehaviorKind::AutoHaulGround: return lang("UILABEL_BEHAVIOR_AUTO_HAUL_GROUND");
    case BehaviorKind::AutoHaulStall: return lang("UILABEL_BEHAVIOR_AUTO_HAUL_STALL");
    case BehaviorKind::ManStall: return langf("UILABEL_BEHAVIOR_MAN_STALL", { propName });
    case BehaviorKind::DealWithDowned: return lang("UILABEL_BEHAVIOR_DEAL_WITH_DOWNED");
    case BehaviorKind::AutoCraft: return lang("UILABEL_BEHAVIOR_AUTO_CRAFT");
    case BehaviorKind::Socialize: return lang("UILABEL_BEHAVIOR_SOCIALIZE");
    case BehaviorKind::SitDown: return lang("UILABEL_BEHAVIOR_SIT_DOWN");
    case BehaviorKind::Play: return lang("UILABEL_BEHAVIOR_PLAY");
    case BehaviorKind::Dance: return lang("UILABEL_BEHAVIOR_DANCE");
    case BehaviorKind::ClearJunk: return lang("UILABEL_BEHAVIOR_CLEAR_JUNK");
    case BehaviorKind::Count: break;
    }
    return "?";
}

inline std::string describeBehavior(const BehaviorRule& r) {
    const std::string propName = behaviorPropName(r);
    switch (r.kind) {
    case BehaviorKind::Eat: return langf("UILABEL_BEHAVIOR_EAT_TIP", { std::to_string(r.percent) });
    case BehaviorKind::Heal: return langf("UILABEL_BEHAVIOR_HEAL_TIP", { std::to_string(r.percent) });
    case BehaviorKind::Share: return lang("UILABEL_BEHAVIOR_SHARE_TIP");
    case BehaviorKind::AutoHarvest: return langf("UILABEL_BEHAVIOR_AUTO_HARVEST_TIP", { propName });
    case BehaviorKind::AutoHaul:
        return langf(r.propKind == (int)PropKind::Altar ? "UILABEL_BEHAVIOR_AUTO_HAUL_LOGS_TIP" : "UILABEL_BEHAVIOR_AUTO_HAUL_TIP", { propName });
    case BehaviorKind::AutoHaulBones: return langf("UILABEL_BEHAVIOR_AUTO_HAUL_BONES_TIP", { propName });
    case BehaviorKind::Patrol: return lang("UILABEL_BEHAVIOR_PATROL_TIP");
    case BehaviorKind::Roam: return lang("UILABEL_BEHAVIOR_ROAM_TIP");
    case BehaviorKind::Rest: return langf("UILABEL_BEHAVIOR_REST_TIP", { std::to_string(r.percent) });
    case BehaviorKind::LeaveBed: return lang("UILABEL_BEHAVIOR_LEAVE_BED_TIP");
    case BehaviorKind::Rescue: return lang("UILABEL_BEHAVIOR_RESCUE_TIP");
    case BehaviorKind::PatrolSpawn: return lang("UILABEL_BEHAVIOR_PATROL_SPAWN_TIP");
    case BehaviorKind::AutoSupply: return lang("UILABEL_BEHAVIOR_AUTO_SUPPLY_TIP");
    case BehaviorKind::AutoConstruct: return lang("UILABEL_BEHAVIOR_AUTO_CONSTRUCT_TIP");
    case BehaviorKind::Group: return langf("UILABEL_BEHAVIOR_GROUP_TIP", { groupRefName(r.propKind) });
    case BehaviorKind::Sleep: return lang("UILABEL_BEHAVIOR_SLEEP_TIP");
    case BehaviorKind::AutoHaulGround: return lang("UILABEL_BEHAVIOR_AUTO_HAUL_GROUND_TIP");
    case BehaviorKind::AutoHaulStall: return lang("UILABEL_BEHAVIOR_AUTO_HAUL_STALL_TIP");
    case BehaviorKind::ManStall: return langf("UILABEL_BEHAVIOR_MAN_STALL_TIP", { propName });
    case BehaviorKind::DealWithDowned: return lang("UILABEL_BEHAVIOR_DEAL_WITH_DOWNED_TIP");
    case BehaviorKind::AutoCraft: return lang("UILABEL_BEHAVIOR_AUTO_CRAFT_TIP");
    case BehaviorKind::Socialize: return lang("UILABEL_BEHAVIOR_SOCIALIZE_TIP");
    case BehaviorKind::SitDown: return lang("UILABEL_BEHAVIOR_SIT_DOWN_TIP");
    case BehaviorKind::Play: return lang("UILABEL_BEHAVIOR_PLAY_TIP");
    case BehaviorKind::Dance: return lang("UILABEL_BEHAVIOR_DANCE_TIP");
    case BehaviorKind::ClearJunk: return lang("UILABEL_BEHAVIOR_CLEAR_JUNK_TIP");
    case BehaviorKind::Count: break;
    }
    return "?";
}

inline int percentOf(int value, int maximum) {
    if (maximum <= 0) return 0;
    return std::clamp(value * 100 / maximum, 0, 100);
}

inline BehaviorRule behaviorFromFields(int on, int kind, int propKind, int percent = BEHAVIOR_NEED_PERCENT) {
    BehaviorRule r;
    r.on = on != 0;
    r.kind = (BehaviorKind)std::clamp(kind, 0, (int)BehaviorKind::Count - 1);
    r.propKind = (isAutomated(r.kind) && r.kind != BehaviorKind::AutoHaulStall) || r.kind == BehaviorKind::Group ? propKind : -1;
    const PercentRange range = percentRange(r.kind);
    r.percent = hasPercent(r.kind) ? std::clamp(percent, range.lo, range.hi) : BEHAVIOR_NEED_PERCENT;
    return r;
}

inline std::vector<BehaviorRule> factionlessBehaviors() {
    BehaviorRule eat, heal, deal, roam;
    heal.kind = BehaviorKind::Heal;
    deal.kind = BehaviorKind::DealWithDowned;
    roam.kind = BehaviorKind::Roam;
    return { eat, heal, deal, roam };
}
// A unit of a faction that runs itself: rescues and deals with the knocked out as its faction's rules say, and patrols
inline std::vector<BehaviorRule> npcFactionBehaviors() {
    BehaviorRule rescue, deal, patrol, sleep, social, sit, play, dance;
    play.kind = BehaviorKind::Play;
    dance.kind = BehaviorKind::Dance;
    rescue.kind = BehaviorKind::Rescue;
    deal.kind = BehaviorKind::DealWithDowned;
    patrol.kind = BehaviorKind::Patrol;
    sleep.kind = BehaviorKind::Sleep;
    social.kind = BehaviorKind::Socialize;
    sit.kind = BehaviorKind::SitDown;
    return { rescue, deal, sleep, patrol, social, sit, play, dance };
}
// A military unit of a faction that runs itself: its needs, sleeping, and roaming
inline std::vector<BehaviorRule> npcMilitaryBehaviors() {
    BehaviorRule rescue, deal, sleep, roam;
    rescue.kind = BehaviorKind::Rescue;
    deal.kind = BehaviorKind::DealWithDowned;
    sleep.kind = BehaviorKind::Sleep;
    roam.kind = BehaviorKind::Roam;
    return { rescue, deal, sleep, roam };
}
inline std::vector<BehaviorRule> mobBehaviors() {
    return {};
}

// Producing tasks: what they make is carried off by the haul tasks, which therefore rank above them
// What kind of task it is, in the order of priority a new task is sorted into: group tasks first, then needs,
// building, hauling, harvesting, and movement last. The Add Task list shows its headings in this order too.
enum class TaskCategory : int { Groups = 0, Needs, Building, Hauling, Harvesting, Recreation, Movement, Count };
inline constexpr const char* TASK_CATEGORY_KEYS[] = { "UILABEL_TASKS_GROUPS", "UILABEL_TASKS_NEEDS", "UILABEL_TASKS_BUILDING",
                                                      "UILABEL_TASKS_HAULING", "UILABEL_TASKS_HARVESTING", "UILABEL_TASKS_RECREATION", "UILABEL_TASKS_MOVEMENT" };
static_assert(std::size(TASK_CATEGORY_KEYS) == (size_t)TaskCategory::Count);
inline TaskCategory taskCategory(BehaviorKind k) {
    switch (k) {
    case BehaviorKind::Group: return TaskCategory::Groups;
    case BehaviorKind::Patrol: case BehaviorKind::Roam: case BehaviorKind::PatrolSpawn: return TaskCategory::Movement;
    case BehaviorKind::AutoSupply: case BehaviorKind::AutoConstruct: case BehaviorKind::ClearJunk: return TaskCategory::Building;
    case BehaviorKind::AutoHarvest: case BehaviorKind::AutoCraft: return TaskCategory::Harvesting;
    case BehaviorKind::AutoHaul: case BehaviorKind::AutoHaulBones: case BehaviorKind::AutoHaulGround: case BehaviorKind::AutoHaulStall:
        return TaskCategory::Hauling;
    case BehaviorKind::ManStall: return TaskCategory::Movement;
    case BehaviorKind::Socialize: case BehaviorKind::SitDown: case BehaviorKind::Play: case BehaviorKind::Dance: return TaskCategory::Recreation;
    default: return TaskCategory::Needs;
    }
}


// Adds a task, or switches on the one it has already. False once the list holds limit tasks. A new task goes in by its
// category: below every task of its own or an earlier category, above the first of a later one (so hauls go above
// harvesting, and both above movement). A haul added again that sits below a later category's task moves up likewise.
inline bool addBehavior(std::vector<BehaviorRule>& rules, const BehaviorRule& add, int limit = BEHAVIOR_LIMIT) {
    const int rank = (int)taskCategory(add.kind);
    // Within hauling, hauls to a store go above Auto-Haul Ground Items, so what is carried is put away before more is fetched
    auto later = [&](const BehaviorRule& x) {
        const int rx = (int)taskCategory(x.kind);
        return rx > rank || (rx == rank && isHaul(add.kind) && x.kind == BehaviorKind::AutoHaulGround);
    };
    auto spot = [&](size_t end) {
        for (size_t i = 0; i < end; ++i) {
            if (later(rules[i])) return i;
        }
        return end;
    };
    for (size_t i = 0; i < rules.size(); ++i) {
        if (!sameBehavior(rules[i], add)) continue;
        rules[i].on = true;
        rules[i].cooldown = 0.0f;
        if (isHaul(add.kind)) {
            const size_t to = spot(i);
            if (to < i) {
                const BehaviorRule moved = rules[i];
                rules.erase(rules.begin() + (long)i);
                rules.insert(rules.begin() + (long)to, moved);
            }
        }
        return true;
    }
    if ((int)rules.size() >= limit) return false;
    rules.insert(rules.begin() + (long)spot(rules.size()), add);
    return true;
}


struct BehaviorOption {
    BehaviorRule rule;
    std::string label;
    bool have = false;
};

inline std::vector<BehaviorKind> addableKinds(bool mob) {
    std::vector<BehaviorKind> out;
    if (!mob) {
        out = { BehaviorKind::Eat, BehaviorKind::Heal, BehaviorKind::Share, BehaviorKind::Rest, BehaviorKind::LeaveBed,
                BehaviorKind::Rescue, BehaviorKind::Sleep, BehaviorKind::AutoHaulGround, BehaviorKind::AutoHaulStall,
                BehaviorKind::DealWithDowned, BehaviorKind::AutoCraft, BehaviorKind::Patrol, BehaviorKind::Roam,
                BehaviorKind::Socialize, BehaviorKind::SitDown, BehaviorKind::Play, BehaviorKind::Dance, BehaviorKind::ClearJunk };
        return out;
    }
    // Patrol Spawn (keeping near where it appeared) is for wild mobs
    out.insert(out.end(), { BehaviorKind::Patrol, BehaviorKind::Roam, BehaviorKind::PatrolSpawn });
    return out;
}

inline bool holdsBehavior(const std::vector<BehaviorRule>& rules, const BehaviorRule& r) {
    return std::any_of(rules.begin(), rules.end(), [&](const BehaviorRule& x) { return sameBehavior(x, r); });
}

inline bool moveBehavior(std::vector<BehaviorRule>& rules, size_t index, int delta) {
    if (index >= rules.size()) return false;
    const long to = (long)index + delta;
    if (to < 0 || to >= (long)rules.size()) return false;
    std::swap(rules[index], rules[(size_t)to]);
    return true;
}

inline bool removeBehavior(std::vector<BehaviorRule>& rules, size_t index) {
    if (index >= rules.size()) return false;
    rules.erase(rules.begin() + (long)index);
    return true;
}
