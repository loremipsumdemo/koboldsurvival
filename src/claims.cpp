#include "airship.h"
#include "building.h"
#include "items.h"
#include "claims.h"
#include "spatial.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "build.h"
#include "factions.h"
#include "hostility.h"
#include "language.h"
#include "game_log.h"

std::vector<Claim> claimsOf(World& w) {
    std::vector<Claim> out;
    for (ObjectId e : w.view<Prop, Transform, Owner>()) {
        if (!propInfo(w.get<Prop>(e)->kind).claimsLand || w.has<Blueprint>(e) || !factionCanClaim(w.get<Owner>(e)->faction)) continue;
        Claim c;
        c.altar = e;
        c.x = w.get<Transform>(e)->x;
        c.z = w.get<Transform>(e)->z;
        c.owner = w.get<Owner>(e)->faction;
        if (const ClaimGrowth* g = w.get<ClaimGrowth>(e)) c.radius = CLAIM_RADIUS + CLAIM_GROWTH_STEP * (float)g->steps;
        out.push_back(c);
    }
    // Camps: a campfire (yours or another faction's) that stands outside its own faction's Altar claim
    const size_t altars = out.size();
    for (ObjectId e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(e)->kind != PropKind::Campfire || w.has<Blueprint>(e)) continue;
        const int faction = w.get<Owner>(e)->faction;
        if (!factionCanClaim(faction)) continue;
        const Transform* t = w.get<Transform>(e);
        bool atHome = false;
        for (size_t i = 0; i < altars && !atHome; ++i) {
            atHome = out[i].owner == faction && distSq(t->x, t->z, out[i].x, out[i].z) <= out[i].radius * out[i].radius;
        }
        if (atHome) continue;
        // Another faction's Altar claim takes priority: a campfire standing in it holds no claim of its own
        bool underAltar = false;
        for (size_t i = 0; i < altars && !underAltar; ++i) {
            underAltar = out[i].owner != faction && distSq(t->x, t->z, out[i].x, out[i].z) <= out[i].radius * out[i].radius;
        }
        if (underAltar) continue;
        Claim c;
        c.altar = e;
        c.x = t->x;
        c.z = t->z;
        c.owner = faction;
        c.radius = CAMP_CLAIM_RADIUS;
        c.camp = true;
        out.push_back(c);
    }
    // Altars first: where an Altar's claim and a camp's overlap, the Altar's land is the one found
    std::sort(out.begin(), out.end(), [](const Claim& a, const Claim& b) {
        if (a.camp != b.camp) return !a.camp;
        return a.altar < b.altar;
    });
    return out;
}

namespace {
struct ClaimCache {
    std::vector<Claim> claims;
    std::uint64_t revisions[4] = { 0, 0, 0, 0 };
    std::uint64_t generation = 1, builtGeneration = 0;
};
}

const std::vector<Claim>& cachedClaims(World& w) {
    ClaimCache& c = w.resource<ClaimCache>();
    const std::uint64_t now[4] = { w.revision<Prop>(), w.revision<Owner>(), w.revision<Blueprint>(), w.revision<ClaimGrowth>() };
    if (c.builtGeneration != c.generation || !std::equal(now, now + 4, c.revisions)) {
        c.claims = claimsOf(w);
        std::copy(now, now + 4, c.revisions);
        c.builtGeneration = c.generation;
    }
    return c.claims;
}

void invalidateClaims(World& w) {
    ++w.resource<ClaimCache>().generation;
}

bool campSheltered(World& w, const std::vector<Claim>& claims, float x, float z, int faction) {
    if (claimAt(claims, x, z, faction)) return true;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (w.get<Prop>(e)->kind != PropKind::Campfire || w.has<Blueprint>(e)) continue;
        if (const Owner* own = w.get<Owner>(e); own && own->faction != faction) continue;
        const Transform* t = w.get<Transform>(e);
        if (distSq(t->x, t->z, x, z) <= CAMP_CLAIM_RADIUS * CAMP_CLAIM_RADIUS) return true;
    }
    return false;
}

namespace {
struct CampPropClock {
    float since = 0.0f;
};
}

void campPropSystem(World& w, float dt) {
    CampPropClock& clock = w.resource<CampPropClock>();
    clock.since += dt;
    if (clock.since < 1.0f) return;
    clock.since = 0.0f;
    std::vector<ObjectId> lost;
    std::vector<Claim> claims;
    bool read = false;
    for (ObjectId e : w.view<Prop, Transform, Owner>()) {
        if (!propInfo(w.get<Prop>(e)->kind).needsCamp || w.has<Blueprint>(e)) continue;
        if (!read) claims = claimsOf(w);
        read = true;
        const Transform* t = w.get<Transform>(e);
        if (!campSheltered(w, claims, t->x, t->z, w.get<Owner>(e)->faction)) lost.push_back(e);
    }
    for (ObjectId e : lost) {
        queueLog(w, langf("MISC_CAMP_PROP_LOST", { lang(propInfo(w.get<Prop>(e)->kind).nameKey) }), LogKind::Other,
                 w.get<Owner>(e)->faction != FACTION_PLAYER);
        breakBuilding(w, e);
    }
}

