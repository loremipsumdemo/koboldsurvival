#include "claims.h"
#include "spatial.h"
#include <algorithm>
#include <cmath>
#include "build.h"
#include "factions.h"
#include "language.h"

std::vector<Claim> claimsOf(World& w) {
    std::vector<Claim> out;
    for (Entity e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(e)->kind != PropKind::Altar) continue;
        Claim c;
        c.altar = e;
        c.x = w.get<Transform>(e)->x;
        c.z = w.get<Transform>(e)->z;
        c.owner = w.get<Owner>(e)->faction;
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Claim& a, const Claim& b) { return a.altar < b.altar; });
    return out;
}

const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner) {
    for (const Claim& c : claims) {
        if (owner >= 0 && c.owner != owner) continue;
        const float dx = x - c.x, dz = z - c.z;
        if (dx * dx + dz * dz <= CLAIM_RADIUS * CLAIM_RADIUS) return &c;
    }
    return nullptr;
}

bool claimsOverlap(float ax, float az, float bx, float bz) {
    const float dx = ax - bx, dz = az - bz;
    const float reach = 2.0f * CLAIM_RADIUS;
    return dx * dx + dz * dz < reach * reach;
}

bool altarHasUpkeep(World& w, Entity altar) {
    const Prop* p = w.get<Prop>(altar);
    const Storage* st = w.get<Storage>(altar);
    if (!p || !st) return false;
    const int input = propInfo(p->kind).input;
    for (const ItemStack& s : st->slots) {
        if (s.item == input && s.count > 0) return true;
    }
    return false;
}

bool altarFueled(World& w, Entity altar) {
    if (const Fuel* f = w.get<Fuel>(altar); f && f->hours > 1e-9) return true;
    return altarHasUpkeep(w, altar);
}

std::string propDisplayName(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p) return std::string();
    if (p->kind == PropKind::Altar) {
        if (const Owner* own = w.get<Owner>(prop)) return langf("MISC_ALTAR_OF", { factionName(own->faction) });
    }
    return lang(propInfo(p->kind).nameKey);
}

bool lockShutFor(World& w, Entity prop, Entity unit) {
    const Faction* f = unit != NULL_ENTITY ? w.get<Faction>(unit) : nullptr;
    return lockShutForFaction(w, prop, f ? f->id : FACTION_PLAYER);
}

bool lockShutForFaction(World& w, Entity prop, int faction) {
    // An Altar has no lock to pick: it is its own faction's, and no one else's
    if (const Prop* p = w.get<Prop>(prop); p && p->kind == PropKind::Altar) {
        const Owner* own = w.get<Owner>(prop);
        return !own || own->faction != faction;
    }
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !lock->locked) return false;
    if (lock->faction >= 0 && lock->faction == faction) return false;
    // A locked prop inside a claim of this faction is open to it, whoever's lock it is
    if (const Transform* t = w.get<Transform>(prop)) {
        if (claimAt(claimsOf(w), t->x, t->z, faction)) return false;
    }
    return true;
}

void claimOwnership(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Transform* t = w.get<Transform>(prop);
    if (!p || !t || p->kind == PropKind::Altar) return;
    const Claim* c = claimAt(claimsOf(w), t->x, t->z);
    if (!c) return;
    const int faction = c->owner;
    if (Owner* own = w.get<Owner>(prop)) own->faction = faction;
    else w.add(prop, Owner{ faction });
    if (Lock* lock = w.get<Lock>(prop)) lock->faction = faction;
    else if (propInfo(p->kind).use == PropUse::Storage) w.add(prop, Lock{ true, BUILT_LOCK_LEVEL, faction });
}

namespace {

// Everything that points at a prop about to go lets go of it: units lying, sitting or shut in it get off, units
// working it or walking to it stop, and Auto-Hauls to it are dropped
void releaseProp(World& w, Entity prop) {
    for (Entity u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == prop) w.remove<Occupying>(u);
    }
    for (Entity u : w.view<Working>()) {
        if (w.get<Working>(u)->prop == prop) w.remove<Working>(u);
    }
    for (Entity u : w.view<PropOrder>()) {
        if (w.get<PropOrder>(u)->prop != prop) continue;
        w.remove<PropOrder>(u);
        w.remove<SoloMove>(u);
    }
    for (Entity u : w.view<Behaviors>()) {
        Behaviors* b = w.get<Behaviors>(u);
        if (b->busy && b->stepProp == prop) b->busy = false;
        b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(), [&](const BehaviorRule& r) {
                           return r.kind == BehaviorKind::AutoHaul && r.prop == prop;
                       }),
                       b->rules.end());
    }
}

