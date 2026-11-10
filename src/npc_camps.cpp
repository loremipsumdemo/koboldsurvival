#include "npc_camps.h"
#include "build.h"
#include "hostility.h"
#include "systems_internal.h"
#include "useitem.h"

using namespace systems_detail;

namespace {
// Seconds between looks for food, for every unit at once. Not saved.
struct CampClock {
    float forageIn = 0.0f;
};

bool walkingNow(World& w, ObjectId u) {
    if (w.has<Route>(u) || w.has<Climbing>(u)) return true;
    const SoloMove* so = w.get<SoloMove>(u);
    return so && so->next < so->waypoints.size();
}

// Its own tasks wait while it retreats or fetches food
void holdTasks(World& w, ObjectId u) {
    if (Behaviors* b = w.get<Behaviors>(u)) {
        if (b->busy) stopAutoStep(w, u, *b);
        b->wait = std::max(b->wait, 1.0f);
    }
}

void releaseTasks(World& w, ObjectId u) {
    if (Behaviors* b = w.get<Behaviors>(u)) b->wait = 0.0f;
}

float dist(const Transform& a, float x, float z) { return std::hypot(a.x - x, a.z - z); }

bool isFood(int item) {
    const ItemDef* d = findItem(item);
    return d && restoreAmount(*d, UseEffect::Stat::Hunger) > 0;
}

bool carriesFood(World& w, ObjectId u) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv) return false;
    for (const ItemStack& s : inv->slots) {
        if (s.count > 0 && isFood(s.item)) return true;
    }
    return false;
}

// Its faction's nearest claim: an Altar's or a camp's
const Claim* homeClaim(const std::vector<Claim>& claims, int faction, const Transform& from) {
    const Claim* best = nullptr;
    float bestD = 0.0f;
    for (const Claim& c : claims) {
        if (c.owner != faction) continue;
        const float d = dist(from, c.x, c.z);
        if (!best || d < bestD) {
            best = &c;
            bestD = d;
        }
    }
    return best;
}

// Out past your nearest claim, the way the unit already is from it; where it stands if you have none
Vec2 awaySpot(const Terrain& t, const std::vector<Claim>& claims, const Transform& from, float turn) {
    const Claim* near = nullptr;
    float nearD = 0.0f;
    for (const Claim& c : claims) {
        if (c.owner != FACTION_PLAYER) continue;
        const float d = dist(from, c.x, c.z);
        if (!near || d < nearD) {
            near = &c;
            nearD = d;
        }
    }
    if (!near) return Vec2{ from.x, from.z };
    const float base = nearD > 0.01f ? std::atan2(from.z - near->z, from.x - near->x) : turn;
    const float r = near->radius + CAMP_CLAIM_RADIUS + RETREAT_AWAY;
    for (int i = 0; i < 16; ++i) {
        const float a = base + turn + (i % 2 ? 1.0f : -1.0f) * (float)((i + 1) / 2) * 0.35f;
        const float x = near->x + std::cos(a) * r, z = near->z + std::sin(a) * r;
        if (openTile(t, x, z)) return Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
    }
    return Vec2{ from.x, from.z };
}

bool tileClear(World& w, const Terrain& t, float x, float z) {
    if (!openTile(t, x, z) || foundationAt(w, (int)std::floor(x), (int)std::floor(z)) != NO_OBJECT) return false;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (std::fabs(tr->x - x) < 1.0f && std::fabs(tr->z - z) < 1.0f) return false;
    }
    return true;
}

enum class CampTry { Made, Crowded, NoRoom };

