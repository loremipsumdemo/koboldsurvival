#pragma once
#include "systems.h"
#include "need_behaviors.h"
#include "claims.h"
#include "mobs.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <cstdlib>
#include "combat.h"
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "needs.h"
#include "party.h"
#include "props.h"
#include "emotes.h"
#include "spatial.h"

namespace systems_detail {
inline constexpr float UNIT_CLEARANCE = 0.2f;
inline constexpr float UNIT_BOX = 1.0f;
inline constexpr float PICKUP_REACH = 2.0f;
inline constexpr int CHAIN_REPLANS = 3;

inline constexpr float FOLLOW_DISTANCE = 1.6f;

inline void endPropTasks(World& w, Entity u) {
    w.remove<LootOrder>(u);
    w.remove<CarryOrder>(u);
    w.remove<PropOrder>(u);
    w.remove<Occupying>(u);
    w.remove<Working>(u);
    w.remove<FollowOrder>(u);
    w.remove<DefendOrder>(u);
}

inline void endPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<SoloMove>(u);
}

inline void cancelPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<PickupChain>(u);
}

inline void clearOrdersButFight(World& w, Entity u) {
    leaveSquad(w, u);
    w.remove<SoloMove>(u);
    cancelPickup(w, u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
}

inline void clearOrders(World& w, Entity u) {
    clearOrdersButFight(w, u);
    w.remove<AttackOrder>(u);
}

// An order of yours that is not a walk, a prop or a carry. Add new order components here, in endPropTasks and in clearOrders.
inline bool hasPlayerOrder(World& w, Entity u) {
    return w.has<AttackOrder>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) || w.has<PickupOrder>(u) ||
           w.has<PickupChain>(u) || w.has<LootOrder>(u) || w.has<TalkOrder>(u);
}

void lootOrderSystem(World& w, CarryEvents& ev);

Vec2 findSpawn(Terrain& t);
Inventory randomNpcLoot(Rng& rng);
void spawnPlayerUnit(World& w, Terrain& t, Rng& rng, Vec2 spawn, int i);

// The nearest prop that passes the test, or NULL_ENTITY
template <typename Pred>
Entity nearestProp(World& w, const Transform& from, Pred pred) {
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity p : w.view<Prop, Transform>()) {
        if (!pred(p)) continue;
        const float d2 = distSq(*w.get<Transform>(p), from);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

void autoWalk(World& w, Terrain& t, Entity u, Vec2 to);
void stopAutoStep(World& w, Entity u, Behaviors& b);
bool openTile(const Terrain& t, float x, float z);

inline bool outOfAction(World& w, Entity u) {
    return outCold(w, u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || lockedIn(w, u);
}

inline void planActiveSquad(World& w, Terrain& t, Entity squadE, Vec2 target) {
    ActiveSquad* sq = w.get<ActiveSquad>(squadE);
    sq->waypoints = t.findPath({ sq->cx, sq->cz }, target);
    sq->next = 0;
    sq->arrived = sq->waypoints.empty();
}

inline void planSolo(World& w, Terrain& t, Entity e, Vec2 target) {
    SoloMove* so = w.get<SoloMove>(e);
    Transform* tr = w.get<Transform>(e);
    so->waypoints = t.findPath({ tr->x, tr->z }, target);
    so->next = 0;
}

inline void walkSolo(World& w, Terrain& t, Entity e, Vec2 target) {
    w.add(e, SoloMove{});
    planSolo(w, t, e, target);
}

inline float slotSpacing(int size) {
    float scale = std::min(1.0f, std::sqrt(size / 10.0f));
    return std::max(MIN_SLOT_SPACING, SLOT_SPACING * scale);
}

inline PropUse useOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    return p ? propInfo(p->kind).use : PropUse::None;
}

inline int workersAt(World& w, Entity prop) {
    int n = 0;
    for (Entity o : w.view<Working>()) n += w.get<Working>(o)->prop == prop ? 1 : 0;
    return n;
}

inline std::vector<Entity> workedProps(World& w) {
    std::vector<Entity> out;
    for (Entity o : w.view<Working>()) out.push_back(w.get<Working>(o)->prop);
    std::sort(out.begin(), out.end());
    return out;
}
inline std::vector<Entity> occupiedProps(World& w) {
    std::vector<Entity> out;
    for (Entity o : w.view<Occupying>()) out.push_back(w.get<Occupying>(o)->prop);
    std::sort(out.begin(), out.end());
    return out;
}
inline int countIn(const std::vector<Entity>& sorted, Entity prop) {
    const auto r = std::equal_range(sorted.begin(), sorted.end(), prop);
    return (int)(r.second - r.first);
}

inline bool takeOne(ItemStack* slots, int count, int item) {
    for (int i = 0; i < count; ++i) {
        if (slots[i].item != item || slots[i].count <= 0) continue;
        if (--slots[i].count <= 0) slots[i] = ItemStack{};
        return true;
    }
    return false;
}
}
