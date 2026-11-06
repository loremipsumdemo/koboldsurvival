#include "systems_internal.h"

using namespace systems_detail;

void squadSystem(World& w, float dt, bool running) {
    for (Entity e : w.view<ActiveSquad>()) {
        ActiveSquad* sq = w.get<ActiveSquad>(e);
        if (sq->arrived) continue;

        float slowest = -1.0f;
        for (Entity m : w.view<ActiveSquadMember, Unit>()) {
            if (w.get<ActiveSquadMember>(m)->squad != e) continue;
            const float s = walkSpeedOf(w, m, running);
            if (s > 0.0f && (slowest < 0.0f || s < slowest)) slowest = s;
        }
        const float pace = slowest < 0.0f ? 0.0f : running ? slowest : std::min(sq->speed, slowest);

        Vec2 wp = sq->waypoints[sq->next];
        float tx = wp.x, tz = wp.z;
        float dx = tx - sq->cx, dz = tz - sq->cz;
        float d = std::sqrt(dx * dx + dz * dz);
        float step = pace * dt;

        if (d > 1e-4f) {
            sq->headX = dx / d;
            sq->headZ = dz / d;
        }

        if (d <= step) {
            sq->cx = tx;
            sq->cz = tz;
            if (++sq->next >= sq->waypoints.size()) sq->arrived = true;
        } else {
            sq->cx += dx / d * step;
            sq->cz += dz / d * step;
        }
    }
}

namespace {
Entity leadOf(World& w, Entity e) {
    if (const FollowOrder* f = w.get<FollowOrder>(e)) return f->target;
    if (const DefendOrder* d = w.get<DefendOrder>(e); d && !w.has<AttackOrder>(e)) return d->ward;
    return NULL_ENTITY;
}

bool closeEnoughToLead(World& w, const std::vector<Entity>& units, Entity e, Entity lead) {
    const Transform* tr = w.get<Transform>(e);
    const Transform* lt = w.get<Transform>(lead);
    if (!tr || !lt) return true;
    const float gx = lt->x - tr->x, gz = lt->z - tr->z;
    const float myGap = std::sqrt(gx * gx + gz * gz);
    if (myGap <= FOLLOW_DISTANCE) return true;
    const Unit* mu = w.get<Unit>(e);
    int crowd = 0;
    for (Entity o : units) crowd += leadOf(w, o) == lead ? 1 : 0;
    const float spacing = 2.0f * mu->radius + UNIT_CLEARANCE;
    if (myGap > FOLLOW_DISTANCE + 0.75f * spacing * std::sqrt((float)crowd)) return false;
    for (Entity o : units) {
        if (o == e || o == lead || leadOf(w, o) != lead) continue;
        const Transform* ot = w.get<Transform>(o);
        const Unit* ou = w.get<Unit>(o);
        const float ox = ot->x - tr->x, oz = ot->z - tr->z;
        const float touching = mu->radius + ou->radius + UNIT_CLEARANCE + 0.3f;
        const float gap = std::sqrt(ox * ox + oz * oz);
        if (gap > touching || gap < 1e-4f) continue;
        const float theirGap = std::hypot(lt->x - ot->x, lt->z - ot->z);
        if (theirGap >= myGap) continue;
        if (ox * gx + oz * gz < 0.5f * gap * myGap) continue;
        return true;
    }
    return false;
}
}