// A campfire where the unit stands: only with no one of another faction near, and its claim clear of every other
CampTry makeCamp(World& w, Terrain& t, ObjectId u, const std::vector<Claim>& claims, float& fx, float& fz) {
    const Transform* ut = w.get<Transform>(u);
    const int faction = factionOf(w, u);
    for (ObjectId o : w.view<Entity, Transform>()) {
        if (o == u || factionOf(w, o) == faction || w.has<Corpse>(o) || inPrisonerCage(w, o)) continue;
        if (distSq(*w.get<Transform>(o), *ut) <= CAMP_CLEAR_RANGE * CAMP_CLEAR_RANGE) return CampTry::Crowded;
    }
    for (int ring = 0; ring <= 3; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                const float x = std::floor(ut->x) + dx + 0.5f, z = std::floor(ut->z) + dz + 0.5f;
                bool clear = tileClear(w, t, x, z);
                for (const Claim& c : claims) clear = clear && !claimsOverlap(x, z, CAMP_CLAIM_RADIUS, c.x, c.z, c.radius);
                if (!clear) continue;
                const ObjectId fire = makeProp(w, PropKind::Campfire, Transform{ x, t.heightAt(x, z), z });
                w.add(fire, Owner{ faction });
                fx = x;
                fz = z;
                queueLog(w, langf("MISC_MAKES_CAMP", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                return CampTry::Made;
            }
        }
    }
    return CampTry::NoRoom;
}

bool bedTaken(World& w, ObjectId bed, ObjectId u) {
    const ObjectId occupant = propOccupant(w, bed);
    if (occupant != NO_OBJECT && occupant != u) return true;
    for (ObjectId o : w.view<PropOrder>()) {
        if (o != u && w.get<PropOrder>(o)->prop == bed) return true;
    }
    return false;
}

// A free bed or bedroll of its faction's in its home claim; at a camp, one is laid round the campfire if none is free
ObjectId restBed(World& w, Terrain& t, ObjectId u, const Claim& home) {
    const int faction = factionOf(w, u);
    const Transform* ut = w.get<Transform>(u);
    ObjectId best = NO_OBJECT;
    float bestD = 0.0f;
    int bedrolls = 0;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (!isBed(w.get<Prop>(p)->kind) || w.has<Blueprint>(p) || storeyOf(w, p) != 0) continue;
        const Transform* pt = w.get<Transform>(p);
        if (dist(*pt, home.x, home.z) > home.radius) continue;
        const Owner* own = w.get<Owner>(p);
        if (!own || own->faction != faction) continue;
        if (home.camp && dist(*pt, home.x, home.z) <= CAMP_BEDROLL_RING + 1.0f) ++bedrolls;
        if (bedTaken(w, p, u)) continue;
        const float d = dist(*pt, ut->x, ut->z);
        if (best == NO_OBJECT || d < bestD) {
            best = p;
            bestD = d;
        }
    }
    if (best != NO_OBJECT || !home.camp || bedrolls >= CAMP_BEDROLLS_MOST) return best;
    // A bedroll of its own, round the campfire
    const float start = std::atan2(ut->z - home.z, ut->x - home.x);
    for (int i = 0; i < 16; ++i) {
        const float a = start + (i % 2 ? 1.0f : -1.0f) * (float)((i + 1) / 2) * 0.4f;
        const float x = std::floor(home.x + std::cos(a) * CAMP_BEDROLL_RING) + 0.5f;
        const float z = std::floor(home.z + std::sin(a) * CAMP_BEDROLL_RING) + 0.5f;
        if (!tileClear(w, t, x, z)) continue;
        const ObjectId bed = makeProp(w, PropKind::Bedroll, Transform{ x, t.heightAt(x, z), z }, a);
        w.add(bed, Owner{ faction });
        return bed;
    }
    return NO_OBJECT;
}

void endRetreat(World& w, Terrain& t, ObjectId u) {
    const Retreating* r = w.get<Retreating>(u);
    if (!r) return;
    if (Behaviors* b = w.get<Behaviors>(u)) b->passive = r->wasPassive;
    if (const Occupying* oc = w.get<Occupying>(u)) {
        const ObjectId bed = oc->prop;
        const Prop* p = w.get<Prop>(bed);
        if (p && isBed(p->kind)) {
            w.remove<Occupying>(u);
            stepOffProp(w, t, u, bed);
        }
    }
    w.remove<Retreating>(u);
    releaseTasks(w, u);
}

