#include "taming.h"
#include <algorithm>
#include <cmath>
#include "building.h"
#include "claims.h"
#include "inventory.h"
#include "items.h"
#include "systems_internal.h"
#include "world_labels.h"

using namespace systems_detail;

namespace {
float dist2(const Transform& a, const Transform& b) { return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z); }

BehaviorRule* ruleOf(Behaviors& b, BehaviorKind kind) {
    for (BehaviorRule& r : b.rules) {
        if (r.kind == kind) return &r;
    }
    return nullptr;
}

bool walkingNow(World& w, ObjectId u) {
    if (w.has<Route>(u) || w.has<Climbing>(u)) return true;
    const SoloMove* so = w.get<SoloMove>(u);
    return so && so->next < so->waypoints.size();
}

int carriedMeat(World& w, ObjectId u, int& item) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv) return 0;
    for (const ItemStack& s : inv->slots) {
        if (s.count > 0 && isRawMeat(s.item)) {
            item = s.item;
            return carriedCount(*inv, s.item);
        }
    }
    return 0;
}

// A cage of this faction's: owned by it, or standing in its claim
bool cageOfFaction(World& w, const std::vector<Claim>& claims, ObjectId cage, int faction) {
    const Owner* own = w.get<Owner>(cage);
    if (own) return own->faction == faction;
    const Transform* ct = w.get<Transform>(cage);
    return ct && claimAt(claims, ct->x, ct->z, faction) != nullptr;
}

// Animals have no hunger of their own: one in a cage is fed once every TAME_FEED_HOURS
bool wantsFeeding(World& w, ObjectId animal) {
    if (w.has<Corpse>(animal)) return false;
    const Taming* tm = w.get<Taming>(animal);
    return !tm || tm->sinceFed >= TAME_FEED_HOURS;
}

// The nearest caged animal of this unit's faction that wants feeding and no one else is taking meat to
ObjectId animalToFeed(World& w, const std::vector<Claim>& claims, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    const int faction = factionOf(w, u);
    std::vector<ObjectId> taken;
    for (ObjectId o : w.view<TameJob>()) {
        if (o != u) taken.push_back(w.get<TameJob>(o)->animal);
    }
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId a : w.view<Mob, Occupying, Transform>()) {
        const ObjectId cage = tamingCageOf(w, a);
        if (cage == NO_OBJECT || !cageOfFaction(w, claims, cage, faction) || !wantsFeeding(w, a)) continue;
        if (std::find(taken.begin(), taken.end(), a) != taken.end()) continue;
        const float d2 = dist2(*w.get<Transform>(a), *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = a;
            bestD2 = d2;
        }
    }
    return best;
}

// The nearest store of this unit's faction that holds raw meat, and one kind of it
ObjectId meatStore(World& w, const std::vector<Claim>& claims, ObjectId u, int& item) {
    const Transform* ut = w.get<Transform>(u);
    const int faction = factionOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId s : w.view<Storage, Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(s)->kind);
        if (info.claimsLand || info.stall || w.has<Blueprint>(s) || lockShutFor(w, claims, s, u)) continue;
        const Transform* st = w.get<Transform>(s);
        const Owner* own = w.get<Owner>(s);
        if (!(own && own->faction == faction) && !claimAt(claims, st->x, st->z, faction)) continue;
        int found = 0;
        for (const ItemStack& slot : w.get<Storage>(s)->slots) {
            if (slot.count > 0 && isRawMeat(slot.item)) found = slot.item;
        }
        if (found == 0) continue;
        const float d2 = dist2(*st, *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = s;
            bestD2 = d2;
            item = found;
        }
    }
    return best;
}

void walkTo(World& w, Terrain& t, ObjectId u, ObjectId target) {
    const Transform* tt = w.get<Transform>(target);
    if (tt) walkSoloTo(w, t, u, Vec2{ tt->x, tt->z }, storeyOf(w, target));
}
}

bool isRawMeat(int item) {
    const ItemDef* d = findItem(item);
    return d && hasTag(*d, "raw_food");
}

ObjectId tamingCageOf(World& w, ObjectId animal) {
    const Occupying* oc = w.get<Occupying>(animal);
    const Prop* p = oc ? w.get<Prop>(oc->prop) : nullptr;
    return p && propInfo(p->kind).taming ? oc->prop : NO_OBJECT;
}

