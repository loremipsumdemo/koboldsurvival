// The systems that fit no bigger group: needs, dropping a stack, turning a unit hostile, disbanding, what a unit
// is doing (its task name and emotes), and whether it can act. The rest of the systems are split by job:
//   world_setup.cpp  the start of a level: the spawn, the starting units, camps, mobs, loot and ground items
//   movement.cpp     move, pickup, attack, talk, follow and defend orders; squads, steering and defending
//   prop_use.cpp     using props: prop orders, work, crafting, storage and passive crafting
//   automation.cpp   behavior rules: hauling, harvesting, resting, patrols, roaming and wild mobs
//   downed.cpp       unconscious units, doom and corpses; carrying and looting
// Helpers more than one of them uses are in systems_internal.h.
#include "systems_internal.h"

using namespace systems_detail;

bool dropStack(World& w, Entity unit, int slot) {
    Inventory* inv = w.get<Inventory>(unit);
    const Transform* tr = w.get<Transform>(unit);
    if (!inv || !tr || slot < 0 || slot >= INVENTORY_SLOTS) return false;

    ItemStack& s = inv->slots[slot];
    if (s.item == 0 || s.count <= 0) return false;

    dropAtUnit(w, unit, s.item, s.count);
    s = ItemStack{};
    return true;
}

void needsSystem(World& w, int hours, double gameSeconds) {
    for (Entity e : w.view<Needs>()) {
        if (w.has<Corpse>(e)) continue;   // a corpse needs nothing
        Needs* n = w.get<Needs>(e);
        const bool mob = w.has<Mob>(e);
        const double gameHours = gameSeconds / 3600.0;
        // The most Rested follows Vitality. A unit that was fully rested stays fully rested as it grows.
        if (const int maxR = maxRestedFor(statsOf(w, e)); maxR != n->maxRested) {
            const bool full = n->rested >= n->maxRested;
            n->maxRested = maxR;
            n->rested = full ? maxR : std::min(n->rested, maxR);
        }
        if (mob) {
            // A mob can't feed itself, so it has no hunger to lose (and none to starve from), and no Comfort. Its
            // Rested never falls by itself and rises a little every hour, more in a bed or bedroll.
            n->hunger = n->maxHunger;
            n->hurried = false;
            const Entity bed = occupiedProp(w, e);
            const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
            const double restGain = std::max(MOB_RESTED_GAIN_PER_HOUR, bp ? restedGainPerHour(bp->kind) : 0.0);
            driftNeed(n->rested, n->maxRested, n->restedPart, restGain, gameHours);
        } else {
            // A unit that moved at run pace in the hour that just ended pays double for its next tick. The mark is
            // used up here, so later hours cost the usual amount.
            int ticks = hours;
            if (hours > 0 && n->hurried) {
                ticks += 1;
                n->hurried = false;
            }
            tickNeeds(*n, ticks);
            // An Exile never goes hungry
            if (const Faction* fa = w.get<Faction>(e); fa && fa->id == FACTION_EXILE) n->hunger = n->maxHunger;

            // Rested rises lying in a bed or bedroll and falls while awake anywhere else; Comfort rises in a seat or
            // bed without a lock and falls anywhere else
            const Entity seat = occupiedProp(w, e);
            const Prop* sp = seat != NULL_ENTITY ? w.get<Prop>(seat) : nullptr;
            const double restGain = sp ? restedGainPerHour(sp->kind) : 0.0;
            const double comfortGain = sp ? comfortGainPerHour(sp->kind) : 0.0;
            const double restRate = restGain > 0.0 ? restGain : w.has<Unconscious>(e) ? 0.0 : -RESTED_LOSS_PER_HOUR;
            driftNeed(n->rested, n->maxRested, n->restedPart, restRate, gameHours);
            driftNeed(n->comfort, n->maxComfort, n->comfortPart, comfortGain > 0.0 ? comfortGain : -COMFORT_LOSS_PER_HOUR,
                      gameHours);
        }

        // An Unconscious unit does not heal: only Doom falling to 0 in a bed, or a rested one's wait running out, wakes it
        if (w.has<Unconscious>(e)) continue;
        const Combat* c = w.get<Combat>(e);
        regenHealth(*n, c && c->target != NULL_ENTITY, gameSeconds, mob);
    }
}

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

bool unitIdle(World& w, Entity u) {
    if (w.has<Corpse>(u) || w.has<Unconscious>(u) || w.has<PlayingDead>(u) || w.has<Carried>(u)) return false;
    if (const Behaviors* b = w.get<Behaviors>(u); b && (b->busy || b->seeking)) return false;
    if (w.has<Working>(u) || w.has<PropOrder>(u) || w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<CarryOrder>(u) ||
        w.has<PutDownOrder>(u) || w.has<LootOrder>(u) || w.has<DefendOrder>(u) || w.has<FollowOrder>(u) ||
        w.has<AttackOrder>(u) || w.has<TalkOrder>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u)) {
        return false;
    }
    if (const Combat* c = w.get<Combat>(u); c && c->target != NULL_ENTITY) return false;
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size()) return false;
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return false;
    }
    return true;
}