void retreatSystem(World& w, Terrain& t, float dt, double hours) {
    const std::vector<ObjectId> retreating = w.view<Retreating, Transform>();
    if (retreating.empty()) return;
    const std::vector<Claim> claims = claimsOf(w);
    for (ObjectId u : retreating) {
        Retreating& r = *w.get<Retreating>(u);
        Behaviors* b = w.get<Behaviors>(u);
        const Needs* n = w.get<Needs>(u);
        r.hours += hours;
        if (!b || !n || n->hp >= n->maxHp || r.hours >= RETREAT_MOST_HOURS) {
            endRetreat(w, t, u);
            continue;
        }
        // Taken: it waits to be let go
        if (outCold(w, u) || w.has<Carried>(u) || inPrisonerCage(w, u)) continue;
        b->passive = true;
        if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
        w.remove<AttackOrder>(u);
        if (w.has<Foraging>(u)) continue;   // gone for food first
        const Transform* ut = w.get<Transform>(u);
        // Home: its faction's nearest Altar or camp. Else a spot away from your claims, to make a camp
        if (!r.planned) {
            if (const Claim* home = homeClaim(claims, factionOf(w, u), *ut)) {
                r.x = home->x;
                r.z = home->z;
                r.home = true;
            } else {
                const Vec2 to = awaySpot(t, claims, *ut, 0.0f);
                r.x = to.x;
                r.z = to.z;
                r.home = false;
            }
            r.planned = true;
            r.arrived = false;
            queueLog(w, langf(r.home ? "MISC_RETREATS_HOME" : "MISC_RETREATS_AWAY", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
        }
        const Claim* home = r.home ? claimAt(claims, r.x, r.z, factionOf(w, u)) : nullptr;
        if (r.home && !home) {
            r.planned = false;   // its home is gone: it looks again
            continue;
        }
        const float near = home ? std::max(2.0f, home->radius * 0.5f) : 2.5f;
        const float d = dist(*ut, r.x, r.z);
        r.replan -= dt;
        if (!r.arrived && d > near) {
            holdTasks(w, u);
            if (!walkingNow(w, u) && r.replan <= 0.0f) {
                r.replan = 2.0f;
                w.remove<IdleSpot>(u);
                walkSolo(w, t, u, Vec2{ r.x, r.z }, 0);
            }
            continue;
        }
        r.arrived = true;
        if (!r.home) {
            holdTasks(w, u);
            if (r.replan > 0.0f) continue;
            r.replan = 5.0f;
            float fx = 0.0f, fz = 0.0f;
            const CampTry made = makeCamp(w, t, u, claims, fx, fz);
            if (made == CampTry::Made) {
                // Home is the camp now
                r.x = fx;
                r.z = fz;
                r.home = true;
            } else if (made == CampTry::NoRoom) {
                // No room here: on along the way
                const Vec2 to = awaySpot(t, claims, *ut, 0.8f + (float)(u % 5u) * 0.4f);
                r.x = to.x;
                r.z = to.z;
                r.arrived = false;
            }
            continue;
        }
        // At home: into a bed or bedroll till it is well
        const ObjectId lying = occupiedProp(w, u);
        const Prop* lp = lying != NO_OBJECT ? w.get<Prop>(lying) : nullptr;
        if (lp && isBed(lp->kind)) {
            holdTasks(w, u);
            continue;
        }
        if (w.has<PropOrder>(u)) {
            holdTasks(w, u);
            if (!walkingNow(w, u) && r.replan <= 0.0f) w.remove<PropOrder>(u);   // stuck: it looks again
            continue;
        }
        if (r.replan > 0.0f) continue;
        r.replan = 5.0f;
        const ObjectId bed = restBed(w, t, u, *home);
        if (bed == NO_OBJECT) continue;   // nowhere to lie down: it gets on with its tasks, out of fights
        holdTasks(w, u);
        const Transform* bt = w.get<Transform>(bed);
        w.remove<IdleSpot>(u);
        walkSolo(w, t, u, Vec2{ bt->x, bt->z }, storeyOf(w, bed));
        w.add(u, PropOrder{ bed, PropAction::Occupy, false });
    }
}

// What a unit may eat from: food on the ground (not in another faction's claim), or a store of its own faction's
ObjectId foodSource(World& w, ObjectId u, const std::vector<Claim>& claims) {
    const Transform* ut = w.get<Transform>(u);
    const int faction = factionOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = FORAGE_RANGE * FORAGE_RANGE;
    for (ObjectId g : w.view<GroundItem, Transform>()) {
        if (!isFood(w.get<GroundItem>(g)->item) || w.has<Carried>(g)) continue;
        const Transform* gt = w.get<Transform>(g);
        const float d2 = distSq(*gt, *ut);
        if (d2 >= bestD2) continue;
        if (const Claim* c = claimAt(claims, gt->x, gt->z); c && c->owner != faction) continue;
        best = g;
        bestD2 = d2;
    }
    for (ObjectId p : w.view<Storage, Prop, Transform>()) {
        const Owner* own = w.get<Owner>(p);
        if (!own || own->faction != faction || w.has<Blueprint>(p) || lockShutForFaction(w, claims, p, faction)) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = distSq(*pt, *ut);
        if (d2 >= bestD2) continue;
        bool food = false;
        for (const ItemStack& s : w.get<Storage>(p)->slots) food = food || (s.count > 0 && isFood(s.item));
        if (!food) continue;
        best = p;
        bestD2 = d2;
    }
    return best;
}

// Some food from the source into the unit's pack (up to 3), for its Eat task to eat
bool takeFood(World& w, ObjectId u, ObjectId source) {
    Inventory* inv = w.get<Inventory>(u);
    if (!inv) return false;
    int took = 0, item = 0;
    if (GroundItem* g = w.get<GroundItem>(source)) {
        item = g->item;
        const int want = std::min(3, g->count);
        took = want - addToInventory(*inv, g->item, want);
        g->count -= took;
        if (g->count <= 0) w.destroy(source);
    } else if (Storage* st = w.get<Storage>(source)) {
        for (ItemStack& s : st->slots) {
            if (s.count <= 0 || !isFood(s.item)) continue;
            item = s.item;
            const int want = std::min(3, s.count);
            took = want - addToInventory(*inv, s.item, want);
            s.count -= took;
            if (s.count <= 0) s = ItemStack{};
            break;
        }
        if (took > 0) propStorageChanged(w, source);
    }
    if (took <= 0) return false;
    queueItemMove(w, source, u, item, took);
    if (Needs* n = w.get<Needs>(u)) n->changed |= NEEDS_HUNGER_CHANGED;
    if (Behaviors* b = w.get<Behaviors>(u)) b->pending = true;
    return true;
}

// Free to go for food: not out of action, on no errand, in no fight, and not busy with a task (walking about aside)
bool freeToForage(World& w, ObjectId u) {
    if (outCold(w, u) || w.has<Carried>(u) || inPrisonerCage(w, u) || w.has<Foraging>(u)) return false;
    if (w.has<Raider>(u) || w.has<Shopper>(u) || w.has<WanderingTrader>(u) || w.has<StallJob>(u) || w.has<InConversation>(u)) return false;
    if (w.has<PropOrder>(u) || w.has<CarryOrder>(u) || w.has<BuildJob>(u) || w.has<AttackOrder>(u)) return false;
    if (const Combat* c = w.get<Combat>(u); c && c->target != NO_OBJECT) return false;
    const Behaviors* b = w.get<Behaviors>(u);
    if (!b || (b->busy && !isWalkBehavior(b->step.kind))) return false;
    // Lying in a bed: only one resting off a beating gets up to eat
    if (occupiedProp(w, u) != NO_OBJECT && !w.has<Retreating>(u)) return false;
    const Needs* n = w.get<Needs>(u);
    return n && n->maxHunger > 0 && n->hunger * 100 < n->maxHunger * FORAGE_BELOW_PERCENT && !carriesFood(w, u);
}

void forageSystem(World& w, Terrain& t, float dt) {
    CampClock& clock = w.resource<CampClock>();
    clock.forageIn -= dt;
    if (clock.forageIn <= 0.0f) {
        clock.forageIn = 2.0f;
        std::vector<Claim> claims;
        bool listed = false;
        for (ObjectId u : w.view<Behaviors, Needs, Transform>()) {
            if (!campNpc(w, u) || !freeToForage(w, u)) continue;
            if (!listed) {
                claims = claimsOf(w);
                listed = true;
            }
            const ObjectId source = foodSource(w, u, claims);
            if (source == NO_OBJECT) continue;
            if (const ObjectId bed = occupiedProp(w, u); bed != NO_OBJECT) {
                w.remove<Occupying>(u);
                stepOffProp(w, t, u, bed);
            }
            holdTasks(w, u);
            w.add(u, Foraging{ source, FORAGE_SECONDS });
            const Transform* st = w.get<Transform>(source);
            w.remove<IdleSpot>(u);
            walkSolo(w, t, u, Vec2{ st->x, st->z }, storeyOf(w, source));
        }
    }
    for (ObjectId u : w.view<Foraging, Transform>()) {
        Foraging& f = *w.get<Foraging>(u);
        f.timeLeft -= dt;
        auto done = [&]() {
            w.remove<Foraging>(u);
            w.remove<SoloMove>(u);
            releaseTasks(w, u);
        };
        if (outCold(w, u) || w.has<Carried>(u) || f.timeLeft <= 0.0f || !w.has<Transform>(f.source)) {
            done();
            continue;
        }
        if (const Combat* c = w.get<Combat>(u); c && c->target != NO_OBJECT) {
            done();
            continue;
        }
        holdTasks(w, u);
        const float reach = w.has<GroundItem>(f.source) ? PICKUP_REACH : PROP_REACH + 0.3f;
        if (flatGap(w, u, f.source) > reach) {
            if (!walkingNow(w, u)) {
                const Transform* st = w.get<Transform>(f.source);
                walkSolo(w, t, u, Vec2{ st->x, st->z }, storeyOf(w, f.source));
            }
            continue;
        }
        takeFood(w, u, f.source);
        done();
    }
}
}

bool campNpc(World& w, ObjectId u) {
    if (!w.has<Entity>(u) || w.has<Commandable>(u) || w.has<Mob>(u) || w.has<Corpse>(u)) return false;
    const int f = factionOf(w, u);
    return f != FACTION_PLAYER && factionCanClaim(f);
}

void startRetreat(World& w, ObjectId u) {
    if (!campNpc(w, u) || w.has<Retreating>(u)) return;
    const Behaviors* b = w.get<Behaviors>(u);
    if (!b) return;
    Retreating r;
    r.wasPassive = b->passive;
    w.add(u, r);
}

void npcCampSystem(World& w, Terrain& t, float dt, double gameSeconds) {
    forageSystem(w, t, dt);
    retreatSystem(w, t, dt, gameSeconds / 3600.0);
}

void outdoorDecaySystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    std::vector<Claim> altars;
    for (const Claim& c : claimsOf(w)) {
        if (!c.camp) altars.push_back(c);
    }
    std::vector<ObjectId> gone;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        if ((kind != PropKind::Bedroll && kind != PropKind::Campfire) || w.has<Blueprint>(e)) continue;
        const Transform* tr = w.get<Transform>(e);
        const bool outside = storeyOf(w, e) == 0 && foundationAt(w, (int)std::floor(tr->x), (int)std::floor(tr->z)) == NO_OBJECT &&
                             !claimAt(altars, tr->x, tr->z);
        if (!outside) {
            w.remove<OutdoorDecay>(e);
            continue;
        }
        OutdoorDecay& d = w.has<OutdoorDecay>(e) ? *w.get<OutdoorDecay>(e) : w.add(e, OutdoorDecay{});
        d.hours += hours;
        if (d.hours >= OUTDOOR_DECAY_HOURS) gone.push_back(e);
    }
    for (ObjectId e : gone) removeProp(w, e);
}
