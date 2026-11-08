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

bool startBoarding(World& w, ObjectId u, ObjectId ship) {
    if (!isAirship(w, ship) || airshipOf(w, u) == ship) return false;
    if (occupantCount(w, ship) >= occupyCapacity(w, ship)) return false;
    w.add(u, Boarding{ ship, AIRSHIP_BOARD_SECONDS, false });
    return true;
}

bool startLeaving(World& w, ObjectId u) {
    const ObjectId ship = airshipOf(w, u);
    if (ship == NO_OBJECT || stuckAboard(w, u)) return false;
    w.add(u, Boarding{ ship, AIRSHIP_LEAVE_SECONDS, true });
    return true;
}

namespace {
void boardingStep(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<Boarding>()) {
        Boarding& b = *w.get<Boarding>(u);
        const PropInfo* info = w.has<Prop>(b.ship) ? &propInfo(w.get<Prop>(b.ship)->kind) : nullptr;
        if (!info || outCold(w, u) || w.has<Carried>(u) || (b.leaving && airshipOf(w, u) != b.ship) ||
            (!b.leaving && (w.has<Occupying>(u) || flatGap(w, u, b.ship) > AIRSHIP_BOARD_REACH))) {
            w.remove<Boarding>(u);
            continue;
        }
        b.left -= dt;
        if (b.left > 0.0f) continue;
        const ObjectId ship = b.ship;
        const bool leaving = b.leaving;
        w.remove<Boarding>(u);
        if (leaving) {
            if (!disembark(w, t, u)) queueLog(w, lang("MISC_CANT_EVACUATE_AIRSHIP"), LogKind::Other, npcOnly(w, u));
            else queueSoundFrom(w, Sfx::Occupy, ship);
            continue;
        }
        // The room is checked again now: others may have got on meanwhile
        if (occupantCount(w, ship) >= occupyCapacity(w, ship)) {
            queueLog(w, langf("MISC_AIRSHIP_FULL", { nameOf(w, u), lang(info->nameKey) }), LogKind::Other, npcOnly(w, u));
            continue;
        }
        if (w.has<Carrying>(u)) putDown(w, u);
        w.remove<SoloMove>(u);
        w.remove<IdleSpot>(u);
        w.add(u, Occupying{ ship });
        queueSoundFrom(w, Sfx::Occupy, ship);
    }
}
}

namespace {
// Where a unit is, for reach: the ground (0), an Airship's deck, or a building's upper floor
long long standingLevel(World& w, ObjectId u) {
    if (const ObjectId ship = airshipOf(w, u); ship != NO_OBJECT) return 1 + 2 * (long long)ship;
    if (const Upstairs* up = w.get<Upstairs>(u)) return 2 + 2 * (long long)up->foundation;
    return 0;
}
}

bool elevated(World& w, ObjectId u) { return airshipOf(w, u) != NO_OBJECT || w.has<Upstairs>(u); }

bool canStrike(World& w, ObjectId attacker, ObjectId target) {
    if (reachOf(w, attacker) > ATTACK_REACH) return true;   // ranged, with ammunition
    return standingLevel(w, attacker) == standingLevel(w, target);
}

void fleeFromAirship(World& w, ObjectId attacker, ObjectId target) {
    if (!elevated(w, attacker) || w.has<Commandable>(target) || canStrike(w, target, attacker)) return;
    if (Fleeing* f = w.get<Fleeing>(target)) {
        f->from = attacker;
        f->left = PROVOKED_SECONDS;
        return;
    }
    w.add(target, Fleeing{ attacker, PROVOKED_SECONDS });
    queueLog(w, langf("MISC_FLEES", { nameOf(w, target) }), LogKind::Other, npcOnly(w, target));
}

void fleeSystem(World& w, Terrain& t, float dt) {
    static const float TURNS[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f };
    for (ObjectId u : w.view<Fleeing, Transform>()) {
        Fleeing& f = *w.get<Fleeing>(u);
        if (w.has<Commandable>(u) || outCold(w, u) || w.has<Carried>(u)) {
            w.remove<Fleeing>(u);
            continue;
        }
        if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
        w.remove<AttackOrder>(u);
        const Transform* ft = w.get<Transform>(f.from);
        const bool danger = ft && w.has<Entity>(f.from) && !outCold(w, f.from) && elevated(w, f.from) &&
                            flatGap(w, u, f.from) <= reachOf(w, f.from) + FLEE_MARGIN;
        if (!danger) {
            f.left -= dt;
            if (f.left <= 0.0f) w.remove<Fleeing>(u);
            continue;
        }
        f.left = PROVOKED_SECONDS;
        const SoloMove* so = w.get<SoloMove>(u);
        if (so && so->next < so->waypoints.size()) continue;
        const Transform* ut = w.get<Transform>(u);
        const float away = std::atan2(ut->z - ft->z, ut->x - ft->x);
        const float run = reachOf(w, f.from) + FLEE_MARGIN + 2.0f;
        for (float turn : TURNS) {
            const float x = ft->x + std::cos(away + turn) * run, z = ft->z + std::sin(away + turn) * run;
            if (!openTile(t, x, z)) continue;
            if (Behaviors* b = w.get<Behaviors>(u); b && b->busy) stopAutoStep(w, u, *b);
            walkSolo(w, t, u, Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f });
            break;
        }
    }
}

void airshipSystem(World& w, Terrain& t, float dt) {
    boardingStep(w, t, dt);
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
