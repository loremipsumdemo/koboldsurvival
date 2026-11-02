#include "behavior.h"
#include <string>
#include <vector>
#include "behavior_rules.h"
#include "components.h"
#include "items.h"
#include "useitem.h"

namespace {

// A unit's faction (factions.h); the Player Faction for one with none
int factionIdOf(World& w, Entity e) {
    if (const Faction* f = w.get<Faction>(e)) return f->id;
    if (w.has<Commandable>(e)) return FACTION_PLAYER;
    return w.has<Hostile>(e) ? FACTION_RED : FACTION_GREEN;
}
bool provokedAgainst(World& w, Entity e, int faction) {
    const Provoked* p = w.get<Provoked>(e);
    if (!p) return false;
    for (const Provoked::Entry& en : p->against) {
        if (en.faction == faction && en.secondsLeft > 0.0f) return true;
    }
    return false;
}

// Whether unit c is in the group a rule names, as seen from the unit that owns the rule. Ally includes the unit
// itself, its faction and allied factions (reputation REP_ALLIED or more); Enemy is a unit it fights (its faction is
// hostile, or one has provoked the other); Self is only the unit.
bool inGroup(World& w, Entity actor, BehaviorTarget group, Entity c) {
    const int fa = factionIdOf(w, actor), fc = factionIdOf(w, c);
    switch (group) {
    case BehaviorTarget::Self:
        return c == actor;
    case BehaviorTarget::Ally:
        return c == actor || factionsAllied(fa, fc);
    case BehaviorTarget::Enemy:
        return c != actor && fa != fc &&
               (factionsHostile(fa, fc) || provokedAgainst(w, actor, fc) || provokedAgainst(w, c, fa));
    case BehaviorTarget::Count:
        break;
    }
    return false;
}

// The value a condition reads from a unit's needs, as a whole percent
int valueOf(const Needs& n, BehaviorCondition c) {
    return c == BehaviorCondition::Health ? percentOf(n.hp, n.maxHp) : percentOf(n.hunger, n.maxHunger);
}

// Carries out a consumable action: the actor's item that restores the most of the stat, used on the target. False if
// the actor has no such item, so nothing happened.
bool carryOut(World& w, Entity actor, Entity target, BehaviorAction action, CombatLog& log) {
    const bool food = action == BehaviorAction::ConsumeFood;
    const UseEffect::Stat stat = food ? UseEffect::Stat::Hunger : UseEffect::Stat::Health;

    const Inventory* inv = w.get<Inventory>(actor);
    if (!inv) return false;
    int best = -1;
    int bestAmount = 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const ItemStack& s = inv->slots[i];
        if (s.item == 0 || s.count <= 0) continue;
        const ItemDef* def = findItem(s.item);
        if (!def) continue;
        const int amount = restoreAmount(*def, stat);
        if (amount > bestAmount) {
            best = i;
            bestAmount = amount;
        }
    }
    if (best < 0) return false;

    const std::string itemName = itemDisplayName(*findItem(inv->slots[best].item));
    const Needs* before = w.get<Needs>(target);
    const int was = before ? (food ? before->hunger : before->hp) : 0;
    if (useStackOn(w, actor, target, best) != UseResult::Used) return false;

    const Needs* after = w.get<Needs>(target);
    const int now = after ? (food ? after->hunger : after->hp) : 0;
    const int max = after ? (food ? after->maxHunger : after->maxHp) : 0;
    std::string line = nameOf(w, actor) + " uses " + itemName;
    if (target != actor) line += " on " + nameOf(w, target);
    line += food ? ": hunger " : ": HP ";
    line += std::to_string(was) + " -> " + std::to_string(now) + " of " + std::to_string(max);
    log.add(line);
    return true;
}

// Checks one rule for its unit. The condition is read from every unit in its condition group, and the unit that
// meets it with the lowest value is the match. The action then goes to the rule's action target: the unit itself for
// Self, or the match when the match is in that group. Returns true if the action was carried out.
bool evaluateRule(World& w, Entity actor, const BehaviorRule& r, CombatLog& log) {
    const Needs* own = w.get<Needs>(actor);
    if (!own || own->hp <= 0) return false;   // a downed unit does nothing

    Entity match = NULL_ENTITY;
    int matchValue = 0;
    for (Entity c : w.view<Needs>()) {
        if (!inGroup(w, actor, r.conditionTarget, c)) continue;
        const int v = valueOf(*w.get<Needs>(c), r.condition);
        if (!compareValue(v, r.op, r.percent)) continue;
        if (match == NULL_ENTITY || v < matchValue) {
            match = c;
            matchValue = v;
        }
    }
    if (match == NULL_ENTITY) return false;

    Entity target = NULL_ENTITY;
    if (r.actionTarget == BehaviorTarget::Self) target = actor;
    else if (inGroup(w, actor, r.actionTarget, match)) target = match;
    else return false;

    return carryOut(w, actor, target, r.action, log);
}

// Whether a rule watches a value that changed for a unit in its condition group
bool watchTriggered(World& w, Entity actor, const BehaviorRule& r, const std::vector<Entity>& hpChanged,
                    const std::vector<Entity>& hungerChanged) {
    const std::vector<Entity>& changed = r.condition == BehaviorCondition::Health ? hpChanged : hungerChanged;
    for (Entity c : changed) {
        if (inGroup(w, actor, r.conditionTarget, c)) return true;
    }
    return false;
}

} // namespace

size_t behaviorSystem(World& w, CombatLog& log) {
    // Which units' health or hunger changed since the last call. Their bits are cleared here, and anything the
    // actions change below sets them again for the next call.
    std::vector<Entity> hpChanged;
    std::vector<Entity> hungerChanged;
    for (Entity e : w.view<Needs>()) {
        Needs* n = w.get<Needs>(e);
        if (n->changed == 0) continue;
        if (n->changed & NEEDS_HP_CHANGED) hpChanged.push_back(e);
        if (n->changed & NEEDS_HUNGER_CHANGED) hungerChanged.push_back(e);
        n->changed = 0;
    }

    size_t checked = 0;
    for (Entity actor : w.view<Behaviors, Needs>()) {
        if (w.has<Unconscious>(actor) || w.has<Carried>(actor)) continue;   // out cold or carried: it does nothing
        Behaviors* b = w.get<Behaviors>(actor);
        // Every rule is checked when the list was edited, otherwise only those watching a value that changed
        const bool all = b->pending;
        b->pending = false;
        for (size_t i = 0; i < b->rules.size(); ++i) {
            const BehaviorRule rule = b->rules[i];   // a copy: the action can't change the list, but this keeps it simple
            if (!rule.on) continue;
            if (!all && !watchTriggered(w, actor, rule, hpChanged, hungerChanged)) continue;
            ++checked;
            evaluateRule(w, actor, rule, log);
        }
    }
    return checked;
}
