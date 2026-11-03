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

// Whether a unit has a behavior of this kind switched on
bool hasOn(const Behaviors& b, BehaviorKind kind) {
    for (const BehaviorRule& r : b.rules) {
        if (r.kind == kind && r.on) return true;
    }
    return false;
}

// Whether a unit can act at all: not out cold, carried or at 0 health
bool canAct(World& w, Entity e) {
    const Needs* n = w.get<Needs>(e);
    return n && n->hp > 0 && !w.has<Unconscious>(e) && !w.has<Carried>(e);
}

// The share of a stat a unit has, as a whole percent
int valueOf(const Needs& n, bool food) { return food ? percentOf(n.hunger, n.maxHunger) : percentOf(n.hp, n.maxHp); }

// The slot of a unit's item that restores the most of the stat, or -1 when it carries none
int bestSlot(World& w, Entity unit, bool food) {
    const Inventory* inv = w.get<Inventory>(unit);
    if (!inv) return -1;
    const UseEffect::Stat stat = food ? UseEffect::Stat::Hunger : UseEffect::Stat::Health;
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
    return best;
}

// Uses the item in the giver's slot on the taker, and logs it. The giver is the taker itself, or a unit sharing with
// it. False if the item could not be used, so nothing happened.
bool consume(World& w, Entity giver, Entity taker, int slot, bool food, CombatLog& log) {
    const Inventory* inv = w.get<Inventory>(giver);
    const ItemDef* def = inv ? findItem(inv->slots[slot].item) : nullptr;
    if (!def) return false;
    const std::string itemName = itemDisplayName(*def);
    const Needs* before = w.get<Needs>(taker);
    const int was = before ? (food ? before->hunger : before->hp) : 0;
    if (useStackOn(w, giver, taker, slot) != UseResult::Used) return false;

    const Needs* after = w.get<Needs>(taker);
    const int now = after ? (food ? after->hunger : after->hp) : 0;
    const int max = after ? (food ? after->maxHunger : after->maxHp) : 0;
    std::string line = nameOf(w, taker) + " uses " + itemName;
    if (giver != taker) line += " shared by " + nameOf(w, giver);
    line += food ? ": hunger " : ": HP ";
    line += std::to_string(was) + " -> " + std::to_string(now) + " of " + std::to_string(max);
    log.add(line);
    return true;
}

// Share: the unit within SHARE_RANGE that can give the taker an item for the stat, or NULL_ENTITY. It has Share on,
// is of an allied faction, can act, carries such an item and needs it less than the taker does. The one that needs it
// least gives.
Entity sharerFor(World& w, Entity taker, bool food, int takerValue) {
    const Transform* tt = w.get<Transform>(taker);
    if (!tt) return NULL_ENTITY;
    const int fa = factionIdOf(w, taker);
    Entity best = NULL_ENTITY;
    int bestValue = takerValue;
    for (Entity g : w.view<Behaviors, Needs, Transform>()) {
        if (g == taker || !canAct(w, g) || !hasOn(*w.get<Behaviors>(g), BehaviorKind::Share)) continue;
        if (!factionsAllied(fa, factionIdOf(w, g))) continue;
        const Transform* gt = w.get<Transform>(g);
        const float dx = gt->x - tt->x, dz = gt->z - tt->z;
        if (dx * dx + dz * dz > SHARE_RANGE * SHARE_RANGE) continue;
        const int v = valueOf(*w.get<Needs>(g), food);
        if (v <= bestValue || bestSlot(w, g, food) < 0) continue;
        best = g;
        bestValue = v;
    }
    return best;
}

// Eat or Heal for one unit: below BEHAVIOR_NEED_PERCENT, it uses its own best item, or with Share on takes one from a
// unit sharing with it. True when it is in need and nothing could be used, so the behavior waits before trying again.
bool needFailed(World& w, Entity actor, const Behaviors& b, bool food, CombatLog& log) {
    const int v = valueOf(*w.get<Needs>(actor), food);
    if (v >= BEHAVIOR_NEED_PERCENT) return false;
    const int own = bestSlot(w, actor, food);
    if (own >= 0 && consume(w, actor, actor, own, food, log)) return false;
    if (hasOn(b, BehaviorKind::Share)) {
        const Entity giver = sharerFor(w, actor, food, v);
        if (giver != NULL_ENTITY && consume(w, giver, actor, bestSlot(w, giver, food), food, log)) return false;
    }
    return true;
}

} // namespace

size_t behaviorSystem(World& w, CombatLog& log, float dt) {
    size_t checked = 0;
    for (Entity actor : w.view<Behaviors, Needs>()) {
        Behaviors* b = w.get<Behaviors>(actor);
        Needs* n = w.get<Needs>(actor);
        // Every behavior is checked when the list was edited, otherwise only those whose stat changed or whose wait
        // after a failed try has run out
        const bool all = b->pending;
        const unsigned changed = n->changed;
        b->pending = false;
        n->changed = 0;
        for (BehaviorRule& r : b->rules) {
            if (isAutomated(r.kind)) continue;   // the automation counts down its own
            const bool waited = r.cooldown > 0.0f && (r.cooldown -= dt) <= 0.0f;
            if (!r.on || (r.kind != BehaviorKind::Eat && r.kind != BehaviorKind::Heal)) continue;
            if (!canAct(w, actor)) continue;   // out cold, carried or downed: it does nothing
            const bool food = r.kind == BehaviorKind::Eat;
            const bool statChanged = (changed & (food ? NEEDS_HUNGER_CHANGED : NEEDS_HP_CHANGED)) != 0;
            if (r.cooldown > 0.0f) continue;
            if (!all && !statChanged && !waited) continue;
            ++checked;
            if (needFailed(w, actor, *b, food, log)) r.cooldown = BEHAVIOR_RETRY_SECONDS;
        }
    }
    return checked;
}
