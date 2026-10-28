#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "language.h"

// Behaviors: rules a unit follows on its own. A rule reads a condition, such as its health below 10%, from
// itself, its allies or its enemies, and when the condition holds it carries out an action, such as drinking a
// healing item. Each unit has a list, checked from the top: the first rule is the highest priority. This header
// is the rule data and the pure functions over it, so the tests can check them without a world.

// Whose value a condition or an action looks at. Ally includes the unit itself.
enum class BehaviorTarget : int { Self = 0, Ally, Enemy, Count };
enum class BehaviorCondition : int { Health = 0, Hunger, Count };
enum class BehaviorOp : int { Less = 0, LessEqual, Equal, GreaterEqual, Greater, NotEqual, Count };
enum class BehaviorAction : int { ConsumeHealing = 0, ConsumeFood, Count };

constexpr int BEHAVIOR_LIMIT = 16;   // most rules one unit can hold

struct BehaviorRule {
    bool on = true;
    BehaviorTarget conditionTarget = BehaviorTarget::Self;
    BehaviorCondition condition = BehaviorCondition::Health;
    BehaviorOp op = BehaviorOp::Less;
    int percent = 10;                                        // 0 to 100
    BehaviorAction action = BehaviorAction::ConsumeHealing;
    BehaviorTarget actionTarget = BehaviorTarget::Self;
};

// The rules every unit starts with, both on: drink a healing item at low health, eat when very hungry
inline std::vector<BehaviorRule> defaultBehaviors() {
    BehaviorRule heal;
    BehaviorRule eat;
    eat.condition = BehaviorCondition::Hunger;
    eat.action = BehaviorAction::ConsumeFood;
    return { heal, eat };
}

// The text shown for each option, in the order the enums are declared
inline std::string targetName(BehaviorTarget t) {
    switch (t) {
    case BehaviorTarget::Self: return lang("MISC_TARGET_SELF");
    case BehaviorTarget::Ally: return lang("MISC_TARGET_ALLY");
    case BehaviorTarget::Enemy: return lang("MISC_TARGET_ENEMY");
    case BehaviorTarget::Count: break;
    }
    return "?";
}

inline std::string conditionName(BehaviorCondition c) {
    switch (c) {
    case BehaviorCondition::Health: return lang("MISC_COND_HEALTH");
    case BehaviorCondition::Hunger: return lang("MISC_COND_HUNGER");
    case BehaviorCondition::Count: break;
    }
    return "?";
}

inline const char* opName(BehaviorOp o) {
    switch (o) {
    case BehaviorOp::Less: return "<";
    case BehaviorOp::LessEqual: return "<=";
    case BehaviorOp::Equal: return "=";
    case BehaviorOp::GreaterEqual: return ">=";
    case BehaviorOp::Greater: return ">";
    case BehaviorOp::NotEqual: return "!=";
    case BehaviorOp::Count: break;
    }
    return "?";
}

inline std::string actionName(BehaviorAction a) {
    switch (a) {
    case BehaviorAction::ConsumeHealing: return lang("MISC_ACT_HEAL");
    case BehaviorAction::ConsumeFood: return lang("MISC_ACT_FOOD");
    case BehaviorAction::Count: break;
    }
    return "?";
}

// Whether a value (a whole percent, 0 to 100) meets the operator and the rule's percent
inline bool compareValue(int value, BehaviorOp op, int percent) {
    switch (op) {
    case BehaviorOp::Less: return value < percent;
    case BehaviorOp::LessEqual: return value <= percent;
    case BehaviorOp::Equal: return value == percent;
    case BehaviorOp::GreaterEqual: return value >= percent;
    case BehaviorOp::Greater: return value > percent;
    case BehaviorOp::NotEqual: return value != percent;
    case BehaviorOp::Count: break;
    }
    return false;
}

// A stat as a whole percent of its maximum, 0 when there is no maximum
inline int percentOf(int value, int maximum) {
    if (maximum <= 0) return 0;
    return std::clamp(value * 100 / maximum, 0, 100);
}

// The whole rule as one line, for the hover text. For example:
// "Self Health < 10%  ->  Consume healing item on Self"
inline std::string describeRule(const BehaviorRule& r) {
    return std::string(targetName(r.conditionTarget)) + " " + conditionName(r.condition) + " " +
           opName(r.op) + " " + std::to_string(r.percent) + "%  ->  " + actionName(r.action) + " on " +
           targetName(r.actionTarget);
}

// Builds a rule from the numbers in a save file. Anything out of range is brought into range.
inline BehaviorRule ruleFromFields(int on, int conditionTarget, int condition, int op, int percent,
                                   int action, int actionTarget) {
    auto clampEnum = [](int v, int count) { return std::clamp(v, 0, count - 1); };
    BehaviorRule r;
    r.on = on != 0;
    r.conditionTarget = (BehaviorTarget)clampEnum(conditionTarget, (int)BehaviorTarget::Count);
    r.condition = (BehaviorCondition)clampEnum(condition, (int)BehaviorCondition::Count);
    r.op = (BehaviorOp)clampEnum(op, (int)BehaviorOp::Count);
    r.percent = std::clamp(percent, 0, 100);
    r.action = (BehaviorAction)clampEnum(action, (int)BehaviorAction::Count);
    r.actionTarget = (BehaviorTarget)clampEnum(actionTarget, (int)BehaviorTarget::Count);
    return r;
}

// Moves a rule up (delta -1) or down (delta +1) the priority list. False when it is already at that end.
inline bool moveBehavior(std::vector<BehaviorRule>& rules, size_t index, int delta) {
    if (index >= rules.size()) return false;
    const long to = (long)index + delta;
    if (to < 0 || to >= (long)rules.size()) return false;
    std::swap(rules[index], rules[(size_t)to]);
    return true;
}

// Deletes the rule at index. False if there is no rule there.
inline bool removeBehavior(std::vector<BehaviorRule>& rules, size_t index) {
    if (index >= rules.size()) return false;
    rules.erase(rules.begin() + (long)index);
    return true;
}
