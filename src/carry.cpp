// Carrying an Unconscious unit: the orders to pick one up and put it down, and the walks to do them.
#include "systems_internal.h"

using namespace systems_detail;

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
    lootOrderSystem(w, ev);
    return ev;
}

bool canBeCarried(World& w, Entity target) {
    if (!w.has<Unit>(target) || !w.has<Transform>(target) || w.has<Carried>(target) || w.has<Carrying>(target)) return false;
    return w.has<Unconscious>(target) || w.has<Commandable>(target);
}
