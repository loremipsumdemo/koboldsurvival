#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>
#include "ecs.h"
#include "language.h"
#include "props.h"

// Saved as numbers: after reordering, raise SAVE_VERSION
enum class BehaviorKind : int { Eat = 0, Heal, Share, AutoHarvest, AutoHaul, Patrol, Roam, Rest, LeaveBed, Rescue, PatrolSpawn, Count };

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
};

constexpr float PATROL_PAUSE_MIN = 4.0f, PATROL_PAUSE_MAX = 12.0f;
constexpr float ROAM_PAUSE_MIN = 5.0f, ROAM_PAUSE_MAX = 20.0f;
constexpr float ROAM_STEP_MIN = 6.0f, ROAM_STEP_MAX = 18.0f;

constexpr int BEHAVIOR_LIMIT = 16;
constexpr int BEHAVIOR_NEED_PERCENT = 50;
constexpr float BEHAVIOR_RETRY_SECONDS = 1.0f;
constexpr float NO_BED_RETRY_SECONDS = 10.0f;

struct BehaviorRule {
    bool on = true;
    BehaviorKind kind = BehaviorKind::Eat;
    int propKind = -1;
    Entity prop = NULL_ENTITY;
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
    }
    return std::string();
}

inline std::vector<BehaviorRule> defaultBehaviors() {
    BehaviorRule eat, heal, share, rest, leave, rescue;
    heal.kind = BehaviorKind::Heal;
    share.kind = BehaviorKind::Share;
    rest.kind = BehaviorKind::Rest;
    leave.kind = BehaviorKind::LeaveBed;
    rescue.kind = BehaviorKind::Rescue;
    return { eat, heal, share, rescue, rest, leave };
}

inline bool isAutomated(BehaviorKind k) {
    return k == BehaviorKind::AutoHarvest || k == BehaviorKind::AutoHaul || k == BehaviorKind::Patrol || k == BehaviorKind::Roam ||
           k == BehaviorKind::PatrolSpawn;
}
inline bool isAutoStep(BehaviorKind k) { return isAutomated(k) || k == BehaviorKind::Rest || k == BehaviorKind::Rescue; }
inline bool isWalkBehavior(BehaviorKind k) {
    return k == BehaviorKind::Patrol || k == BehaviorKind::Roam || k == BehaviorKind::PatrolSpawn;
}

inline bool sameBehavior(const BehaviorRule& a, const BehaviorRule& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == BehaviorKind::AutoHarvest) return a.propKind == b.propKind;
    if (a.kind == BehaviorKind::AutoHaul) return a.prop == b.prop;
    return true;
}

inline std::string behaviorPropName(const BehaviorRule& r) {
    if (!isAutomated(r.kind) || r.propKind < 0 || r.propKind >= propKindCount()) return std::string();
    return lang(propInfo((PropKind)r.propKind).nameKey);
}

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

inline int percentOf(int value, int maximum) {
    if (maximum <= 0) return 0;
    return std::clamp(value * 100 / maximum, 0, 100);
}

inline BehaviorRule behaviorFromFields(int on, int kind, int propKind, int percent = BEHAVIOR_NEED_PERCENT) {
    BehaviorRule r;
    r.on = on != 0;
    r.kind = (BehaviorKind)std::clamp(kind, 0, (int)BehaviorKind::Count - 1);
    r.propKind = isAutomated(r.kind) ? propKind : -1;
    const PercentRange range = percentRange(r.kind);
    r.percent = hasPercent(r.kind) ? std::clamp(percent, range.lo, range.hi) : BEHAVIOR_NEED_PERCENT;
    return r;
}

inline std::vector<BehaviorRule> exileBehaviors() {
    BehaviorRule eat, heal, roam;
    heal.kind = BehaviorKind::Heal;
    roam.kind = BehaviorKind::Roam;
    return { eat, heal, roam };
}
inline std::vector<BehaviorRule> mobBehaviors() {
    return {};
}

inline bool addBehavior(std::vector<BehaviorRule>& rules, const BehaviorRule& add) {
    for (BehaviorRule& r : rules) {
        if (sameBehavior(r, add)) {
            r.on = true;
            r.cooldown = 0.0f;
            return true;
        }
    }
    if ((int)rules.size() >= BEHAVIOR_LIMIT) return false;
    if (isAutomated(add.kind)) {
        rules.push_back(add);
        return true;
    }
    size_t after = 0;
    for (size_t i = 0; i < rules.size(); ++i) {
        if (!isAutomated(rules[i].kind)) after = i + 1;
    }
    rules.insert(rules.begin() + (long)after, add);
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
                BehaviorKind::Rescue };
    }
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