std::string taskNameOf(World& w, Entity u) {
    if (w.has<Corpse>(u)) return lang("UILABEL_TASK_DEAD");
    if (w.has<Unconscious>(u)) return lang("UILABEL_TASK_UNCONSCIOUS");
    if (w.has<PlayingDead>(u)) return lang("UILABEL_TASK_PLAYING_DEAD");
    if (w.has<Carried>(u)) return lang("UILABEL_TASK_CARRIED");
    // A step of its own behaviors: the behavior's name, such as Auto-Harvest: Tree, while it is on its way or at it
    if (const Behaviors* b = w.get<Behaviors>(u)) {
        if (b->seeking) return langf("UILABEL_TASK_SHARE_WALK", { nameOf(w, b->seek) });
        const PropOrder* po = w.get<PropOrder>(u);
        const Working* wk = w.get<Working>(u);
        if (b->busy && po && po->automatic) return langf("UILABEL_TASK_AUTO_GOING", { behaviorName(b->step) });
        if (b->busy && wk && !wk->unlocking && wk->prop == b->stepProp) return behaviorName(b->step);
    }
    Entity prop = NULL_ENTITY;
    if (const Working* g = w.get<Working>(u); g && g->unlocking) return statName(StatId::Lockpicking);
    if (const Working* g = w.get<Working>(u); g && w.has<Prop>(g->prop) && w.get<Prop>(g->prop)->kind == PropKind::Debris) {
        return lang("UILABEL_TASK_CLEARING");
    }
    if (const Working* g = w.get<Working>(u)) prop = g->prop;
    else if (const PropOrder* po = w.get<PropOrder>(u); po && useOf(w, po->prop) == PropUse::Resource) prop = po->prop;
    if (const Prop* p = prop != NULL_ENTITY ? w.get<Prop>(prop) : nullptr) return statName(propInfo(p->kind).stat);
    if (const Occupying* oc = w.get<Occupying>(u)) {
        if (lockedIn(w, u)) return lang("UILABEL_TASK_LOCKED_IN");
        const Prop* p = w.get<Prop>(oc->prop);
        return lang(p && isSeat(p->kind) ? "UILABEL_TASK_SITTING" : "UILABEL_TASK_RESTING");
    }
    if (w.has<Carrying>(u) || w.has<CarryOrder>(u)) return lang("UILABEL_TASK_CARRYING");
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->action == PropAction::Store) return lang("UILABEL_TASK_STORING");
    if (w.has<DefendOrder>(u)) return lang("UILABEL_TASK_DEFENDING");
    if (w.has<FollowOrder>(u)) return lang("UILABEL_TASK_FOLLOWING");
    const SoloMove* so = w.get<SoloMove>(u);
    if (so && so->next < so->waypoints.size()) return lang("UILABEL_TASK_MOVING");
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return lang("UILABEL_TASK_MOVING");
    }
    return lang("UILABEL_TASK_IDLE");
}

std::vector<EmoteId> emotesOf(World& w, Entity u) {
    // Out cold or dead, that is all it shows
    if (w.has<Corpse>(u)) return { EmoteId::Dead };
    if (w.has<Unconscious>(u)) return { EmoteId::Unconscious };
    if (w.has<PlayingDead>(u)) return { EmoteId::PlayingDead };
    std::vector<EmoteId> out;
    auto propEmoteOf = [&](Entity prop) {
        const Prop* p = w.get<Prop>(prop);
        return p ? propEmote(p->kind) : EmoteId::None;
    };
    if (const Working* wk = w.get<Working>(u)) {
        if (wk->unlocking) out.push_back(EmoteId::Lockpicking);
        else if (propEmoteOf(wk->prop) != EmoteId::None) out.push_back(propEmoteOf(wk->prop));
    }
    if (const Occupying* oc = w.get<Occupying>(u); oc && propEmoteOf(oc->prop) != EmoteId::None) out.push_back(propEmoteOf(oc->prop));
    if (lockedIn(w, u)) out.push_back(EmoteId::LockedIn);
    if (w.has<Inventory>(u) && w.has<Stats>(u)) {
        const float limit = carryLimit(statsOf(w, u));
        if (limit > 0.0f && carriedWeightOf(w, u) >= ENCUMBERED_SHARE * limit) out.push_back(EmoteId::Encumbered);
    }
    if (const Needs* n = w.get<Needs>(u); n && n->hunger < 1) out.push_back(EmoteId::Hungry);
    return out;
}

bool cannotAct(World& w, Entity u) {
    // Out cold, shut in a cage, or carried (one playing dead can still break free with an order: cannotTakeOrder)
    return w.has<Unconscious>(u) || lockedIn(w, u) || (w.has<Carried>(u) && !w.has<PlayingDead>(u));
}

bool cannotTakeOrder(World& w, Entity u) {
    if (w.has<PlayingDead>(u)) breakFree(w, u);
    return cannotAct(w, u);
}

void breakFree(World& w, Entity u) {
    w.remove<PlayingDead>(u);
    const Carried* c = w.get<Carried>(u);
    if (!c) return;
    const Entity carrier = c->by;
    w.remove<Carried>(u);
    if (const Carrying* cr = w.get<Carrying>(carrier); cr && cr->unit == u) w.remove<Carrying>(carrier);
    if (Transform* t = w.get<Transform>(u)) {
        if (const Transform* ct = w.get<Transform>(carrier)) *t = *ct;
    }
    queueLog(langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
}

bool canBeCarried(World& w, Entity target) {
    if (!w.has<Unit>(target) || !w.has<Transform>(target) || w.has<Carried>(target) || w.has<Carrying>(target)) return false;
    return w.has<Unconscious>(target) || w.has<Commandable>(target);
}
