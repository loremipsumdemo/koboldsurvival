// Unconscious units, doom and corpses, playing dead; carrying a unit and putting it down; looting.
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
    leaveSquad(w, u);
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

bool issueCarryOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target) {
    const Transform* tt = w.get<Transform>(target);
    if (!tt || !canBeCarried(w, target)) return false;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || w.has<Carrying>(u)) continue;
        if (cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        walkSolo(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, CarryOrder{ target });
        return true;
    }
    return false;
}

void issuePutDownOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 point) {
    for (Entity u : units) {
        if (!w.has<Carrying>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        walkSolo(w, t, u, point);
        w.add(u, PutDownOrder{ point.x, point.z });
    }
}

void putDown(World& w, Entity carrier) {
    const Carrying* cr = w.get<Carrying>(carrier);
    if (!cr) return;
    const Entity carried = cr->unit;
    w.remove<Carrying>(carrier);
    w.remove<Carried>(carried);
    w.remove<PlayingDead>(carried);
    if (Transform* ct = w.get<Transform>(carried)) {
        if (const Transform* t = w.get<Transform>(carrier)) *ct = *t;
        queueLog(langf("MISC_PUTS_DOWN", { nameOf(w, carrier), nameOf(w, carried) }), LogKind::Other, npcOnly(w, carrier, carried));
        queueSoundFrom(w, Sfx::PutDown, carrier);
    }
}

CarryEvents carrySystem(World& w) {
    CarryEvents ev;
    std::vector<Entity>& picked = ev.picked;
    // Walking to pick someone up: in reach, it is lifted, unless someone else got there first
    for (Entity u : w.view<CarryOrder, Transform>()) {
        const Entity target = w.get<CarryOrder>(u)->target;
        if (!canBeCarried(w, target) || w.has<Carrying>(u) || cannotAct(w, u)) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (flatGap(w, u, target) <= CARRY_REACH) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
            // Whatever it was doing ends: it is lifted, out of a bed if it lay in one
            leaveSquad(w, target);
            clearOrders(w, target);
            w.remove<IdleSpot>(target);
            if (Combat* c = w.get<Combat>(target)) c->target = NULL_ENTITY;
            w.add(target, Carried{ u });
            w.add(u, Carrying{ target });
            queueLog(langf("MISC_PICKS_UP_UNIT", { nameOf(w, u), nameOf(w, target) }), LogKind::Other, npcOnly(w, u, target));
            queueSoundFrom(w, Sfx::CarryUp, u);
            picked.push_back(u);
            continue;
        }
        // Stopped short with no route left: it can't be reached, so give up
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
        }
    }
    // Walking to put someone down: at the spot, or as near as the route got, it is put down
    for (Entity u : w.view<PutDownOrder, Transform>()) {
        if (!w.has<Carrying>(u)) {
            w.remove<PutDownOrder>(u);
            continue;
        }
        const PutDownOrder pd = *w.get<PutDownOrder>(u);
        const Transform* tr = w.get<Transform>(u);
        const SoloMove* so = w.get<SoloMove>(u);
        const bool there = std::hypot(pd.x - tr->x, pd.z - tr->z) <= 0.6f;
        if (there || !so || so->next >= so->waypoints.size()) {
            w.remove<PutDownOrder>(u);
            w.remove<SoloMove>(u);
            putDown(w, u);
        }
    }
    // Every carried unit goes where its carrier is. A carrier that is gone, knocked out or no longer holding it drops it.
    // Awake: with its own squad it rides along; with a hostile it plays dead; with anyone else it gets free at once.
    for (Entity c : w.view<Carried>()) {
        const Entity by = w.get<Carried>(c)->by;
        const Carrying* cr = w.get<Carrying>(by);
        if (!cr || cr->unit != c || w.has<Unconscious>(by) || !w.has<Transform>(by)) {
            w.remove<Carried>(c);
            w.remove<PlayingDead>(c);
            if (cr && cr->unit == c) w.remove<Carrying>(by);
            continue;
        }
        if (w.has<Unconscious>(c)) {
            w.remove<PlayingDead>(c);
        } else if (w.has<Commandable>(c) && w.has<Commandable>(by)) {
            w.remove<PlayingDead>(c);
        } else if (w.has<Hostile>(by)) {
            if (!w.has<PlayingDead>(c)) {
                w.add(c, PlayingDead{});
                queueLog(langf("MISC_PLAYS_DEAD", { nameOf(w, c) }), LogKind::Other, npcOnly(w, c));
            }
        } else {
            breakFree(w, c);
            continue;
        }
        if (Transform* ct = w.get<Transform>(c)) *ct = *w.get<Transform>(by);
    }
    for (Entity u : w.view<Carrying>()) {
        const Entity c = w.get<Carrying>(u)->unit;
        const Carried* cd = w.get<Carried>(c);
        if (!cd || cd->by != u) w.remove<Carrying>(u);
        else if (w.has<Occupying>(u)) putDown(w, u);   // a unit never carries anyone while it occupies a prop
    }
    // Walking to loot: in reach, the game opens the panel
    for (Entity u : w.view<LootOrder, Transform>()) {
        const Entity target = w.get<LootOrder>(u)->target;
        if (!w.has<Unconscious>(target) || w.has<Unconscious>(u)) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (flatGap(w, u, target) <= CARRY_REACH) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
            ev.loot.push_back({ u, target });
            continue;
        }
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
        }
    }
    return ev;
}

