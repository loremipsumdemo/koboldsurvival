#include "taming.h"
#include <algorithm>
#include "claims.h"
#include "items.h"
#include "systems_internal.h"

using namespace systems_detail;

bool isRawMeat(int item) {
    const ItemDef* d = findItem(item);
    return d && hasTag(*d, "raw_food");
}

ObjectId tamingCageOf(World& w, ObjectId animal) {
    const Occupying* oc = w.get<Occupying>(animal);
    const Prop* p = oc ? w.get<Prop>(oc->prop) : nullptr;
    return p && propInfo(p->kind).taming ? oc->prop : NO_OBJECT;
}

int tamingFaction(World& w, ObjectId cage) {
    if (const Owner* own = w.get<Owner>(cage)) return own->faction;
    const Transform* ct = w.get<Transform>(cage);
    if (!ct) return -1;
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* c = claimAt(claims, ct->x, ct->z);
    return c ? c->owner : -1;
}

double tameHoursNeeded(World& w, ObjectId animal) {
    const Needs* n = w.get<Needs>(animal);
    return tameHoursFor(n ? n->maxHp : 100);
}

float tamingProgress(World& w, ObjectId animal) {
    const Taming* tm = w.get<Taming>(animal);
    if (!tm) return 0.0f;
    return (float)std::clamp(tm->hours / tameHoursNeeded(w, animal), 0.0, 1.0);
}

void tameAnimal(World& w, ObjectId animal, int faction) {
    if (!w.has<Mob>(animal) || !findFaction(faction)) return;
    const ObjectId cage = tamingCageOf(w, animal);
    w.remove<Taming>(animal);
    w.remove<Occupying>(animal);
    w.remove<SpawnHome>(animal);
    w.remove<Provoked>(animal);
    w.remove<Retreating>(animal);
    if (Lock* lk = cage != NO_OBJECT ? w.get<Lock>(cage) : nullptr) lk->locked = false;
    // Out beside the cage, not in it
    if (const Transform* ct = cage != NO_OBJECT ? w.get<Transform>(cage) : nullptr) {
        Transform* at = w.get<Transform>(animal);
        at->x = ct->x + 1.2f;
        at->z = ct->z;
    }
    if (Faction* fa = w.get<Faction>(animal)) fa->id = faction;
    else w.add(animal, Faction{ faction });
    if (Combat* c = w.get<Combat>(animal)) c->target = NO_OBJECT;
    Behaviors& b = w.has<Behaviors>(animal) ? *w.get<Behaviors>(animal) : w.add(animal, Behaviors{});
    if (factionPlayerControlled(faction)) {
        // Yours to order: into a squad with room, the one you are looking at first
        if (!makeMilitary(w, animal, w.resource<SquadBook>().current)) {
            w.remove<Hostile>(animal);
            w.remove<Neutral>(animal);
            w.add(animal, Commandable{});
        }
        b.rules = mobBehaviors();
    } else {
        b.rules = mobBehaviors();
        BehaviorRule patrol;
        patrol.kind = BehaviorKind::Patrol;
        addBehavior(b.rules, patrol);
    }
    b.passive = false;
    b.busy = false;
    b.pending = true;
    queueLog(w, langf("MISC_TAMED", { nameOf(w, animal), factionName(faction) }), LogKind::Other, LogWho(!factionPlayerControlled(faction), animal),
             factionPlayerControlled(faction) ? Tint::Green : Tint::Gray);
    queueSoundFrom(w, Sfx::Unlocked, animal);
}

void tamingSystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    std::vector<std::pair<ObjectId, int>> done;
    for (ObjectId a : w.view<Mob>()) {
        const ObjectId cage = w.has<Corpse>(a) ? NO_OBJECT : tamingCageOf(w, a);
        const int faction = cage != NO_OBJECT ? tamingFaction(w, cage) : -1;
        // Only in a Taming Cage of a faction not its own already
        if (faction < 0 || faction == factionOf(w, a) || isMobFaction(faction)) {
            w.remove<Taming>(a);
            continue;
        }
        Taming& tm = w.has<Taming>(a) ? *w.get<Taming>(a) : w.add(a, Taming{});
        if (hours > 0.0) tm.hours += hours;
        if (tm.hours >= tameHoursNeeded(w, a)) done.push_back({ a, faction });
    }
    for (const auto& [a, faction] : done) tameAnimal(w, a, faction);
}