// An Altar's claim is gone: what its faction owned in the circle now belongs to nobody. A lock stays as it is, with no
// faction behind it.
void abandonClaim(World& w, float x, float z, int faction) {
    for (Entity e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Owner>(e)->faction != faction) continue;
        const Transform* t = w.get<Transform>(e);
        const float dx = t->x - x, dz = t->z - z;
        if (dx * dx + dz * dz > CLAIM_RADIUS * CLAIM_RADIUS) continue;
        w.remove<Owner>(e);
        if (Lock* lock = w.get<Lock>(e); lock && lock->faction == faction) lock->faction = -1;
    }
}

}  // namespace

// A level's prop going for good is remembered, or a save would bring it back from the seed
static void rememberGone(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || p->serial < 0) return;
    const std::vector<Entity> holder = w.view<GoneProps>();
    GoneProps& gone = holder.empty() ? w.add(w.create(), GoneProps{}) : *w.get<GoneProps>(holder.front());
    gone.serials.push_back(p->serial);
}

void breakBuilding(World& w, Entity e) {
    if (!w.has<Prop>(e) || !w.has<Transform>(e)) return;
    const PropKind kind = w.get<Prop>(e)->kind;
    const Transform at = *w.get<Transform>(e);
    if (kind == PropKind::Debris) {
        releaseProp(w, e);
        rememberGone(w, e);
        w.destroy(e);
        return;
    }
    const std::vector<ItemStack> held = propHoldings(w, e);
    // Bones leave no Debris: only what they held and the Bones item, where they lay
    if (destroyedByHand(kind)) {
        releaseProp(w, e);
        rememberGone(w, e);
        w.destroy(e);
        dropStacks(w, at, held);
        spawnGroundItem(w, at, BONES_ITEM, BONES_ITEM_COUNT);
        return;
    }
    releaseProp(w, e);
    const Owner* own = w.get<Owner>(e);
    const int altarOwner = kind == PropKind::Altar && own ? own->faction : -1;
    rememberGone(w, e);
    w.destroy(e);
    if (altarOwner >= 0) abandonClaim(w, at.x, at.z, altarOwner);
    dropStacks(w, at, held);
    const Entity debris = w.create();
    w.add(debris, Transform{ at.x, at.y, at.z });
    w.add(debris, Prop{ PropKind::Debris, -1, 0.0f });
    claimOwnership(w, debris);
}

bool destroyBuilding(World& w, Entity e) {
    if (!w.has<Prop>(e)) return false;
    breakBuilding(w, e);
    return true;
}

void resolveDuplicateAltars(World& w) {
    std::vector<int> seen;
    std::vector<Entity> extra;
    for (const Claim& c : claimsOf(w)) {
        if (std::find(seen.begin(), seen.end(), c.owner) != seen.end()) extra.push_back(c.altar);
        else seen.push_back(c.owner);
    }
    for (Entity e : extra) breakBuilding(w, e);
}

void claimDecaySystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    // An Altar with upkeep recovers, and one with none wears through in ALTAR_DECAY_HOURS
    for (const Claim& c : claimsOf(w)) {
        Health* hp = w.get<Health>(c.altar);
        if (!hp) continue;
        if (altarFueled(w, c.altar)) hp->value = std::min(hp->max, hp->value + ALTAR_RECOVER_PER_HOUR * hours);
        else hp->value -= hours / ALTAR_DECAY_HOURS * ALTAR_HEALTH;
    }
    for (Entity e : w.view<Health>()) {
        if (w.get<Health>(e)->value <= 0.0 && w.has<Prop>(e)) breakBuilding(w, e);
    }
}

void settleClaimOwnership(World& w) {
    for (Entity e : w.view<Prop, Transform>()) claimOwnership(w, e);
}

bool propDecaying(World& w, Entity e) {
    const Prop* p = w.get<Prop>(e);
    return p && p->kind == PropKind::Altar && w.has<Health>(e) && !altarFueled(w, e);
}
