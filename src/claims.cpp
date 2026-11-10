#include "airship.h"
#include "building.h"
#include "items.h"
#include "claims.h"
#include "spatial.h"
#include <algorithm>
#include <cmath>
#include "build.h"
#include "factions.h"
#include "hostility.h"
#include "language.h"

std::vector<Claim> claimsOf(World& w) {
    std::vector<Claim> out;
    for (ObjectId e : w.view<Prop, Transform, Owner>()) {
        if (!propInfo(w.get<Prop>(e)->kind).claimsLand || !factionCanClaim(w.get<Owner>(e)->faction)) continue;
        Claim c;
        c.altar = e;
        c.x = w.get<Transform>(e)->x;
        c.z = w.get<Transform>(e)->z;
        c.owner = w.get<Owner>(e)->faction;
        out.push_back(c);
    }
    // Camps: a campfire of another faction (not yours) that stands outside its own faction's Altar claim
    const size_t altars = out.size();
    for (ObjectId e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(e)->kind != PropKind::Campfire || w.has<Blueprint>(e)) continue;
        const int faction = w.get<Owner>(e)->faction;
        if (faction == FACTION_PLAYER || !factionCanClaim(faction)) continue;
        const Transform* t = w.get<Transform>(e);
        bool atHome = false;
        for (size_t i = 0; i < altars && !atHome; ++i) {
            const float dx = t->x - out[i].x, dz = t->z - out[i].z;
            atHome = out[i].owner == faction && dx * dx + dz * dz <= CLAIM_RADIUS * CLAIM_RADIUS;
        }
        if (atHome) continue;
        Claim c;
        c.altar = e;
        c.x = t->x;
        c.z = t->z;
        c.owner = faction;
        c.radius = CAMP_CLAIM_RADIUS;
        c.camp = true;
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Claim& a, const Claim& b) { return a.altar < b.altar; });
    return out;
}

const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner) {
    for (const Claim& c : claims) {
        if (owner >= 0 && c.owner != owner) continue;
        const float dx = x - c.x, dz = z - c.z;
        if (dx * dx + dz * dz <= c.radius * c.radius) return &c;
    }
    return nullptr;
}

bool claimsOverlap(float ax, float az, float ar, float bx, float bz, float br) {
    const float dx = ax - bx, dz = az - bz;
    const float reach = ar + br;
    return dx * dx + dz * dz < reach * reach;
}

const Claim* claimOf(const std::vector<Claim>& claims, ObjectId prop) {
    for (const Claim& c : claims) {
        if (c.altar == prop) return &c;
    }
    return nullptr;
}

bool altarHasUpkeep(World& w, ObjectId altar) {
    const Prop* p = w.get<Prop>(altar);
    const Storage* st = w.get<Storage>(altar);
    if (!p || !st) return false;
    for (const ItemStack& s : st->slots) {
        if (inputMatches(propInfo(p->kind), s.item) && s.count > 0) return true;
    }
    return false;
}

bool altarFueled(World& w, ObjectId altar) {
    if (const Fuel* f = w.get<Fuel>(altar); f && f->hours > 1e-9) return true;
    return altarHasUpkeep(w, altar);
}

std::string propDisplayName(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p) return std::string();
    if (propInfo(p->kind).claimsLand) {
        if (const Owner* own = w.get<Owner>(prop)) return langf("MISC_ALTAR_OF", { factionName(own->faction) });
    }
    return lang(propInfo(p->kind).nameKey);
}

bool lockShutFor(World& w, ObjectId prop, ObjectId unit) {
    return lockShutForFaction(w, prop, factionOf(w, unit));
}

bool lockShutFor(World& w, const std::vector<Claim>& claims, ObjectId prop, ObjectId unit) {
    return lockShutForFaction(w, claims, prop, factionOf(w, unit));
}

bool lockShutForFaction(World& w, ObjectId prop, int faction) {
    const Lock* lock = w.get<Lock>(prop);
    const Prop* p = w.get<Prop>(prop);
    if ((p && propInfo(p->kind).claimsLand) || (lock && lock->locked)) return lockShutForFaction(w, claimsOf(w), prop, faction);
    return false;
}

bool lockShutForFaction(World& w, const std::vector<Claim>& claims, ObjectId prop, int faction) {
    if (const Prop* p = w.get<Prop>(prop); p && propInfo(p->kind).claimsLand) {
        const Owner* own = w.get<Owner>(prop);
        return !own || own->faction != faction;
    }
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !lock->locked) return false;
    if (lock->faction >= 0 && lock->faction == faction) return false;
    const Transform* t = w.get<Transform>(prop);
    return !(t && claimAt(claims, t->x, t->z, faction));
}

void claimOwnership(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    const Transform* t = w.get<Transform>(prop);
    if (!p || !t || propInfo(p->kind).claimsLand) return;
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* c = claimAt(claims, t->x, t->z);
    if (!c) return;
    const int faction = c->owner;
    if (Owner* own = w.get<Owner>(prop)) own->faction = faction;
    else w.add(prop, Owner{ faction });
    // A camp's claim owns what is in it, but locks none of it
    if (c->camp) return;
    if (Lock* lock = w.get<Lock>(prop)) lock->faction = faction;
    else if (propInfo(p->kind).use == PropUse::Storage) w.add(prop, Lock{ true, BUILT_LOCK_LEVEL, faction });
}