bool issueLootOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target) {
    const Transform* tt = w.get<Transform>(target);
    if (!w.has<Unconscious>(target) || !tt) return false;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<Mob>(u)) continue;   // a mob takes nothing (mobs.h)
        clearOrders(w, u);
        walkSolo(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, LootOrder{ target });
        return true;
    }
    return false;
}

bool canHoldItem(World& w, Entity unit, int item) {
    const Mob* m = w.get<Mob>(unit);
    return !m || mobCanHold(m->kind, item);
}

bool inventoryLocked(World& w, Entity unit) {
    return w.has<Mob>(unit) && !w.has<Unconscious>(unit);
}

bool lootTake(World& w, Entity looter, Entity target, bool gear, int slot, std::string& why) {
    Inventory* into = w.get<Inventory>(looter);
    if (!into || !w.has<Unconscious>(target)) return false;
    if (w.has<Mob>(looter)) {   // a mob takes nothing but what it was born with
        why = lang("MISC_MOB_CANT_HOLD");
        return false;
    }
    int item = 0, count = 0;
    if (gear) {
        Equipment* worn = w.get<Equipment>(target);
        if (!worn || slot < 0 || slot >= GEAR_SLOTS || worn->items[slot] == 0) return false;
        item = worn->items[slot];
        count = 1;
        if (addToInventory(*into, item, 1) > 0) {
            why = lang("MISC_LOOT_NO_ROOM");
            return false;
        }
        worn->items[slot] = 0;
    } else {
        Inventory* from = w.get<Inventory>(target);
        if (!from || slot < 0 || slot >= INVENTORY_SLOTS || from->slots[slot].item == 0) return false;
        ItemStack& st = from->slots[slot];
        item = st.item;
        const int left = addToInventory(*into, st.item, st.count);
        count = st.count - left;
        if (count <= 0) {
            why = lang("MISC_LOOT_NO_ROOM");
            return false;
        }
        if (left > 0) st.count = left;
        else st = ItemStack{};
    }
    queueUnitLabel(looter, itemGainLabelFor(count, item));
    queueSound(Sfx::Take);
    // Anything taken out of an Unconscious mob kills it: it goes to 0 health and its Doom completes at once
    if (w.has<Mob>(target) && !w.has<Corpse>(target)) {
        if (Needs* n = w.get<Needs>(target)) {
            n->hp = 0;
            n->changed |= NEEDS_HP_CHANGED;
        }
        queueLog(langf("MISC_MEAT_TAKEN_DIES", { nameOf(w, target) }), LogKind::Other, npcOnly(w, target));
        finishDoom(w, target);
        queueSoundFrom(w, Sfx::Dies, target);
    }
    return true;
}

int lootAll(World& w, Entity looter, Entity target) {
    int moved = 0;
    std::string why;
    for (int g = 0; g < GEAR_SLOTS; ++g) moved += lootTake(w, looter, target, true, g, why) ? 1 : 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) moved += lootTake(w, looter, target, false, i, why) ? 1 : 0;
    return moved;
}
