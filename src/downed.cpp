// Unconscious units, Conscious and Dying, corpses, and playing dead.
#include "systems_internal.h"

using namespace systems_detail;

// ---- Unconscious, Dying and Corpses ----

// Whether a unit lies in a bed or a bedroll, where Dying stops
static bool inBedOrBedroll(World& w, Entity u) {
    const Entity bed = occupiedProp(w, u);
    const Prop* p = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
    return p && (p->kind == PropKind::Bed || p->kind == PropKind::Bedroll);
}

void knockOut(World& w, Entity u) {
    if (!w.has<Unit>(u) || w.has<Unconscious>(u)) return;
    // Whatever it was doing ends. It keeps its bed if it is lying on one, and drops anyone it carried.
    const Entity bed = occupiedProp(w, u);
    const bool keepBed = inBedOrBedroll(w, u);
    if (w.has<Carrying>(u)) putDown(w, u);
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    if (keepBed) w.add(u, Occupying{ bed });
    if (Combat* c = w.get<Combat>(u)) c->target = NULL_ENTITY;
    if (Needs* n = w.get<Needs>(u)) {
        if (n->hp != 0) n->changed |= NEEDS_HP_CHANGED;
        n->hp = 0;
    }
    w.remove<PlayingDead>(u);
    // Who knocked it out: a hit from a unit of some faction, or nothing (hunger). Your units knocking out a unit of
    // another faction cost reputation with it.
    Unconscious un;
    if (Combat* c = w.get<Combat>(u)) {
        un.koFaction = c->downedByFaction;
        c->downedByFaction = -1;
    }
    w.add(u, un);
    queueLog(langf("MISC_FALLS_UNCONSCIOUS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    queueSoundFrom(w, Sfx::Downed, u);
    const int victim = factionOf(w, u);
    if (un.koFaction == FACTION_PLAYER && victim != FACTION_PLAYER && !isMobFaction(victim)) {
        const int now = changePlayerReputation(victim, -REP_KO_PENALTY);
        queueLog(langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_KO_PENALTY), std::to_string(now) }),
                 LogKind::Other);
    }
}

void downedSystem(World& w) {
    for (Entity u : w.view<Unit, Needs>()) {
        if (w.get<Needs>(u)->hp <= 0 && !w.has<Unconscious>(u)) knockOut(w, u);
    }
}

// Conscious is full: the unit wakes with a little health. Near a hostile unit one that is not a mob plays dead, unless it
// lies in a bed. Carried by anyone but a hostile, it is put down and stands up again.
static void wakeUp(World& w, Entity u, WakeChanges& out) {
    w.remove<Unconscious>(u);
    if (Needs* n = w.get<Needs>(u)) {
        n->hp = std::max(n->hp, std::min(std::max(n->maxHp, 1), RECOVERY_HEALTH));
        n->changed |= NEEDS_HP_CHANGED;
    }
    if (const Carried* cd = w.get<Carried>(u); cd && !w.has<Hostile>(cd->by)) putDown(w, cd->by);
    queueLog(langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    out.woke.push_back(u);
    if (!w.has<Mob>(u) && !w.has<Carried>(u) && !inBedOrBedroll(w, u) && hostileNear(w, u)) {
        w.add(u, PlayingDead{});
        queueLog(langf("MISC_PLAYS_DEAD", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

WakeChanges unconsciousSystem(World& w, double gameSeconds) {
    WakeChanges out;
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return out;
    for (Entity u : w.view<Unconscious>()) {
        if (w.has<Corpse>(u)) continue;
        Unconscious* un = w.get<Unconscious>(u);
        Needs* n = w.get<Needs>(u);
        // Comfort adds to each tick's gain, up to double at full Comfort. A mob has no Comfort.
        const float gain = 1.0f + (n && !w.has<Mob>(u) && n->maxComfort > 0 ? (float)n->comfort / (float)n->maxComfort : 0.0f);
        const bool bed = inBedOrBedroll(w, u);
        un->tickPart += CONSCIOUS_TICKS_PER_HOUR * hours;
        const int ticks = (int)un->tickPart;
        un->tickPart -= ticks;
        for (int i = 0; i < ticks; ++i) {
            // Each tick is paid for out of Rested. With none left it goes on Dying, unless the unit lies in a bed.
            if (n && n->rested > 0) {
                --n->rested;
            } else if (!bed && ++un->dying >= 100.0f) {
                finishDying(w, u);
                out.died.push_back(u);
                break;
            }
            un->conscious += gain;
            if (un->conscious >= 100.0f) {
                wakeUp(w, u, out);
                break;
            }
        }
    }
    return out;
}

bool hostileNear(World& w, Entity u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    const int mine = factionOf(w, u);
    for (Entity o : w.view<Unit, Transform>()) {
        if (o == u || w.has<Unconscious>(o) || w.has<PlayingDead>(o) || w.has<Carried>(o)) continue;
        if (!factionsHostile(mine, factionOf(w, o))) continue;
        const Transform* ot = w.get<Transform>(o);
        const float dx = ot->x - ut->x, dz = ot->z - ut->z;
        if (dx * dx + dz * dz <= RESTED_HOSTILE_RANGE * RESTED_HOSTILE_RANGE) return true;
    }
    return false;
}

void playDeadSystem(World& w) {
    for (Entity u : w.view<PlayingDead>()) {
        if (w.has<Carried>(u) || w.has<Unconscious>(u)) continue;   // carried: carrySystem looks after it
        if (hostileNear(w, u)) continue;
        w.remove<PlayingDead>(u);
        queueLog(langf("MISC_GETS_UP", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

void finishDying(World& w, Entity u) {
    Unconscious* un = w.get<Unconscious>(u);
    if (!un || w.has<Corpse>(u)) return;
    // Dead for good: a corpse, no longer anyone's to command, still there to carry and to loot
    un->dying = 100.0f;
    w.add(u, Corpse{});
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.remove<Occupying>(u);
    queueLog(langf("MISC_DIES", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    // Knocked out by your units, and dead before it woke: more reputation lost with its faction (a mob of a mob
    // faction is game, and costs none)
    const int victim = factionOf(w, u);
    if (un->koFaction == FACTION_PLAYER && victim != FACTION_PLAYER && !isMobFaction(victim)) {
        const int now = changePlayerReputation(victim, -REP_DEATH_PENALTY);
        queueLog(langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_DEATH_PENALTY), std::to_string(now) }),
                 LogKind::Other);
    }
}

