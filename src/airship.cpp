#include "airship.h"
#include "build.h"
#include "systems_internal.h"

using namespace systems_detail;

bool isAirship(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    return p && propInfo(p->kind).airship;
}

int occupyCapacity(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p) return 0;
    return propInfo(p->kind).airship ? std::max(1, propInfo(p->kind).workers) : 1;
}

int occupantCount(World& w, ObjectId prop) {
    int n = 0;
    for (ObjectId u : w.view<Occupying>()) n += w.get<Occupying>(u)->prop == prop ? 1 : 0;
    return n;
}

ObjectId airshipOf(World& w, ObjectId u) {
    const Occupying* oc = w.get<Occupying>(u);
    return oc && isAirship(w, oc->prop) ? oc->prop : NO_OBJECT;
}

bool stuckAboard(World& w, ObjectId u) {
    const ObjectId ship = airshipOf(w, u);
    const AirshipState* st = ship != NO_OBJECT ? w.get<AirshipState>(ship) : nullptr;
    return st && !st->overOpen;
}

void flyAirship(World& w, ObjectId airship, Vec2 to) {
    if (!isAirship(w, airship)) return;
    AirshipState& st = w.has<AirshipState>(airship) ? *w.get<AirshipState>(airship) : w.add(airship, AirshipState{});
    st.flying = true;
    st.tx = to.x;
    st.tz = to.z;
}

void airshipSystem(World& w, const Terrain& t, float dt) {
    for (ObjectId ship : w.view<Prop, Transform>()) {
        if (!isAirship(w, ship)) continue;
        AirshipState& st = w.has<AirshipState>(ship) ? *w.get<AirshipState>(ship) : w.add(ship, AirshipState{});
        Transform* tr = w.get<Transform>(ship);
        Prop* prop = w.get<Prop>(ship);
        if (st.flying) {
            const float dx = st.tx - tr->x, dz = st.tz - tr->z, d = std::sqrt(dx * dx + dz * dz);
            if (d <= AIRSHIP_ARRIVED) {
                st.flying = false;
            } else {
                const float step = std::min(d, AIRSHIP_SPEED * dt);
                tr->x = std::clamp(tr->x + dx / d * step, 1.0f, (float)t.size() - 1.0f);
                tr->z = std::clamp(tr->z + dz / d * step, 1.0f, (float)t.size() - 1.0f);
                prop->yaw = std::atan2(dx, dz) - 1.5707963f;
            }
        }
        tr->y = std::max(t.heightAt(tr->x, tr->z), 0.0f) + AIRSHIP_LIFT;
        st.overOpen = t.walkable((int)std::floor(tr->x), (int)std::floor(tr->z));

        // Its occupants stand on deck, two by two
        const float c = std::cos(prop->yaw), s = std::sin(prop->yaw);
        int k = 0;
        for (ObjectId u : w.view<Occupying, Transform>()) {
            if (w.get<Occupying>(u)->prop != ship) continue;
            const float along = (k / 2) == 0 ? 0.5f : -0.5f, across = (k % 2) == 0 ? 0.3f : -0.3f;
            Transform* ut = w.get<Transform>(u);
            ut->x = tr->x + along * c - across * s;
            ut->z = tr->z + along * s + across * c;
            ut->y = tr->y + AIRSHIP_DECK;
            w.remove<SoloMove>(u);
            ++k;
        }
    }
}

bool disembark(World& w, Terrain& t, ObjectId u) {
    const ObjectId ship = airshipOf(w, u);
    if (ship == NO_OBJECT) return true;
    if (stuckAboard(w, u)) return false;
    w.remove<Occupying>(u);
    stepOffProp(w, t, u, ship);
    if (Transform* ut = w.get<Transform>(u)) ut->y = t.heightAt(ut->x, ut->z);
    return true;
}

void airshipLost(World& w, ObjectId airship) {
    if (!isAirship(w, airship)) return;
    std::vector<ObjectId> aboard;
    for (ObjectId u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == airship) aboard.push_back(u);
    }
    for (ObjectId u : aboard) {
        w.remove<Occupying>(u);
        queueLog(w, langf("MISC_AIRSHIP_FALLS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
        knockOut(w, u);
        finishDying(w, u);
    }
}

bool unreachableLand(const Terrain& t, float x, float z, int minTiles) {
    const int home = t.areaAt((int)std::floor(x), (int)std::floor(z));
    if (home <= 0) return false;
    const std::vector<int> sizes = t.areaSizes();
    for (size_t a = 1; a < sizes.size(); ++a) {
        if ((int)a != home && sizes[a] >= minTiles) return true;
    }
    return false;
}

ObjectId placeStartingAirship(World& w, Terrain& t, float x, float z) {
    const std::vector<ObjectId> props = w.view<Prop, Transform>();
    for (int ring = 3; ring <= 14; ++ring) {
        for (int k = 0; k < 8 * ring; ++k) {
            const float a = TWO_PI * (float)k / (float)(8 * ring);
            const float px = std::floor(x + std::cos(a) * (float)ring) + 0.5f, pz = std::floor(z + std::sin(a) * (float)ring) + 0.5f;
            if (!t.walkable((int)std::floor(px), (int)std::floor(pz))) continue;
            bool clear = true;
            for (size_t i = 0; i < props.size() && clear; ++i) {
                const Transform* tr = w.get<Transform>(props[i]);
                clear = (tr->x - px) * (tr->x - px) + (tr->z - pz) * (tr->z - pz) >= 9.0f;
            }
            if (!clear) continue;
            const ObjectId ship = placeBuiltProp(w, t, PropKind::Airship, px, pz, 0.0f, FACTION_PLAYER);
            if (ship != NO_OBJECT) w.add(ship, AirshipState{});
            return ship;
        }
    }
    return NO_OBJECT;
}
