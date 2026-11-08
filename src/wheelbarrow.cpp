#include "wheelbarrow.h"
#include "systems_internal.h"

using namespace systems_detail;

bool isWheelbarrow(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    return p && propInfo(p->kind).wheelbarrow;
}

ObjectId barrowOf(World& w, ObjectId u) {
    const Pushing* p = w.get<Pushing>(u);
    return p ? p->barrow : NO_OBJECT;
}

ObjectId pusherOf(World& w, ObjectId barrow) {
    for (ObjectId u : w.view<Pushing>()) {
        if (w.get<Pushing>(u)->barrow == barrow) return u;
    }
    return NO_OBJECT;
}

bool startPushing(World& w, ObjectId u, ObjectId barrow) {
    if (!isWheelbarrow(w, barrow)) return false;
    if (barrowOf(w, u) == barrow) return true;
    const ObjectId other = pusherOf(w, barrow);
    if (other != NO_OBJECT && other != u) return false;
    const Transform* ut = w.get<Transform>(u);
    const Transform* bt = w.get<Transform>(barrow);
    if (!ut || !bt) return false;
    stopPushing(w, u);
    if (w.has<Carrying>(u)) putDown(w, u);
    w.remove<Occupying>(u);
    w.remove<IdleSpot>(u);
    // It turns to face the way from the unit to the Wheelbarrow, so it is picked up as it stands
    const float dx = bt->x - ut->x, dz = bt->z - ut->z;
    const float yaw = dx * dx + dz * dz > 1e-6f ? std::atan2(dx, dz) : w.get<Prop>(barrow)->yaw + 1.5707963f;
    w.add(u, Pushing{ barrow, yaw, ut->x, ut->z });
    return true;
}

void stopPushing(World& w, ObjectId u, bool logIt) {
    const Pushing* p = w.get<Pushing>(u);
    if (!p) return;
    const ObjectId barrow = p->barrow;
    w.remove<Pushing>(u);
    if (logIt && w.has<Prop>(barrow)) {
        queueLog(w, langf("MISC_LETS_GO_OF", { nameOf(w, u), lang(propInfo(w.get<Prop>(barrow)->kind).nameKey) }), LogKind::Other,
                 npcOnly(w, u));
    }
}

void pushingAttacked(World& w, ObjectId attacker, ObjectId target) {
    stopPushing(w, attacker, true);
    stopPushing(w, target, true);
}

void wheelbarrowSystem(World& w) {
    std::vector<ObjectId> held;
    for (ObjectId u : w.view<Pushing, Transform>()) {
        Pushing& p = *w.get<Pushing>(u);
        const ObjectId barrow = p.barrow;
        const Combat* c = w.get<Combat>(u);
        const bool fighting = w.has<AttackOrder>(u) || (c && c->target != NO_OBJECT);
        if (!isWheelbarrow(w, barrow) || !w.has<Transform>(barrow) || w.has<Blueprint>(barrow) ||
            std::find(held.begin(), held.end(), barrow) != held.end() || fighting) {
            stopPushing(w, u, fighting);
            continue;
        }
        // Hands it can't push with: out cold, carried or carrying, sitting or lying in something, aboard, climbing or upstairs
        if (outCold(w, u) || w.has<Carried>(u) || w.has<Carrying>(u) || w.has<Occupying>(u) || w.has<Boarding>(u) ||
            w.has<Climbing>(u) || storeyOf(w, u) != 0) {
            stopPushing(w, u);
            continue;
        }
        held.push_back(barrow);
        const Transform* ut = w.get<Transform>(u);
        const float dx = ut->x - p.lastX, dz = ut->z - p.lastZ;
        if (dx * dx + dz * dz > 0.0025f) {
            p.yaw = std::atan2(dx, dz);
            p.lastX = ut->x;
            p.lastZ = ut->z;
        }
        Transform* bt = w.get<Transform>(barrow);
        bt->x = ut->x + std::sin(p.yaw) * WHEELBARROW_AHEAD;
        bt->z = ut->z + std::cos(p.yaw) * WHEELBARROW_AHEAD;
        bt->y = ut->y;
        w.get<Prop>(barrow)->yaw = p.yaw - 1.5707963f;
        w.remove<Storey>(barrow);
    }
}