const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner) {
    for (const Claim& c : claims) {
        if (owner >= 0 && c.owner != owner) continue;
        if (distSq(x, z, c.x, c.z) <= c.radius * c.radius) return &c;
    }
    return nullptr;
}

bool claimsOverlap(float ax, float az, float ar, float bx, float bz, float br) {
    const float reach = ar + br;
    return distSq(ax, az, bx, bz) < reach * reach;
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
    if ((p && propInfo(p->kind).claimsLand) || (lock && lock->locked)) return lockShutForFaction(w, cachedClaims(w), prop, faction);
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
    invalidateClaims(w);
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
        if (distSq(t->x, t->z, x, z) > radius * radius) continue;
        w.remove<Owner>(e);
        if (Lock* lock = w.get<Lock>(e); lock && lock->faction == faction) lock->faction = -1;
    }
}

// Where what a broken prop leaves lies, and on which floor: its own floor's top, or for a floor itself (which held
// it up), the top of the floor under it
Transform remainsAt(World& w, ObjectId e, int& storey) {
    Transform at = *w.get<Transform>(e);
    storey = storeyOf(w, e);
    if (storey <= 0) return at;
    const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
    const float ground = at.y - (info.upperFloor ? floorSlabHeight(storey) : storeyBaseHeight(storey));
    if (info.upperFloor) --storey;
    at.y = ground + storeyBaseHeight(storey);
    return at;
}
}

void breakBuilding(World& w, ObjectId e, bool leaveDebris) {
    if (!w.has<Prop>(e) || !w.has<Transform>(e)) return;
    const PropKind kind = w.get<Prop>(e)->kind;
    int storey = 0;
    const Transform at = remainsAt(w, e, storey);
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
        if (logs > 0) spawnGroundItem(w, at, material, logs, storey);
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
        dropStacks(w, at, held, storey);
        // Broken: it leaves its item (a Dragon Crystal's crystals), or bones
        const int left = propInfo(kind).item > 0 ? propInfo(kind).item : itemTagged(ITEM_TAG_COMMON_BONES);
        spawnGroundItem(w, at, left, BONES_ITEM_COUNT, storey);
        return;
    }
    releaseProp(w, e);
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* heldClaim = claimOf(claims, e);
    const bool heldAltar = heldClaim && !heldClaim->camp;
    const int altarOwner = heldAltar ? heldClaim->owner : -1;
    const float heldRadius = heldAltar ? heldClaim->radius : 0.0f;
    w.destroy(e);
    // A camp's campfire claims what is in it but never strips its owners: what it sheltered falls on its own
    if (altarOwner >= 0) abandonClaim(w, at.x, at.z, heldRadius, altarOwner);
    dropStacks(w, at, held, storey);
    if (!leaveDebris) return;
    // The debris stands on the floor the remains are on, as if it had been placed there
    const ObjectId debris = makeProp(w, PropKind::Debris, at);
    if (storey > 0) w.add(debris, Storey{ storey });
    claimOwnership(w, debris);
}

bool destroyBuilding(World& w, ObjectId e) {
    if (!w.has<Prop>(e)) return false;
    breakBuilding(w, e);
    return true;
}

