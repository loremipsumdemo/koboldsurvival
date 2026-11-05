#pragma once
// Shared by the systems files (systems.cpp, world_setup.cpp, movement.cpp, prop_use.cpp, automation.cpp and
// downed.cpp), and only by them: the helpers more than one of them uses. What only one file uses stays in that file.
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

inline constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units in a formation
inline constexpr float UNIT_BOX = 1.0f;         // the side of the square every unit and mob takes up: one tile
inline constexpr float PICKUP_REACH = 2.0f;     // a unit this close to a ground item (in tiles) can take it
inline constexpr int CHAIN_REPLANS = 3;         // new routes a unit on a pickup chain tries to an item it stopped short of

inline constexpr float FOLLOW_DISTANCE = 1.6f;   // a unit following or defending another keeps within this many tiles of it

// Ends a unit's standing orders: the walk to a prop, a bed it lies on, the work it does, and following or defending a
// unit. Every order the player gives calls this, so a unit given another order leaves its bed or stops what it was at.
inline void endPropTasks(World& w, Entity u) {
    w.remove<LootOrder>(u);
    w.remove<CarryOrder>(u);
    w.remove<PutDownOrder>(u);
    w.remove<PropOrder>(u);
    w.remove<Occupying>(u);
    w.remove<Working>(u);
    w.remove<FollowOrder>(u);
    w.remove<DefendOrder>(u);
}

// Stops a unit's pickup: the order and its walk both end. A pickup chain the unit is on goes on (pickupChainSystem).
inline void endPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<SoloMove>(u);
}

// Another order replaces the unit's pickup: its pickup order and any pickup chain it was on both end
inline void cancelPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<PickupChain>(u);
}

// Ends what a unit was doing, apart from a fight: its place in an active squad, its walk, any pickup, talk and standing
// orders. The automation uses this, since a behavior's step does not end a fight.
inline void clearOrdersButFight(World& w, Entity u) {
    leaveSquad(w, u);
    w.remove<SoloMove>(u);
    cancelPickup(w, u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
}

// Ends everything a unit was doing, so a new order can begin
inline void clearOrders(World& w, Entity u) {
    clearOrdersButFight(w, u);
    w.remove<AttackOrder>(u);
}

// Plans a new route for an active squad's center. Only the center paths; members follow slots.
inline void planActiveSquad(World& w, Terrain& t, Entity squadE, Vec2 target) {
    ActiveSquad* sq = w.get<ActiveSquad>(squadE);
    sq->waypoints = t.findPath({ sq->cx, sq->cz }, target);
    sq->next = 0;
    sq->arrived = sq->waypoints.empty();
}

// Plans a single unit's own route to a ground point
inline void planSolo(World& w, Terrain& t, Entity e, Vec2 target) {
    SoloMove* so = w.get<SoloMove>(e);
    Transform* tr = w.get<Transform>(e);
    so->waypoints = t.findPath({ tr->x, tr->z }, target);
    so->next = 0;
}

// Sets a single unit walking its own route to a ground point
inline void walkSolo(World& w, Terrain& t, Entity e, Vec2 target) {
    w.add(e, SoloMove{});
    planSolo(w, t, e, target);
}

// Formation spacing grows with the active squad's size, up to the full spacing at 10 units.
// Small active squads stay compact so they don't spread out around the click point.
inline float slotSpacing(int size) {
    float scale = std::min(1.0f, std::sqrt(size / 10.0f));
    return std::max(MIN_SLOT_SPACING, SLOT_SPACING * scale);
}

// ---- Props ----

inline PropUse useOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    return p ? propInfo(p->kind).use : PropUse::None;
}

inline int workersAt(World& w, Entity prop) {
    int n = 0;
    for (Entity o : w.view<Working>()) n += w.get<Working>(o)->prop == prop ? 1 : 0;
    return n;
}

// For a search over many props: the prop each unit works (or lies in), sorted, once for the whole search, so each
// prop's count is a binary search instead of a pass over every worker
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

// Uses up one of an item from a row of slots. False if there is none.
inline bool takeOne(ItemStack* slots, int count, int item) {
    for (int i = 0; i < count; ++i) {
        if (slots[i].item != item || slots[i].count <= 0) continue;
        if (--slots[i].count <= 0) slots[i] = ItemStack{};
        return true;
    }
    return false;
}

} // namespace systems_detail