namespace {
constexpr float unitWidth = 2.0f * 0.5f + UNIT_CLEARANCE;
constexpr float CHASE_CHECK_SECONDS = 0.4f;
constexpr float CHASE_PLAN_SECONDS = 1.0f;
constexpr float CHASE_REPLAN_DRIFT = 2.0f;
constexpr float CHASE_WAYPOINT_REACH = 0.3f;
constexpr int CHASE_PLANS_PER_STEP = 4;
constexpr float SLOT_ROUTE_DISTANCE = 1.5f;

using Vec2f = std::pair<float, float>;

std::int64_t cellKey(int cx, int cz) { return (std::int64_t)(((std::uint64_t)(std::uint32_t)cx << 32) | (std::uint32_t)cz); }

struct SteerFrame {
    std::vector<Entity> units;
    std::vector<Vec2f> vel;
    std::vector<float> speeds;
    std::vector<Vec2f> goal;
    std::vector<char> going, pinned;
    std::vector<Entity> calmLead;
    std::vector<Entity> attacked;
    float dt = 0.0f;
    int plansLeft = CHASE_PLANS_PER_STEP;
    std::vector<Vec2f> pos;
    std::vector<std::pair<std::int64_t, std::uint32_t>> byCell;
    bool underAttack(Entity e) const { return std::binary_search(attacked.begin(), attacked.end(), e); }
};

Vec2 chaseStep(World& w, const Terrain& t, SteerFrame& s, Entity e, const Transform* tr, Vec2 goal) {
    ChaseRoute* r = w.get<ChaseRoute>(e);
    if (!r) r = &w.add(e, ChaseRoute{});
    const Vec2 from{ tr->x, tr->z };
    r->checkIn -= s.dt;
    r->planIn -= s.dt;
    if (r->checkIn <= 0.0f) {
        r->checkIn = CHASE_CHECK_SECONDS;
        r->blocked = !t.lineClear(from, goal);
        if (!r->blocked) r->waypoints.clear();
    }
    if (!r->blocked) return goal;

    const bool finished = r->next >= r->waypoints.size();
    const bool drifted = std::hypot(goal.x - r->goal.x, goal.z - r->goal.z) > CHASE_REPLAN_DRIFT;
    if ((finished || drifted) && r->planIn <= 0.0f && s.plansLeft > 0) {
        --s.plansLeft;
        r->planIn = CHASE_PLAN_SECONDS;
        r->waypoints = t.findPath(from, goal);
        r->next = 0;
        r->goal = goal;
    }
    while (r->next < r->waypoints.size()) {
        const Vec2 wp = r->waypoints[r->next];
        if (std::hypot(wp.x - from.x, wp.z - from.z) > CHASE_WAYPOINT_REACH) return wp;
        ++r->next;
    }
    return goal;
}

void steerGoal(World& w, Terrain& t, SteerFrame& s, size_t i, bool running) {
    const std::vector<Entity>& units = s.units;
    auto underAttack = [&](Entity e) { return s.underAttack(e); };
    Entity e = units[i];
    Transform* tr = w.get<Transform>(e);

    float tx = tr->x, tz = tr->z;
    float leadGap = -1.0f;
    bool chasing = false;
    if (w.has<Occupying>(e)) {
        s.speeds[i] = 0.0f;
        s.pinned[i] = 1;
        s.goal[i] = { tr->x, tr->z };
        return;
    }
    if (const Working* g = w.get<Working>(e); g && !w.has<SoloMove>(e)) {
        if (const Transform* pt = w.get<Transform>(g->prop)) {
            const float gx = pt->x - tr->x, gz = pt->z - tr->z;
            if (gx * gx + gz * gz > PROP_REACH * PROP_REACH) {
                tx = pt->x;
                tz = pt->z;
                chasing = true;
            } else if (!underAttack(e)) {
                s.calmLead[i] = g->prop;
            }
        }
    } else if (const TalkOrder* talk = w.get<TalkOrder>(e)) {
        if (const Transform* nt = w.get<Transform>(talk->npc)) {
            tx = nt->x;
            tz = nt->z;
            chasing = true;
        }
    } else if (ActiveSquadMember* sm = w.get<ActiveSquadMember>(e)) {
        if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad)) {
            int cols = std::min(SQUAD_COLUMNS, sq->size);
            int rows = (sq->size + cols - 1) / cols;
            float colOff = sm->slot % cols - (cols - 1) * 0.5f;
            float rowOff = sm->slot / cols - (rows - 1) * 0.5f;
            float spacing = slotSpacing(sq->size);

            float rightX = -sq->headZ, rightZ = sq->headX;
            float backX = -sq->headX, backZ = -sq->headZ;
            tx = sq->cx + (rightX * colOff + backX * rowOff) * spacing;
            tz = sq->cz + (rightZ * colOff + backZ * rowOff) * spacing;

            Vec2 st = t.snap({ tx, tz });
            tx = st.x;
            tz = st.z;
            chasing = std::hypot(tx - tr->x, tz - tr->z) > SLOT_ROUTE_DISTANCE;
        }
    } else if (SoloMove* so = w.get<SoloMove>(e)) {
        while (so->next < so->waypoints.size()) {
            Vec2 wp = so->waypoints[so->next];
            float wx = wp.x - tr->x, wz = wp.z - tr->z;
            bool last = so->next + 1 == so->waypoints.size();
            float reach = last ? 0.05f : 0.2f;
            if (wx * wx + wz * wz > reach * reach) break;
            ++so->next;
        }
        if (so->next < so->waypoints.size()) {
            tx = so->waypoints[so->next].x;
            tz = so->waypoints[so->next].z;
        }
    } else if (const AttackOrder* ao = w.get<AttackOrder>(e)) {
        if (const Transform* tt = w.get<Transform>(ao->target)) {
            const float gx = tt->x - tr->x, gz = tt->z - tr->z;
            if (std::sqrt(gx * gx + gz * gz) > reachOf(w, e) * CHASE_STOP_SHARE || !canSee(w, t, e, ao->target)) {
                tx = tt->x;
                tz = tt->z;
                chasing = true;
            }
        }
    } else if (w.has<FollowOrder>(e) || w.has<DefendOrder>(e)) {
        const Entity lead = leadOf(w, e);
        if (const Transform* lt = w.get<Transform>(lead)) {
            leadGap = std::hypot(lt->x - tr->x, lt->z - tr->z);
            const IdleSpot* rest = w.get<IdleSpot>(e);
            const bool calm = !underAttack(e) && !underAttack(lead);
            const bool stay = calm && rest && rest->leadGap >= 0.0f && leadGap <= rest->leadGap + unitWidth;
            if (stay) {
                s.calmLead[i] = lead;
            } else if (!closeEnoughToLead(w, units, e, lead)) {
                tx = lt->x;
                tz = lt->z;
                chasing = true;
            } else if (calm) {
                s.calmLead[i] = lead;
            }
        } else {
            w.remove<FollowOrder>(e);
        }
    } else if (!w.has<Commandable>(e)) {
        if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) {
            if (const Transform* tt = w.get<Transform>(c->target)) {
                const float gx = tt->x - tr->x, gz = tt->z - tr->z;
                if (std::sqrt(gx * gx + gz * gz) > reachOf(w, e) * CHASE_STOP_SHARE || !canSee(w, t, e, c->target)) {
                    tx = tt->x;
                    tz = tt->z;
                    chasing = true;
                }
            }
        }
    }

    if (chasing) {
        const Vec2 step = chaseStep(w, t, s, e, tr, Vec2{ tx, tz });
        tx = step.x;
        tz = step.z;
    } else {
        w.remove<ChaseRoute>(e);
    }

    const bool idle = tx == tr->x && tz == tr->z;
    if (!idle && w.has<PlayingDead>(e)) {
        w.remove<PlayingDead>(e);
        queueLog(langf("MISC_GETS_UP", { nameOf(w, e) }), LogKind::Other, npcOnly(w, e));
    }
    if (idle) {
        IdleSpot* spot = w.get<IdleSpot>(e);
        if (!spot) spot = &w.add(e, IdleSpot{ tr->x, tr->z, leadGap });
        else if (spot->leadGap < 0.0f) spot->leadGap = leadGap;
        tx = spot->x;
        tz = spot->z;
    } else if (w.has<IdleSpot>(e)) {
        w.remove<IdleSpot>(e);
    }
    s.goal[i] = { tx, tz };
    s.going[i] = idle ? 0 : 1;
    s.speeds[i] = walkSpeedOf(w, e, running);
}

