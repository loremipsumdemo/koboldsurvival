#include "systems_internal.h"

using namespace systems_detail;

ObjectId makeEntity(World& w, const Transform& at, int faction) {
    const ObjectId e = w.create();
    w.add(e, at);
    if (factionPlayerControlled(faction)) {
        w.add(e, Entity{});
        w.add(e, Commandable{});
    } else {
        w.add(e, Entity{ HOSTILE_WALK_SPEED });
        if (factionsHostile(w, faction, FACTION_PLAYER)) w.add(e, Hostile{});
        else w.add(e, Neutral{});
    }
    w.add(e, Faction{ faction });
    w.add(e, Inventory{});
    w.add(e, Equipment{});
    w.add(e, Needs{});
    w.add(e, Behaviors{});
    w.add(e, Combat{});
    w.add(e, Name{});
    w.add(e, UnitSprite{});
    return e;
}

Dialogue defaultDialogue() {
    return Dialogue{ lang("NPC_GREETING"), { { lang("NPC_OPTION_WARES"), lang("NPC_GREETING"), true },
                                             { lang("NPC_OPTION_QUEST"), lang("NPC_GREETING") } } };
}

void makeHostile(World& w, ObjectId unit) {
    if (!w.has<Entity>(unit) || w.has<Commandable>(unit)) return;
    provoke(w, unit, FACTION_PLAYER);
    if (w.has<Hostile>(unit)) return;
    w.remove<Neutral>(unit);
    w.add(unit, Hostile{});
    queueSound(w, Sfx::TurnHostile);
    queueLog(w, langf("MISC_TURNS_HOSTILE", { nameOf(w, unit) }), LogKind::Seen, LogWho(false, unit));
}

bool disbandUnit(World& w, ObjectId u) {
    if (!w.has<Commandable>(u) || !w.has<Entity>(u)) return false;
    leaveSquad(w, u);
    const bool caged = lockedIn(w, u);
    const Occupying keep = w.has<Occupying>(u) ? *w.get<Occupying>(u) : Occupying{};
    if (w.has<Carrying>(u)) putDown(w, u);
    clearOrders(w, u);
    if (caged) w.add(u, keep);
    if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.get<Entity>(u)->speed = HOSTILE_WALK_SPEED;

    const Transform* tr = w.get<Transform>(u);
    Behaviors& b = w.has<Behaviors>(u) ? *w.get<Behaviors>(u) : w.add(u, Behaviors{});
    b.busy = false;
    b.seeking = false;
    b.seek = NO_OBJECT;
    b.stepProp = NO_OBJECT;
    b.wait = 0.0f;
    b.pending = true;
    b.block = false;
    b.sneak = false;
    int faction = FACTION_FACTIONLESS;
    if (const Mob* mb = w.get<Mob>(u)) {
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
        b.rules = factionlessBehaviors();
        b.passive = false;
        if (!w.has<Dialogue>(u)) w.add(u, defaultDialogue());
        if (Needs* n = w.get<Needs>(u)) n->hunger = n->maxHunger;
    }
    if (Faction* fa = w.get<Faction>(u)) fa->id = faction;
    else w.add(u, Faction{ faction });
    if (factionsHostile(w, faction, FACTION_PLAYER)) w.add(u, Hostile{});
    else w.add(u, Neutral{});
    queueLog(w, langf(w.has<Mob>(u) ? "MISC_DISBANDED_MOB" : "MISC_DISBANDED", { nameOf(w, u) }), LogKind::Other, LogWho(false, u));
    return true;
}

