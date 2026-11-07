#include "systems_internal.h"
#include "build.h"

using namespace systems_detail;

static bool inBedOrBedroll(World& w, ObjectId u) {
    const ObjectId bed = occupiedProp(w, u);
    const Prop* p = bed != NO_OBJECT ? w.get<Prop>(bed) : nullptr;
    return p && isBed(p->kind);
}

void knockOut(World& w, ObjectId u) {
    if (!w.has<Entity>(u) || outCold(w, u)) return;
    const ObjectId bed = occupiedProp(w, u);
    const bool keepBed = inBedOrBedroll(w, u);
    if (w.has<Carrying>(u)) putDown(w, u);
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    if (keepBed) w.add(u, Occupying{ bed });
    if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
    if (Needs* n = w.get<Needs>(u)) {
        if (n->hp != 0) n->changed |= NEEDS_HP_CHANGED;
        n->hp = 0;
    }
    w.remove<PlayingDead>(u);
    Unconscious un;
    if (Combat* c = w.get<Combat>(u)) {
        un.koFaction = c->downedByFaction;
        c->downedByFaction = -1;
    }
    w.add(u, un);
    queueLog(w, langf("MISC_FALLS_UNCONSCIOUS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    queueSoundFrom(w, Sfx::Downed, u);
    const int victim = factionOf(w, u);
    if (un.koFaction == FACTION_PLAYER && victim != FACTION_PLAYER && !isMobFaction(victim)) {
        const int now = changePlayerReputation(w, victim, -REP_KO_PENALTY);
        queueLog(w, langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_KO_PENALTY), std::to_string(now) }),
                 LogKind::Other);
    }
}

void downedSystem(World& w) {
    for (ObjectId u : w.view<Entity, Needs>()) {
        if (w.get<Needs>(u)->hp <= 0 && !outCold(w, u)) knockOut(w, u);
    }
}

static void wakeUp(World& w, ObjectId u, WakeChanges& out) {
    w.remove<Unconscious>(u);
    if (Needs* n = w.get<Needs>(u)) {
        n->hp = std::max(n->hp, std::min(std::max(n->maxHp, 1), RECOVERY_HEALTH));
        n->changed |= NEEDS_HP_CHANGED;
    }
    if (const Carried* cd = w.get<Carried>(u); cd && !w.has<Hostile>(cd->by)) putDown(w, cd->by);
    queueLog(w, langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    out.woke.push_back(u);
    if (inBedOrBedroll(w, u)) {
        const Behaviors* b = w.get<Behaviors>(u);
        const bool leaves = b && std::any_of(b->rules.begin(), b->rules.end(),
                                             [](const BehaviorRule& r) { return r.on && r.kind == BehaviorKind::LeaveBed; });
        if (!leaves) w.remove<Occupying>(u);
    } else if (canPlayDead(w, u) && !w.has<Carried>(u) && hostileNear(w, u)) {
        w.add(u, PlayingDead{});
        queueLog(w, langf("MISC_PLAYS_DEAD", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

WakeChanges unconsciousSystem(World& w, double gameSeconds) {
    WakeChanges out;
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return out;
    for (ObjectId u : w.view<Unconscious>()) {
        Unconscious* un = w.get<Unconscious>(u);
        Needs* n = w.get<Needs>(u);
        const float gain = 1.0f + (n && !w.has<Mob>(u) && n->maxComfort > 0 ? (float)n->comfort / (float)n->maxComfort : 0.0f);
        const bool bed = inBedOrBedroll(w, u);
        un->tickPart += CONSCIOUS_TICKS_PER_HOUR * hours;
        const int ticks = (int)un->tickPart;
        un->tickPart -= ticks;
        for (int i = 0; i < ticks; ++i) {
            if (w.has<Mob>(u)) {
            } else if (n && n->rested > 0) {
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

void corpseDecaySystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    for (ObjectId u : w.view<Corpse, Transform>()) {
        Corpse* corpse = w.get<Corpse>(u);
        corpse->hours += hours;
        if (corpse->hours < CORPSE_BONES_HOURS || w.has<Carried>(u)) continue;
        const Transform at = *w.get<Transform>(u);
        std::vector<ItemStack> belongings = unitBelongings(w, u);
        w.destroy(u);
        const float yaw = TWO_PI * (float)(u % 16u) / 16.0f;
        const ObjectId bones = makeProp(w, PropKind::Bones, at, yaw);
        w.add(bones, BonesPile{ 0.0, std::move(belongings) });
    }
}

void bonesDecaySystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (!destroyedByHand(w.get<Prop>(e)->kind)) continue;
        BonesPile* pile = w.get<BonesPile>(e);
        if (!pile) pile = &w.add(e, BonesPile{});
        pile->hours += hours;
        if (pile->hours >= BONES_DECAY_HOURS) breakBuilding(w, e);
    }
}

void groundFoodSystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    for (ObjectId e : w.view<GroundItem>()) {
        GroundItem* g = w.get<GroundItem>(e);
        if (g->hours >= GROUND_FOOD_HOURS) continue;
        g->hours += hours;
        if (g->hours >= GROUND_FOOD_HOURS && spoilsOnGround(g->item)) w.destroy(e);
    }
}

bool hostileNear(World& w, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    const int mine = factionOf(w, u);
    for (ObjectId o : w.view<Entity, Transform>()) {
        if (o == u || outCold(w, o) || w.has<PlayingDead>(o) || w.has<Carried>(o)) continue;
        if (!factionsHostile(w, mine, factionOf(w, o))) continue;
        const Transform* ot = w.get<Transform>(o);
        const float dx = ot->x - ut->x, dz = ot->z - ut->z;
        if (dx * dx + dz * dz <= RESTED_HOSTILE_RANGE * RESTED_HOSTILE_RANGE) return true;
    }
    return false;
}

void playDeadSystem(World& w) {
    for (ObjectId u : w.view<PlayingDead>()) {
        if (w.has<Carried>(u) || outCold(w, u)) continue;
        if (hostileNear(w, u)) continue;
        w.remove<PlayingDead>(u);
        queueLog(w, langf("MISC_GETS_UP", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

void finishDying(World& w, ObjectId u) {
    const Unconscious* un = w.get<Unconscious>(u);
    if (!un) return;
    const int koFaction = un->koFaction;
    w.remove<Unconscious>(u);
    w.remove<Stats>(u);
    w.remove<XpBuffer>(u);
    w.add(u, Corpse{});
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.remove<Occupying>(u);
    queueLog(w, langf("MISC_DIES", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    const int victim = factionOf(w, u);
    if (koFaction == FACTION_PLAYER && victim != FACTION_PLAYER && !isMobFaction(victim)) {
        const int now = changePlayerReputation(w, victim, -REP_DEATH_PENALTY);
        queueLog(w, langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_DEATH_PENALTY), std::to_string(now) }),
                 LogKind::Other);
    }
}