void unitsNearby(const SteerFrame& s, const Transform* tr, size_t i, std::vector<std::uint32_t>& nearby) {
    nearby.clear();
    const int cx = (int)std::floor(tr->x / UNIT_BOX), cz = (int)std::floor(tr->z / UNIT_BOX);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const std::int64_t key = cellKey(cx + dx, cz + dz);
            auto it = std::lower_bound(s.byCell.begin(), s.byCell.end(), std::make_pair(key, (std::uint32_t)0));
            for (; it != s.byCell.end() && it->first == key; ++it) {
                if (it->second != i) nearby.push_back(it->second);
            }
        }
    }
    std::sort(nearby.begin(), nearby.end());
}

void steerVelocity(World& w, SteerFrame& s, size_t i, std::vector<std::uint32_t>& nearby) {
    const Entity e = s.units[i];
    const Transform* tr = w.get<Transform>(e);
    unitsNearby(s, tr, i, nearby);

    float vx = 0.0f, vz = 0.0f, sideX = 0.0f, sideZ = 0.0f;
    bool shoved = false;
    for (const std::uint32_t j : nearby) {
        const std::pair<float, float>& o = s.pos[j];
        float sx = tr->x - o.first, sz = tr->z - o.second;
        const float overX = UNIT_BOX - std::fabs(sx), overZ = UNIT_BOX - std::fabs(sz);
        if (overX <= 0.0f || overZ <= 0.0f) continue;
        if (s.calmLead[i] != NULL_ENTITY && s.calmLead[i] == s.calmLead[j]) continue;
        if (std::fabs(sx) < 1e-4f && std::fabs(sz) < 1e-4f) sx = (e & 1u) ? 1e-3f : -1e-3f;
        const bool alongX = overX < overZ;
        const float push = (alongX ? overX : overZ) * 8.0f;
        if (alongX) vx += (sx >= 0.0f ? 1.0f : -1.0f) * push;
        else vz += (sz >= 0.0f ? 1.0f : -1.0f) * push;
        if (s.going[i] && s.pinned[j]) {
            float hx = s.goal[i].first - tr->x, hz = s.goal[i].second - tr->z;
            const float hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-4f) {
                hx /= hl;
                hz /= hl;
                float side = -hz * sx + hx * sz;
                if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;
                const float sgn = side > 0.0f ? 1.0f : -1.0f;
                vx += -hz * sgn * push * 1.5f;
                vz += hx * sgn * push * 1.5f;
            }
        }
        if (!s.going[i] && s.going[j]) {
            shoved = true;
            float hx = s.goal[j].first - o.first, hz = s.goal[j].second - o.second;
            const float hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-4f) {
                hx /= hl;
                hz /= hl;
                float side = hx * sz - hz * sx;
                if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;
                const float sign = side > 0.0f ? 1.0f : -1.0f;
                sideX += -hz * sign * push;
                sideZ += hx * sign * push;
            }
        }
    }
    if (shoved) {
        vx += sideX;
        vz += sideZ;
    } else {
        const float dx = s.goal[i].first - tr->x, dz = s.goal[i].second - tr->z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        const float deadZone = s.calmLead[i] != NULL_ENTITY ? unitWidth : 0.05f;
        if (dist > deadZone && s.speeds[i] > 0.0f) {
            const float sp = std::min(s.speeds[i], dist * 2.5f);
            vx += dx / dist * sp;
            vz += dz / dist * sp;
        }
    }
    s.vel[i] = { vx, vz };
}