namespace {
void releaseProp(World& w, ObjectId prop) {
    for (ObjectId u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == prop) w.remove<Occupying>(u);
    }
    for (ObjectId u : w.view<Working>()) {
        if (w.get<Working>(u)->prop == prop) w.remove<Working>(u);
    }
    for (ObjectId u : w.view<PropOrder>()) {
        if (w.get<PropOrder>(u)->prop != prop) continue;
        w.remove<PropOrder>(u);
        w.remove<SoloMove>(u);
    }
    for (ObjectId u : w.view<Behaviors>()) {
        Behaviors* b = w.get<Behaviors>(u);
        if (b->busy && b->stepProp == prop) b->busy = false;
        b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(), [&](const BehaviorRule& r) {
                           return ruleNamesProp(r.kind) && r.prop == prop;
                       }),
                       b->rules.end());
    }
}

void abandonClaim(World& w, float x, float z, float radius, int faction) {
    for (ObjectId e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Owner>(e)->faction != faction) continue;
        const Transform* t = w.get<Transform>(e);
        const float dx = t->x - x, dz = t->z - z;
        if (dx * dx + dz * dz > radius * radius) continue;
        w.remove<Owner>(e);
        if (Lock* lock = w.get<Lock>(e); lock && lock->faction == faction) lock->faction = -1;
    }
}
}

void breakBuilding(World& w, ObjectId e) {
    if (!w.has<Prop>(e) || !w.has<Transform>(e)) return;
    const PropKind kind = w.get<Prop>(e)->kind;
    const Transform at = *w.get<Transform>(e);
    if (propInfo(kind).debris) {
        releaseProp(w, e);
        w.destroy(e);
        return;
    }
    // A blueprint goes with nothing left behind but the logs brought to it; a foundation takes its pieces with it
    if (const Blueprint* bp = w.get<Blueprint>(e)) {
        const int logs = bp->progress > 0.0 ? 0 : bp->supplied;   // spent once building began
        const int material = materialItem(materialOf(w, e));
        if (propInfo(kind).foundation) foundationLost(w, e);
        releaseProp(w, e);
        w.destroy(e);
        if (logs > 0) spawnGroundItem(w, at, material, logs);
        return;
    }
    if (propInfo(kind).foundation) foundationLost(w, e);
    if (propInfo(kind).wallRule || propInfo(kind).door) buildingTilesChanged(w, (int)std::floor(at.x), (int)std::floor(at.z), (int)std::floor(at.x), (int)std::floor(at.z));
    if (propInfo(kind).stairs) {
        // Their steps open up to walk across again
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        pieceTiles(w, e, x0, z0, x1, z1);
        buildingTilesChanged(w, x0, z0, x1, z1);
    }
    const std::vector<ItemStack> held = propHoldings(w, e);
    airshipLost(w, e);
    if (destroyedByHand(kind)) {
        releaseProp(w, e);
        w.destroy(e);
        dropStacks(w, at, held);
        // Broken: it leaves its item (a Dragon Crystal's crystals), or bones
        const int left = propInfo(kind).item > 0 ? propInfo(kind).item : itemTagged(ITEM_TAG_COMMON_BONES);
        spawnGroundItem(w, at, left, BONES_ITEM_COUNT);
        return;
    }
    releaseProp(w, e);
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* heldClaim = claimOf(claims, e);
    const int altarOwner = heldClaim ? heldClaim->owner : -1;
    const float heldRadius = heldClaim ? heldClaim->radius : 0.0f;
    w.destroy(e);
    if (altarOwner >= 0) abandonClaim(w, at.x, at.z, heldRadius, altarOwner);
    dropStacks(w, at, held);
    claimOwnership(w, makeProp(w, PropKind::Debris, at));
}

bool destroyBuilding(World& w, ObjectId e) {
    if (!w.has<Prop>(e)) return false;
    breakBuilding(w, e);
    return true;
}

void claimDecaySystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    for (const Claim& c : claimsOf(w)) {
        Health* hp = w.get<Health>(c.altar);
        if (!hp) continue;
        if (altarFueled(w, c.altar)) hp->value = std::min(hp->max, hp->value + ALTAR_RECOVER_PER_HOUR * hours);
        else hp->value -= hours / ALTAR_DECAY_HOURS * ALTAR_HEALTH;
    }
    for (ObjectId e : w.view<Health>()) {
        if (w.get<Health>(e)->value <= 0.0 && w.has<Prop>(e)) breakBuilding(w, e);
    }
}

void settleClaimOwnership(World& w) {
    for (ObjectId e : w.view<Prop, Transform>()) claimOwnership(w, e);
}

bool propDecaying(World& w, ObjectId e) {
    const Prop* p = w.get<Prop>(e);
    return p && propInfo(p->kind).claimsLand && w.has<Health>(e) && !altarFueled(w, e);
}

// Units let go of a prop that is going: they stop occupying, working or heading for it
void releasePropLinks(World& w, ObjectId prop) { releaseProp(w, prop); }

void removeProp(World& w, ObjectId e) {
    const Transform* tr = w.get<Transform>(e);
    if (!tr || !w.has<Prop>(e)) return;
    const Transform at = *tr;
    const std::vector<ItemStack> held = propHoldings(w, e);
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* c = claimOf(claims, e);
    const int owner = c ? c->owner : -1;
    const float radius = c ? c->radius : 0.0f;
    releaseProp(w, e);
    w.destroy(e);
    if (owner >= 0) abandonClaim(w, at.x, at.z, radius, owner);
    dropStacks(w, at, held);
}
