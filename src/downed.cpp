// Unconscious units, doom and corpses, and playing dead.
#include "systems_internal.h"

using namespace systems_detail;

// ---- Unconscious, Doom and Corpses ----

float doomPerHour(const Stats& s) {
    const int vit = std::clamp(levelOf(s, StatId::Vitality), 1, MAX_LEVEL);
    return 20.0f + 30.0f * (1.0f - (float)vit / 100.0f);
}

float doomRecoveryPerHour(PropKind propKind) {
    if (propKind == PropKind::Bed) return 20.0f;
    if (propKind == PropKind::Bedroll) return 10.0f;
    return 0.0f;
}

void knockOut(World& w, Entity u) {
    if (!w.has<Unit>(u) || w.has<Unconscious>(u)) return;
    // Whatever it was doing ends. It keeps its bed if it is lying on one, and drops anyone it carried.
    const Entity bed = occupiedProp(w, u);
    const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
    const bool keepBed = bp && doomRecoveryPerHour(bp->kind) > 0.0f;
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
    // Well rested, and not starving: no Doom, and it wakes by itself after a while (a mob has no Rested)
    if (const Needs* n = w.get<Needs>(u); n && !w.has<Mob>(u) && n->hunger > 0
        && percentOf(n->rested, n->maxRested) >= RESTED_SAFE_PERCENT) {
        un.rested = true;
        un.wake = RESTED_WAKE_HOURS;
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

DoomChanges doomSystem(World& w, double gameSeconds) {
    DoomChanges out;
    const float hours = (float)(gameSeconds / 3600.0);
    if (hours <= 0.0f) return out;
    for (Entity u : w.view<Unconscious>()) {
        if (w.has<Corpse>(u)) continue;
        Unconscious* un = w.get<Unconscious>(u);
        if (un->rested) {
            // Knocked out well rested: no Doom. It wakes when its wait runs out, and lies still playing dead if a
            // hostile unit is near (a carried one: carrySystem decides, by who carries it).
            un->doom = 0.0f;
            un->wake -= hours;
            if (un->wake <= 0.0f) {
                w.remove<Unconscious>(u);
                if (Needs* n = w.get<Needs>(u)) {
                    n->hp = std::max(n->hp, std::min(std::max(n->maxHp, 1), RECOVERY_HEALTH));
                    n->changed |= NEEDS_HP_CHANGED;
                    // Coming round from a knock-out is tiring
                    n->rested = std::max(0, n->rested - RESTED_WAKE_COST);
                }
                // Carried by anyone but a hostile, it is put down and stands up again
                if (const Carried* cd = w.get<Carried>(u); cd && !w.has<Hostile>(cd->by)) putDown(w, cd->by);
                queueLog(langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                out.woke.push_back(u);
                if (!w.has<Carried>(u) && hostileNear(w, u)) {
                    w.add(u, PlayingDead{});
                    queueLog(langf("MISC_PLAYS_DEAD", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                }
            }
            continue;
        }
        const Entity bed = occupiedProp(w, u);
        const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
        const float rest = bp ? doomRecoveryPerHour(bp->kind) : 0.0f;
        if (rest > 0.0f) {
            // In a bed or bedroll Doom falls, and at 0 the unit wakes with a little health, still lying there
            un->doom = std::clamp(un->doom - rest * hours, 0.0f, 100.0f);
            if (un->doom <= 0.0f) {
                w.remove<Unconscious>(u);
                if (Needs* n = w.get<Needs>(u)) {
                    n->hp = std::min(std::max(n->maxHp, 1), RECOVERY_HEALTH);
                    n->changed |= NEEDS_HP_CHANGED;
                }
                queueLog(langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                out.woke.push_back(u);
            }
            continue;
        }
        un->doom = std::clamp(un->doom + doomPerHour(statsOf(w, u)) * hours, 0.0f, 100.0f);
        if (un->doom >= 100.0f) {
            finishDoom(w, u);
            out.died.push_back(u);
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

void finishDoom(World& w, Entity u) {
    Unconscious* un = w.get<Unconscious>(u);
    if (!un || w.has<Corpse>(u)) return;
    // Dead for good: a corpse, no longer anyone's to command, still there to carry and to loot
    un->doom = 100.0f;
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