bool startTameStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    r.why = BehaviorWait::None;
    if (w.has<Mob>(u) || !w.has<Transform>(u) || w.has<Carrying>(u)) return false;
    const std::vector<Claim> claims = claimsOf(w);
    const ObjectId animal = animalToFeed(w, claims, u);
    if (animal == NO_OBJECT) {
        r.why = BehaviorWait::NoCagedAnimal;
        return false;
    }
    TameJob job;
    job.animal = animal;
    job.cage = tamingCageOf(w, animal);
    if (carriedMeat(w, u, job.item) > 0) {
        job.phase = TameJob::Phase::Feed;
    } else {
        job.source = meatStore(w, claims, u, job.item);
        if (job.source == NO_OBJECT) {
            r.why = BehaviorWait::NoRawMeat;
            return false;
        }
        job.phase = TameJob::Phase::Fetch;
    }
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    b.busy = true;
    b.step = r;
    b.stepProp = job.cage;
    const ObjectId first = job.phase == TameJob::Phase::Fetch ? job.source : job.cage;
    if (flatGap(w, u, first) > PROP_REACH) walkTo(w, t, u, first);
    w.add(u, job);
    return true;
}

void tameJobSystem(World& w, Terrain& t, float dt, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours > 0.0) {
        for (ObjectId a : w.view<Taming>()) w.get<Taming>(a)->sinceFed += hours;
    }
    for (ObjectId u : w.view<TameJob, Transform>()) {
        TameJob& job = *w.get<TameJob>(u);
        Behaviors* b = w.get<Behaviors>(u);
        auto end = [&](float cooldown) {
            w.remove<TameJob>(u);
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            if (!b) return;
            const BehaviorKind kind = b->step.kind;
            b->busy = false;
            if (BehaviorRule* r = ruleOf(*b, kind)) r->cooldown = std::max(r->cooldown, cooldown);
        };
        // The animal is gone, out of its cage or fed by someone else meanwhile: the trip ends
        if (!b || outCold(w, u) || w.has<Carried>(u) || !w.has<Mob>(job.animal) || tamingCageOf(w, job.animal) != job.cage ||
            !wantsFeeding(w, job.animal)) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        const ObjectId goal = job.phase == TameJob::Phase::Fetch ? job.source : job.cage;
        if (!w.has<Transform>(goal)) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        if (flatGap(w, u, goal) > PROP_REACH + 0.3f) {
            if (!walkingNow(w, u)) {
                job.stuck += dt;
                if (job.stuck > 12.0f) end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                else if (job.stuck > 1.0f && job.stuck - dt <= 1.0f) walkTo(w, t, u, goal);
            }
            continue;
        }
        if (w.has<Climbing>(u)) continue;
        job.stuck = 0.0f;
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
        Inventory* inv = w.get<Inventory>(u);
        if (job.phase == TameJob::Phase::Fetch) {
            Storage* s = w.get<Storage>(job.source);
            if (!s || !inv) {
                end(BEHAVIOR_RETRY_SECONDS);
                continue;
            }
            const int want = std::min(TAME_MEAT_CARRIED, slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, job.item));
            const int took = takeFromSlots(s->slots.data(), (int)s->slots.size(), job.item, std::max(0, want));
            if (took <= 0) {
                end(BEHAVIOR_RETRY_SECONDS);
                continue;
            }
            addToInventory(*inv, job.item, took);
            propStorageChanged(w, job.source);
            queueItemMove(w, job.source, u, job.item, took);
            queueSoundFrom(w, Sfx::ItemMove, job.source);
            job.phase = TameJob::Phase::Feed;
            walkTo(w, t, u, job.cage);
            continue;
        }
        // At the cage: one raw meat into the animal
        if (!inv || carriedCount(*inv, job.item) <= 0) {
            if (!inv || carriedMeat(w, u, job.item) <= 0) {
                end(BEHAVIOR_RETRY_SECONDS);
                continue;
            }
        }
        takeFromSlots(inv->slots.data(), INVENTORY_SLOTS, job.item, 1);
        queueItemMove(w, u, job.animal, job.item, 1);
        queueSoundFrom(w, Sfx::ItemMove, job.cage);
        Taming& tm = w.has<Taming>(job.animal) ? *w.get<Taming>(job.animal) : w.add(job.animal, Taming{});
        ++tm.fed;
        tm.sinceFed = 0.0;
        queueLog(w, langf("MISC_FEEDS_CAGED", { nameOf(w, u), nameOf(w, job.animal), itemNameOf(job.item), std::to_string(tm.fed) }),
                 LogKind::Other, npcOnly(w, u, job.animal));
        end(0.0f);
    }
}
