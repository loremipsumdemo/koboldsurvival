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
#include "squads.h"
#include "ghosts.h"
#include "airship.h"
#include "building.h"

namespace systems_detail {
inline constexpr float UNIT_CLEARANCE = 0.2f;
inline constexpr float CROWD_CELL = 1.1f;   // no smaller than two units' radii and the margin of a touch
inline constexpr float PICKUP_REACH = 2.0f;
inline constexpr int CHAIN_REPLANS = 3;

inline constexpr float FOLLOW_DISTANCE = 1.6f;

inline void endPropTasks(World& w, ObjectId u) {
    w.remove<LootOrder>(u);
    w.remove<CarryOrder>(u);
    w.remove<PropOrder>(u);
    if (airshipOf(w, u) == NO_OBJECT) w.remove<Occupying>(u);   // only Evacuate takes a unit off an Airship, and slowly
    w.remove<Boarding>(u);
    w.remove<Working>(u);
    w.remove<FollowOrder>(u);
    w.remove<DefendOrder>(u);
}

inline void endPickup(World& w, ObjectId u) {
    w.remove<PickupOrder>(u);
    w.remove<SoloMove>(u);
}

inline void cancelPickup(World& w, ObjectId u) {
    w.remove<PickupOrder>(u);
    w.remove<PickupChain>(u);
}

inline void clearOrdersButFight(World& w, ObjectId u) {
    w.remove<BuildJob>(u);
    w.remove<StallJob>(u);
    w.remove<Climbing>(u);
    w.remove<Route>(u);
    leaveSquad(w, u);
    w.remove<SoloMove>(u);
    cancelPickup(w, u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
}

inline void clearOrders(World& w, ObjectId u) {
    clearOrdersButFight(w, u);
    w.remove<AttackOrder>(u);
}

// An order of yours that is not a walk, a prop or a carry. Add new order components here, in endPropTasks and in clearOrders.
inline bool hasPlayerOrder(World& w, ObjectId u) {
    return w.has<AttackOrder>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) || w.has<PickupOrder>(u) ||
           w.has<PickupChain>(u) || w.has<LootOrder>(u) || w.has<TalkOrder>(u);
}

void lootOrderSystem(World& w, CarryEvents& ev);

Vec2 findSpawn(Terrain& t);
Inventory randomNpcLoot(Rng& rng);
void spawnPlayerUnit(World& w, Terrain& t, Rng& rng, Vec2 spawn, int i);
// Gives a unit made by makeEntity its stats, health, gear, body and name
void finishUnit(World& w, Rng& rng, ObjectId u, std::vector<std::string>& takenNames);

// The nearest prop that passes the test, or NO_OBJECT
template <typename Pred>
ObjectId nearestProp(World& w, const Transform& from, Pred pred) {
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (!pred(p)) continue;
        const float d2 = distSq(*w.get<Transform>(p), from);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

void autoWalk(World& w, Terrain& t, ObjectId u, Vec2 to, int storey = -1);
void stepOffProp(World& w, Terrain& t, ObjectId u, ObjectId prop);
void stopAutoStep(World& w, ObjectId u, Behaviors& b);
bool openTile(const Terrain& t, float x, float z);

inline bool walkingNow(World& w, ObjectId u) {
    if (w.has<Route>(u) || w.has<Climbing>(u)) return true;
    const SoloMove* so = w.get<SoloMove>(u);
    return so && so->next < so->waypoints.size();
}

inline BehaviorRule* ruleOf(Behaviors& b, BehaviorKind kind) {
    for (BehaviorRule& r : b.rules) {
        if (r.kind == kind) return &r;
    }
    return nullptr;
}

// The faction that owns a prop, or -1
inline int ownerOf(World& w, ObjectId prop) {
    const Owner* o = w.get<Owner>(prop);
    return o ? o->faction : -1;
}

// A prop of this faction's: owned by it, or standing in its claim
inline bool propOfFaction(World& w, const std::vector<Claim>& claims, ObjectId prop, int faction) {
    if (const Owner* own = w.get<Owner>(prop)) return own->faction == faction;
    const Transform* pt = w.get<Transform>(prop);
    return pt && claimAt(claims, pt->x, pt->z, faction) != nullptr;
}

// A unit on an errand of its own (shopping, raiding, retreating, fetching food) leaves its tasks alone meanwhile
inline void holdTasks(World& w, ObjectId u) {
    if (Behaviors* b = w.get<Behaviors>(u)) {
        if (b->busy) stopAutoStep(w, u, *b);
        b->wait = std::max(b->wait, 1.0f);
    }
}

inline void releaseTasks(World& w, ObjectId u) {
    if (Behaviors* b = w.get<Behaviors>(u)) b->wait = 0.0f;
}

inline bool outOfAction(World& w, ObjectId u) {
    return outCold(w, u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || lockedIn(w, u);
}

inline void planActiveSquad(World& w, Terrain& t, ObjectId squadE, Vec2 target) {
    ActiveSquad* sq = w.get<ActiveSquad>(squadE);
    sq->waypoints = t.findPath({ sq->cx, sq->cz }, target);
    sq->next = 0;
    sq->arrived = sq->waypoints.empty();
}

// A walk on the unit's own floor
inline void planSolo(World& w, Terrain& t, ObjectId e, Vec2 target) { planSoloMove(w, t, e, target); }

// A walk anywhere: storey is the floor the target is on (-1: the unit's own floor if the target has one there, else the
// ground). On another floor, the unit goes by way of the ladders.
inline void walkSolo(World& w, Terrain& t, ObjectId e, Vec2 target, int storey = -1) { walkSoloTo(w, t, e, target, storey); }

inline PropUse useOf(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    return p ? propInfo(p->kind).use : PropUse::None;
}

inline int workersAt(World& w, ObjectId prop) {
    int n = 0;
    for (ObjectId o : w.view<Working>()) n += w.get<Working>(o)->prop == prop ? 1 : 0;
    return n;
}

inline std::vector<ObjectId> workedProps(World& w) {
    std::vector<ObjectId> out;
    for (ObjectId o : w.view<Working>()) out.push_back(w.get<Working>(o)->prop);
    std::sort(out.begin(), out.end());
    return out;
}
inline std::vector<ObjectId> occupiedProps(World& w) {
    std::vector<ObjectId> out;
    for (ObjectId o : w.view<Occupying>()) out.push_back(w.get<Occupying>(o)->prop);
    std::sort(out.begin(), out.end());
    return out;
}
inline int countIn(const std::vector<ObjectId>& sorted, ObjectId prop) {
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
