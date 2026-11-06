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
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
        }
    }
    for (Entity c : w.view<Carried>()) {
        const Entity by = w.get<Carried>(c)->by;
        const Carrying* cr = w.get<Carrying>(by);
        if (!cr || cr->unit != c || outCold(w, by) || !w.has<Transform>(by)) {
            w.remove<Carried>(c);
            w.remove<PlayingDead>(c);
            if (cr && cr->unit == c) w.remove<Carrying>(by);
            continue;
        }
        if (outCold(w, c)) {
            w.remove<PlayingDead>(c);
        } else if (w.has<Commandable>(c) && w.has<Commandable>(by)) {
            w.remove<PlayingDead>(c);
        } else if (w.has<Hostile>(by) && canPlayDead(w, c)) {
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
        else if (w.has<Occupying>(u)) putDown(w, u);
    }
    lootOrderSystem(w, ev);
    return ev;
}

bool canBeCarried(World& w, Entity target) {
    if (!w.has<Unit>(target) || !w.has<Transform>(target) || w.has<Carried>(target) || w.has<Carrying>(target)) return false;
    return outCold(w, target) || w.has<Commandable>(target);
}
