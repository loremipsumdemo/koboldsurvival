// Turning a unit hostile and disbanding one. The rest of the systems are split by job:
//   world_setup.cpp  the start of a level: the spawn, the starting units, camps, mobs, loot and ground items
//   movement.cpp     move, pickup, attack, talk, follow and defend orders; squads, steering and defending
//   prop_use.cpp     using props: prop orders, work, crafting, storage and passive crafting
//   automation.cpp   behavior rules: hauling, harvesting, resting, patrols and roaming
//   wild_ai.cpp      patrol spawns, wild mobs grazing and fleeing, and Exiles running when hurt
//   downed.cpp       unconscious units, dying and corpses
//   carry.cpp        carrying a unit and putting it down
//   loot.cpp         looting, and what a unit may hold
//   needs.cpp        hunger, rest and comfort over time
//   unit_status.cpp  what a unit is doing, and whether it can act
// Helpers more than one of them uses are in systems_internal.h.
#include "systems_internal.h"

using namespace systems_detail;

void makeHostile(World& w, Entity unit) {
    // It is provoked by your faction (factionSystem keeps it hostile while that lasts), and turns at once
    if (!w.has<Unit>(unit) || w.has<Commandable>(unit)) return;
    provoke(w, unit, FACTION_PLAYER);
    if (w.has<Hostile>(unit)) return;
    w.remove<Neutral>(unit);
    w.add(unit, Hostile{});
    queueSound(Sfx::TurnHostile);
    queueLog(langf("MISC_TURNS_HOSTILE", { nameOf(w, unit) }), LogKind::Seen);
}

bool disbandUnit(World& w, Entity u) {
    if (!w.has<Commandable>(u) || !w.has<Unit>(u)) return false;
    // Whatever it was doing ends. One locked in a cage stays in it; anyone it carried is put down.
    leaveSquad(w, u);
    const bool caged = lockedIn(w, u);
    const Occupying keep = w.has<Occupying>(u) ? *w.get<Occupying>(u) : Occupying{};
    if (w.has<Carrying>(u)) putDown(w, u);
    clearOrders(w, u);
    if (caged) w.add(u, keep);
    if (Combat* c = w.get<Combat>(u)) c->target = NULL_ENTITY;
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.get<Unit>(u)->speed = HOSTILE_WALK_SPEED;   // it walks at the pace of every unit that is not yours

    const Transform* tr = w.get<Transform>(u);
    Behaviors& b = w.has<Behaviors>(u) ? *w.get<Behaviors>(u) : w.add(u, Behaviors{});
    b.busy = false;
    b.seeking = false;
    b.seek = NULL_ENTITY;
    b.stepProp = NULL_ENTITY;
    b.wait = 0.0f;
    b.pending = true;
    b.block = false;
    b.sneak = false;
    int faction = FACTION_EXILE;
    if (const Mob* mb = w.get<Mob>(u)) {
        // Back to the wild, in its kind's own faction, as a wild mob of that kind is made (spawnMob)
        const MobInfo& m = mobInfo(mb->kind);
        faction = mobFaction(mb->kind);
        b.rules = mobBehaviors();
        BehaviorRule roam;
        roam.kind = m.patrolSpawn ? BehaviorKind::PatrolSpawn : BehaviorKind::Roam;
        addBehavior(b.rules, roam);
        if (m.patrolSpawn && tr) w.add(u, SpawnHome{ tr->x, tr->z, false });
        const MobTemperament tm = mobTemperament(mb->kind);
        b.passive = tm == MobTemperament::Avoidant || tm == MobTemperament::Passive;
    } else {
        // An Exile: feeds and heals itself and roams, and has something to say like any other NPC. It fights as any
        // neutral unit does, but runs once its health is low (wildMobSystem).
        b.rules = exileBehaviors();
        b.passive = false;
        if (!w.has<Dialogue>(u)) w.add(u, Dialogue{});
        if (Needs* n = w.get<Needs>(u)) n->hunger = n->maxHunger;
    }
    if (Faction* fa = w.get<Faction>(u)) fa->id = faction;
    else w.add(u, Faction{ faction });
    if (factionsHostile(faction, FACTION_PLAYER)) w.add(u, Hostile{});
    else w.add(u, Neutral{});
    queueLog(langf(w.has<Mob>(u) ? "MISC_DISBANDED_MOB" : "MISC_DISBANDED", { nameOf(w, u) }), LogKind::Other);
    return true;
}