bool deconstructBuilding(World& w, ObjectId e) {
    if (!w.has<Prop>(e) || !w.has<Transform>(e) || w.has<Blueprint>(e)) return false;
    const PropKind kind = w.get<Prop>(e)->kind;
    int storey = 0;
    const Transform at = remainsAt(w, e, storey);
    int logs = 0, material = 0;
    if (isBuildingPiece(kind)) {
        logs = materialsNeeded(w, e);
        material = materialItem(materialOf(w, e));
    }
    breakBuilding(w, e, false);
    if (logs > 0 && material > 0) spawnGroundItem(w, at, material, logs, storey);
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
    // Every prop with health mends by itself, a tenth of its health an hour, inside an Altar's claim (not a camp's)
    const std::vector<Claim> claims = claimsOf(w);
    for (ObjectId e : w.view<Health, Prop, Transform>()) {
        Health* h = w.get<Health>(e);
        if (h->value <= 0.0 || h->value >= h->max || propInfo(w.get<Prop>(e)->kind).claimsLand) continue;
        const Transform* tr = w.get<Transform>(e);
        const Claim* c = claimAt(claims, tr->x, tr->z);
        if (c && !c->camp) h->value = std::min(h->max, h->value + PROP_MEND_SHARE_PER_HOUR * h->max * hours);
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
    int storey = 0;
    const Transform at = remainsAt(w, e, storey);
    const std::vector<ItemStack> held = propHoldings(w, e);
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* c = claimOf(claims, e);
    const bool altar = c && !c->camp;
    const int owner = altar ? c->owner : -1;
    const float radius = altar ? c->radius : 0.0f;
    releaseProp(w, e);
    w.destroy(e);
    if (owner >= 0) abandonClaim(w, at.x, at.z, radius, owner);
    dropStacks(w, at, held, storey);
}

namespace {
// What claim growth remembers between looks. Not saved: a loaded game looks again.
struct ClaimWatch {
    float timer = 0.0f;
    std::unordered_map<ObjectId, unsigned long long> noRoomSeen;   // an Altar, and the claims as they were when it had no room
};

// The claims as they stand, in one number: any claim-making prop added, lost, moved or grown changes it
unsigned long long claimsSignature(const std::vector<Claim>& claims) {
    unsigned long long h = 1469598103934665603ULL;
    auto mix = [&](unsigned long long v) { h = (h ^ v) * 1099511628211ULL; };
    for (const Claim& c : claims) {
        mix((unsigned long long)c.altar);
        mix((unsigned long long)std::lround(c.x * 4.0f));
        mix((unsigned long long)std::lround(c.z * 4.0f));
        mix((unsigned long long)std::lround(c.radius * 4.0f));
        mix(c.camp ? 1ULL : 0ULL);
        mix((unsigned long long)(c.owner + 1));
    }
    return h;
}
}

void claimGrowthSystem(World& w, float dt) {
    ClaimWatch& watch = w.resource<ClaimWatch>();
    watch.timer += dt;
    constexpr float LOOK_EVERY_SECONDS = 4.0f;
    if (watch.timer < LOOK_EVERY_SECONDS) return;
    watch.timer = 0.0f;
    std::vector<Claim> claims = claimsOf(w);
    bool anyAltar = false;
    for (const Claim& c : claims) anyAltar = anyAltar || !c.camp;
    if (!anyAltar) return;
    const BuildingMap map = buildingMap(w);
    const std::vector<BuildingCluster> buildings = buildingClusters(w, map);
    std::vector<ObjectId> altars;
    for (const Claim& c : claims) {
        if (!c.camp) altars.push_back(c.altar);
    }
    for (ObjectId altar : altars) {
        for (;;) {
            const std::vector<Claim> standing = claimsOf(w);
            const Claim* held = claimOf(standing, altar);
            if (!held) break;
            const Claim c = *held;   // as it stands after any growth
            int steps = 0;
            if (const ClaimGrowth* g = w.get<ClaimGrowth>(altar)) steps = g->steps;
            int earned = 0;
            for (const BuildingCluster& b : buildings) {
                if (b.complete && b.owner == c.owner && distSq(b.centerX(), b.centerZ(), c.x, c.z) <= c.radius * c.radius) ++earned;
            }
            if (earned <= steps || steps >= CLAIM_GROWTH_MOST) break;
            const std::vector<Claim> now = claimsOf(w);
            const unsigned long long sig = claimsSignature(now);
            auto seen = watch.noRoomSeen.find(altar);
            if (seen != watch.noRoomSeen.end() && seen->second == sig) break;   // nothing has changed since it found no room
            const float bigger = c.radius + CLAIM_GROWTH_STEP;
            bool room = true;
            for (const Claim& k : now) {
                if (k.altar == altar || k.camp) continue;
                if (claimsOverlap(c.x, c.z, bigger, k.x, k.z, k.radius)) room = false;
            }
            if (!room) {
                watch.noRoomSeen[altar] = sig;
                break;
            }
            watch.noRoomSeen.erase(altar);
            if (ClaimGrowth* g = w.get<ClaimGrowth>(altar)) ++g->steps;
            else w.add(altar, ClaimGrowth{ 1 });
            invalidateClaims(w);
            // A camp of another faction that the larger claim reaches is destroyed: an Altar's land comes first
            for (const Claim& k : now) {
                if (!k.camp || k.owner == c.owner) continue;
                if (claimsOverlap(c.x, c.z, bigger, k.x, k.z, k.radius)) breakBuilding(w, k.altar);
            }
            if (c.owner == FACTION_PLAYER) queueLog(w, langf("MISC_CLAIM_GROWS", { factionName(c.owner), std::to_string((int)bigger) }), LogKind::Other);
        }
    }
}
