#include "need_behaviors.h"
#include <string>
#include <vector>
#include "behavior.h"
#include "components.h"
#include "hostility.h"
#include "items.h"
#include "useitem.h"

namespace {
bool hasOn(const Behaviors& b, BehaviorKind kind) {
    for (const BehaviorRule& r : b.rules) {
        if (r.kind == kind && r.on) return true;
    }
    return false;
}

bool canAct(World& w, Entity e) {
    const Needs* n = w.get<Needs>(e);
    return n && n->hp > 0 && !outCold(w, e) && !w.has<Carried>(e);
}

int valueOf(const Needs& n, bool food) { return food ? percentOf(n.hunger, n.maxHunger) : percentOf(n.hp, n.maxHp); }

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

bool consume(World& w, Entity giver, Entity taker, int slot, bool food, GameLog& log) {
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
    log.add(line, Tint::Gray, LogKind::Other, npcOnly(w, giver, taker));
    return true;
}

Entity sharerFor(World& w, Entity taker, bool food, int takerValue, float range) {
    const Transform* tt = w.get<Transform>(taker);
    if (!tt) return NULL_ENTITY;
    Entity best = NULL_ENTITY;
    int bestValue = takerValue;
    for (Entity g : w.view<Behaviors, Needs, Transform>()) {
        if (!canShareWith(w, g, taker, food, takerValue)) continue;
        const Transform* gt = w.get<Transform>(g);
        const float dx = gt->x - tt->x, dz = gt->z - tt->z;
        if (dx * dx + dz * dz > range * range) continue;
        const int v = valueOf(*w.get<Needs>(g), food);
        if (best != NULL_ENTITY && v <= bestValue) continue;
        best = g;
        bestValue = v;
    }
    return best;
}

bool needFailed(World& w, Entity actor, Behaviors& b, BehaviorRule& r, GameLog& log) {
    const bool food = r.kind == BehaviorKind::Eat;
    const int v = valueOf(*w.get<Needs>(actor), food);
    r.why = BehaviorWait::None;
    if (v >= r.percent) return false;
    const int own = bestSlot(w, actor, food);
    if (own >= 0 && consume(w, actor, actor, own, food, log)) return false;
    if (hasOn(b, BehaviorKind::Share)) {
        const Entity giver = sharerFor(w, actor, food, v, SHARE_RANGE);
        if (giver != NULL_ENTITY && consume(w, giver, actor, bestSlot(w, giver, food), food, log)) {
            b.seek = NULL_ENTITY;
            return false;
        }
        const Entity farther = sharerFor(w, actor, food, v, SHARE_SEEK_RANGE);
        if (farther != NULL_ENTITY) {
            b.seek = farther;
            r.why = BehaviorWait::WalkingToShare;
            return true;
        }
    }
    r.why = BehaviorWait::NoItem;
    return true;
}
}

bool canShareWith(World& w, Entity g, Entity taker, bool food, int takerValue) {
    if (g == taker || !canAct(w, g)) return false;
    const Behaviors* gb = w.get<Behaviors>(g);
    if (!gb || !hasOn(*gb, BehaviorKind::Share)) return false;
    if (!factionsAllied(factionOf(w, taker), factionOf(w, g))) return false;
    const Needs* gn = w.get<Needs>(g);
    return gn && valueOf(*gn, food) > takerValue && bestSlot(w, g, food) >= 0;
}

size_t behaviorSystem(World& w, GameLog& log, float dt) {
    size_t checked = 0;
    for (Entity actor : w.view<Behaviors, Needs>()) {
        Behaviors* b = w.get<Behaviors>(actor);
        Needs* n = w.get<Needs>(actor);
        const bool all = b->pending;
        const unsigned changed = n->changed;
        b->pending = false;
        n->changed = 0;
        for (BehaviorRule& r : b->rules) {
            if (isAutoStep(r.kind)) continue;
            const bool waited = r.cooldown > 0.0f && (r.cooldown -= dt) <= 0.0f;
            if (!r.on || !hasPercent(r.kind)) {
                r.why = BehaviorWait::None;
                continue;
            }
            if (!canAct(w, actor)) continue;
            const bool food = r.kind == BehaviorKind::Eat;
            const bool statChanged = (changed & (food ? NEEDS_HUNGER_CHANGED : NEEDS_HP_CHANGED)) != 0;
            if (r.cooldown > 0.0f || (!all && !statChanged && !waited)) continue;
            ++checked;
            if (needFailed(w, actor, *b, r, log)) r.cooldown = BEHAVIOR_RETRY_SECONDS;
        }
        bool seeking = false;
        for (const BehaviorRule& r : b->rules) seeking = seeking || (r.on && r.why == BehaviorWait::WalkingToShare);
        if (!seeking) b->seek = NULL_ENTITY;
    }
    return checked;
}
