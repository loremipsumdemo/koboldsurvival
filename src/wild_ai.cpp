#include "systems_internal.h"

using namespace systems_detail;

void patrolSpawnSystem(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<SpawnHome, Transform, Behaviors>()) {
        // In a cage (a Taming Cage's animal): it stays put, and doesn't head home
        if (outCold(w, u) || w.has<Carried>(u) || w.has<Fleeing>(u) || inPrisonerCage(w, u)) continue;
        SpawnHome* h = w.get<SpawnHome>(u);
        Behaviors* b = w.get<Behaviors>(u);
        BehaviorRule* rule = nullptr;
        for (BehaviorRule& r : b->rules) {
            if (r.kind == BehaviorKind::PatrolSpawn) rule = &r;
        }
        if (!rule || !rule->on) {
            h->returning = false;
            h->returnSeconds = 0.0f;
            continue;
        }
        Transform* tr = w.get<Transform>(u);
        float away = std::hypot(tr->x - h->x, tr->z - h->z);
        if (!h->returning && away > SPAWN_LEASH_RANGE) {
            h->returning = true;
            h->returnSeconds = 0.0f;
            queueLog(w, langf("MISC_GOES_HOME", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
        }
        if (!h->returning) continue;
        h->returnSeconds += dt;
        if (const Mob* m = w.get<Mob>(u); m && mobInfo(m->kind).category == MobCategory::Boss
            && h->returnSeconds >= BOSS_RETURN_TELEPORT_SECONDS && away > SPAWN_HOME_REACH) {
            tr->x = h->x;
            tr->z = h->z;
            tr->y = t.heightAt(h->x, h->z);
            away = 0.0f;
        }
        if (Combat* c = w.get<Combat>(u)) {
            c->target = NO_OBJECT;
        }
        w.remove<AttackOrder>(u);
        b->passive = true;
        if (Needs* n = w.get<Needs>(u); n && n->hp != n->maxHp) {
            n->hp = n->maxHp;
            n->changed |= NEEDS_HP_CHANGED;
        }
        if (away <= SPAWN_HOME_REACH) {
            h->returning = false;
            h->returnSeconds = 0.0f;
            w.remove<SoloMove>(u);
            b->busy = false;
            rule->cooldown = 0.0f;
            continue;
        }
        const SoloMove* so = w.get<SoloMove>(u);
        const bool walking = so && so->next < so->waypoints.size();
        h->replanIn -= dt;
        if ((!walking || !b->busy || b->step.kind != BehaviorKind::PatrolSpawn) && h->replanIn <= 0.0f) {
            h->replanIn = 1.0f;
            b->busy = true;
            b->step = *rule;
            b->stepProp = NO_OBJECT;
            autoWalk(w, t, u, Vec2{ h->x, h->z });
        }
    }
}

void fleeFrom(World& w, Terrain& t, ObjectId u, Behaviors& b, ObjectId from) {
    const SoloMove* so = w.get<SoloMove>(u);
    if (b.busy && b.step.kind == BehaviorKind::Roam && so && so->next < so->waypoints.size()) return;
    const Transform* ut = w.get<Transform>(u);
    const Transform* ot = w.get<Transform>(from);
    if (!ut || !ot) return;
    const float away = std::atan2(ut->z - ot->z, ut->x - ot->x);
    static const float TURNS[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f };
    for (float turn : TURNS) {
        const float x = ut->x + std::cos(away + turn) * WILD_FLEE_DISTANCE;
        const float z = ut->z + std::sin(away + turn) * WILD_FLEE_DISTANCE;
        if (!openTile(t, x, z)) continue;
        if (b.busy) stopAutoStep(w, u, b);
        BehaviorRule roam;
        roam.kind = BehaviorKind::Roam;
        for (const BehaviorRule& r : b.rules) {
            if (r.kind == BehaviorKind::Roam) roam = r;
        }
        b.busy = true;
        b.step = roam;
        b.stepProp = NO_OBJECT;
        autoWalk(w, t, u, Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f });
        return;
    }
}

void wildMobSystem(World& w, Terrain& t, float dt) {
    std::vector<ObjectId> fighters, walkers;
    bool fightersListed = false, walkersListed = false;
    for (ObjectId u : w.view<Behaviors, Transform, Faction>()) {
        if (w.get<Faction>(u)->id != FACTION_NOMAD || w.has<Mob>(u)) continue;
        Behaviors* b = w.get<Behaviors>(u);
        const Needs* n = w.get<Needs>(u);
        const bool low = n && n->maxHp > 0 && n->hp * 100 <= n->maxHp * NOMAD_FLEE_PERCENT;
        b->passive = low;
        if (!low) continue;
        if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
        if (outOfAction(w, u) || n->hp <= 0) continue;
        b->sense -= dt;
        if (b->sense > 0.0f) continue;
        b->sense = 0.25f;
        const Transform* ut = w.get<Transform>(u);
        ObjectId near = NO_OBJECT;
        float nearD2 = 0.0f;
        if (!fightersListed) {
            fighters = w.view<Combat, Transform>();
            fightersListed = true;
        }
        for (ObjectId o : fighters) {
            if (o == u || w.get<Combat>(o)->target != u || outCold(w, o)) continue;
            const Transform* ot = w.get<Transform>(o);
            const float d2 = distSq(*ot, *ut);
            if (near == NO_OBJECT || d2 < nearD2) {
                near = o;
                nearD2 = d2;
            }
        }
        if (near == NO_OBJECT) continue;
        if (w.has<Occupying>(u)) w.remove<Occupying>(u);
        fleeFrom(w, t, u, *b, near);
    }
    for (ObjectId u : w.view<Mob, Behaviors, Transform, Faction>()) {
        const int faction = w.get<Faction>(u)->id;
        if (!isMobFaction(faction)) continue;
        Behaviors* b = w.get<Behaviors>(u);
        const MobTemperament tm = mobTemperament(w.get<Mob>(u)->kind);
        const SpawnHome* home = w.get<SpawnHome>(u);
        b->passive = tm == MobTemperament::Avoidant || tm == MobTemperament::Passive || (home && home->returning);
        if (tm != MobTemperament::Avoidant) continue;
        const Needs* n = w.get<Needs>(u);
        if (outCold(w, u) || w.has<Carried>(u) || (n && n->hp <= 0) || inPrisonerCage(w, u)) continue;
        b->sense -= dt;
        if (b->sense > 0.0f) continue;
        b->sense = 0.5f;
        const Transform* ut = w.get<Transform>(u);
        ObjectId near = NO_OBJECT;
        float nearD2 = WILD_AVOID_RANGE * WILD_AVOID_RANGE;
        if (!walkersListed) {
            walkers = w.view<Entity, Transform>();
            walkersListed = true;
        }
        for (ObjectId o : walkers) {
            if (o == u || outCold(w, o) || w.has<Carried>(o)) continue;
            const Faction* of = w.get<Faction>(o);
            if ((of ? of->id : FACTION_PLAYER) == faction) continue;
            if (const Mob* om = w.get<Mob>(o); om && mobKindsFriendly(w.get<Mob>(u)->kind, om->kind)) continue;
            const Transform* ot = w.get<Transform>(o);
            const float d2 = distSq(*ot, *ut);
            if (d2 < nearD2) {
                near = o;
                nearD2 = d2;
            }
        }
        if (near == NO_OBJECT) continue;
        fleeFrom(w, t, u, *b, near);
    }
}