void applySteering(World& w, Terrain& t, Entity e, Vec2f velocity, float dt, bool running) {
    Transform* tr = w.get<Transform>(e);
    const bool player = w.has<Commandable>(e);
    const float x0 = tr->x, z0 = tr->z;

    float nx = tr->x + velocity.first * dt;
    float nz = tr->z + velocity.second * dt;
    if (t.isFree(nx, tr->z)) tr->x = nx;
    if (t.isFree(tr->x, nz)) tr->z = nz;
    tr->y = t.heightAt(tr->x, tr->z);

    const float moved = std::hypot(tr->x - x0, tr->z - z0);
    if (moved > 0.0f) {
        if (player && running && moved >= 0.01f) {
            if (Needs* nd = w.get<Needs>(e)) nd->hurried = true;
        }
    }
}
}

void steeringSystem(World& w, Terrain& t, float dt, bool running) {
    SteerFrame s;
    s.dt = dt;
    s.units = w.view<Unit, Transform>();
    std::vector<Entity>& units = s.units;
    units.erase(std::remove_if(units.begin(), units.end(), [&](Entity e) { return outCold(w, e) || w.has<Carried>(e); }),
                units.end());
    s.vel.assign(units.size(), { 0.0f, 0.0f });
    s.speeds.assign(units.size(), 0.0f);
    s.goal.assign(units.size(), { 0.0f, 0.0f });
    s.going.assign(units.size(), 0);
    s.pinned.assign(units.size(), 0);
    s.calmLead.assign(units.size(), NULL_ENTITY);

    for (Entity e : units) {
        if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) s.attacked.push_back(c->target);
    }
    std::sort(s.attacked.begin(), s.attacked.end());

    for (size_t i = 0; i < units.size(); ++i) steerGoal(w, t, s, i, running);

    s.pos.resize(units.size());
    s.byCell.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const Transform* tr = w.get<Transform>(units[i]);
        s.pos[i] = { tr->x, tr->z };
        s.byCell[i] = { cellKey((int)std::floor(tr->x / UNIT_BOX), (int)std::floor(tr->z / UNIT_BOX)), (std::uint32_t)i };
    }
    std::sort(s.byCell.begin(), s.byCell.end());

    std::vector<std::uint32_t> nearby;
    for (size_t i = 0; i < units.size(); ++i) {
        if (!s.pinned[i]) steerVelocity(w, s, i, nearby);
    }
    for (size_t i = 0; i < units.size(); ++i) applySteering(w, t, units[i], s.vel[i], dt, running);
}

void cleanupActiveSquads(World& w) {
    std::vector<Entity> used;
    for (Entity e : w.view<ActiveSquadMember>()) used.push_back(w.get<ActiveSquadMember>(e)->squad);

    for (Entity sq : w.view<ActiveSquad>()) {
        if (std::find(used.begin(), used.end(), sq) == used.end()) w.destroy(sq);
    }
}

