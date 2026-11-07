#include "systems_internal.h"

using namespace systems_detail;

namespace {
constexpr float SQUAD_TURN_RATE = 2.5f;        // radians per second
constexpr float SLOT_REACHED = 0.1f;
constexpr float SLOT_PROGRESS = 0.02f;         // how much closer counts as getting closer
constexpr float SLOT_SWAP_AFTER = 0.3f;        // seconds without getting closer before it tries swapping slots or moving one
constexpr float SLOT_BESIDE_AFTER = 0.7f;      // before it settles against a squadmate who has settled
constexpr float SLOT_GIVE_UP_AFTER = 1.0f;     // before it settles wherever it is
constexpr float SLOT_RETRY_SECONDS = 4.0f;
constexpr int SLOT_MOST_RETRIES = 3;
constexpr float SLOT_SWAP_SECONDS = 1.0f;
constexpr float SLOT_CLOSE_ENOUGH = 0.5f;      // settled this near its slot, it doesn't try again
}

void squadSystem(World& w, float dt, bool running) {
    for (ObjectId e : w.view<ActiveSquad>()) {
        ActiveSquad* sq = w.get<ActiveSquad>(e);
        if (sq->arrived) continue;

        float slowest = -1.0f;
        for (ObjectId m : w.view<ActiveSquadMember, Entity>()) {
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
            // The squad turns gradually, so its far end doesn't swing round at every corner
            constexpr float FULL_TURN = 6.2831853f;
            const float now = std::atan2(sq->headZ, sq->headX);
            const float most = SQUAD_TURN_RATE * dt;
            const float turn = std::clamp(std::remainder(std::atan2(dz, dx) - now, FULL_TURN), -most, most);
            sq->headX = std::cos(now + turn);
            sq->headZ = std::sin(now + turn);
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
ObjectId leadOf(World& w, ObjectId e) {
    if (const FollowOrder* f = w.get<FollowOrder>(e)) return f->target;
    if (const DefendOrder* d = w.get<DefendOrder>(e); d && !w.has<AttackOrder>(e)) return d->ward;
    return NO_OBJECT;
}

bool closeEnoughToLead(World& w, const std::vector<ObjectId>& units, ObjectId e, ObjectId lead) {
    const Transform* tr = w.get<Transform>(e);
    const Transform* lt = w.get<Transform>(lead);
    if (!tr || !lt) return true;
    const float gx = lt->x - tr->x, gz = lt->z - tr->z;
    const float myGap = std::sqrt(gx * gx + gz * gz);
    if (myGap <= FOLLOW_DISTANCE) return true;
    const Entity* mu = w.get<Entity>(e);
    int crowd = 0;
    for (ObjectId o : units) crowd += leadOf(w, o) == lead ? 1 : 0;
    const float spacing = 2.0f * mu->radius + UNIT_CLEARANCE;
    if (myGap > FOLLOW_DISTANCE + 0.75f * spacing * std::sqrt((float)crowd)) return false;
    for (ObjectId o : units) {
        if (o == e || o == lead || leadOf(w, o) != lead) continue;
        const Transform* ot = w.get<Transform>(o);
        const Entity* ou = w.get<Entity>(o);
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
constexpr float CONTACT_MARGIN = 0.02f;
constexpr float SEPARATE_RATE = 6.0f;          // tiles per second, for each tile of overlap
constexpr float MOST_SEPARATE_SPEED = 2.0f;
constexpr float STEP_ASIDE_SPEED = 1.5f;

using Vec2f = std::pair<float, float>;

std::int64_t cellKey(int cx, int cz) { return (std::int64_t)(((std::uint64_t)(std::uint32_t)cx << 32) | (std::uint32_t)cz); }

struct SteerFrame {
    std::vector<ObjectId> units;
    std::vector<Vec2f> vel;
    std::vector<float> speeds;
    std::vector<Vec2f> goal;
    std::vector<char> going, pinned;
    std::vector<ObjectId> calmLead;
    std::vector<ObjectId> attacked;
    float dt = 0.0f;
    int plansLeft = CHASE_PLANS_PER_STEP;
    std::vector<Vec2f> pos;
    std::vector<float> radius;
    std::vector<Vec2f> contacts;
    std::vector<std::uint32_t> contactWho;
    std::vector<std::pair<std::int64_t, std::uint32_t>> byCell;
    bool underAttack(ObjectId e) const { return std::binary_search(attacked.begin(), attacked.end(), e); }
};

Vec2 chaseStep(World& w, const Terrain& t, SteerFrame& s, ObjectId e, const Transform* tr, Vec2 goal) {
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

void giveUpOnSlot(ActiveSquadMember& sm) {
    sm.settled = true;
    sm.retryIn = SLOT_RETRY_SECONDS;
    ++sm.retries;
}

// Where a fighter walks: a ranged one, or a melee one still far away, heads for its target until in reach. A melee one
// heads for its slot and stops once in reach; one with no slot waits outside.
void attackGoal(World& w, const Terrain& t, ObjectId e, const Transform* tr, ObjectId foe, float& tx, float& tz, bool& chasing) {
    const Transform* tt = w.get<Transform>(foe);
    if (!tt) return;
    const float gx = tt->x - tr->x, gz = tt->z - tr->z;
    const float gap = std::sqrt(gx * gx + gz * gz);
    const float reach = reachOf(w, e);
    const bool seen = canSee(w, t, e, foe);
    AttackSlot* place = reach <= ATTACK_REACH ? w.get<AttackSlot>(e) : nullptr;
    if (!place || place->target != foe) {
        if (gap > reach * CHASE_STOP_SHARE || !seen) {
            tx = tt->x;
            tz = tt->z;
            chasing = true;
        }
        return;
    }
    Vec2 spot{ tr->x, tr->z };
    if (place->slot >= 0) {
        if (place->closing && gap <= reach * CHASE_STOP_SHARE && seen) place->closing = false;
        else if (!place->closing && (gap > reach * CHASE_RESUME_SHARE || !seen)) place->closing = true;
        if (place->closing) spot = attackSlotSpot(*tt, place->slot);
    } else {
        if (gap > ATTACK_WAIT_FAR || gap < ATTACK_WAIT_NEAR) place->closing = true;
        else if (std::fabs(gap - ATTACK_WAIT_RADIUS) < 0.3f) place->closing = false;
        if (place->closing && gap > 1e-4f) {
            spot = Vec2{ tt->x - gx / gap * ATTACK_WAIT_RADIUS, tt->z - gz / gap * ATTACK_WAIT_RADIUS };
        }
    }
    if (!place->closing) return;
    const Vec2 st = t.snap(spot);
    tx = st.x;
    tz = st.z;
    chasing = true;
}

void steerGoal(World& w, Terrain& t, SteerFrame& s, size_t i, bool running) {
    const std::vector<ObjectId>& units = s.units;
    auto underAttack = [&](ObjectId e) { return s.underAttack(e); };
    ObjectId e = units[i];
    Transform* tr = w.get<Transform>(e);

    float tx = tr->x, tz = tr->z;
    float leadGap = -1.0f;
    bool chasing = false;
    const Combat* fighting = w.get<Combat>(e);
    if (w.has<Occupying>(e) || (w.has<InConversation>(e) && !underAttack(e) && !(fighting && fighting->target != NO_OBJECT))) {
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
            if (sq->arrived) {
                const Vec2 spot = sq->finalSpots[(size_t)sm->slot];
                const float gap = std::hypot(spot.x - tr->x, spot.z - tr->z);
                if (sm->settled) {
                    sm->retryIn -= s.dt;
                    if (gap > SLOT_CLOSE_ENOUGH && sm->retries < SLOT_MOST_RETRIES && sm->retryIn <= 0.0f) {
                        sm->settled = false;
                        sm->stuck = 0.0f;
                        sm->bestGap = gap;
                    }
                } else if (gap <= SLOT_REACHED) {
                    sm->settled = true;
                } else if (gap < sm->bestGap - SLOT_PROGRESS) {
                    sm->bestGap = gap;
                    sm->stuck = 0.0f;
                } else if ((sm->stuck += s.dt) >= SLOT_GIVE_UP_AFTER) {
                    giveUpOnSlot(*sm);
                }
                if (!sm->settled) {
                    tx = spot.x;
                    tz = spot.z;
                    chasing = gap > SLOT_ROUTE_DISTANCE;
                }
            } else {
                const Vec2 st = t.snap(formationSpot(Vec2{ sq->cx, sq->cz }, sq->headX, sq->headZ, sq->offsets[(size_t)sm->slot]));
                tx = st.x;
                tz = st.z;
                chasing = std::hypot(tx - tr->x, tz - tr->z) > SLOT_ROUTE_DISTANCE;
            }
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
        attackGoal(w, t, e, tr, ao->target, tx, tz, chasing);
    } else if (w.has<FollowOrder>(e) || w.has<DefendOrder>(e)) {
        const ObjectId lead = leadOf(w, e);
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
        if (const Combat* c = w.get<Combat>(e); c && c->target != NO_OBJECT) attackGoal(w, t, e, tr, c->target, tx, tz, chasing);
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
        queueLog(w, langf("MISC_GETS_UP", { nameOf(w, e) }), LogKind::Other, npcOnly(w, e));
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
    const int cx = (int)std::floor(tr->x / CROWD_CELL), cz = (int)std::floor(tr->z / CROWD_CELL);
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

// Moves a slot that someone outside the squad is standing on to the nearest place that is free
void moveSlotOffBlocker(const Terrain& t, const SteerFrame& s, ActiveSquad& sq, int slot) {
    constexpr float FULL_TURN = 6.2831853f;
    const Vec2 was = sq.finalSpots[(size_t)slot];
    const float apart = sq.spacing * 0.95f;
    for (int ring = 1; ring <= 6; ++ring) {
        const int around = 8 * ring;
        for (int k = 0; k < around; ++k) {
            const float a = FULL_TURN * (float)k / (float)around;
            const Vec2 at{ was.x + std::cos(a) * sq.spacing * (float)ring, was.z + std::sin(a) * sq.spacing * (float)ring };
            if (!t.isFree(at.x, at.z)) continue;
            bool clear = true;
            for (size_t o = 0; o < sq.finalSpots.size() && clear; ++o) {
                clear = (int)o == slot || std::hypot(sq.finalSpots[o].x - at.x, sq.finalSpots[o].z - at.z) >= apart;
            }
            for (size_t o = 0; o < s.pos.size() && clear; ++o) {
                clear = std::hypot(s.pos[o].first - at.x, s.pos[o].second - at.z) >= s.radius[o] + 0.5f;
            }
            if (!clear) continue;
            sq.finalSpots[(size_t)slot] = at;
            return;
        }
    }
}

// A squad member that can't get to its slot, with those it is touching known: it trades slots with a squadmate if that shortens
// their walks, moves a slot that an outsider is on, or settles beside a squadmate who has settled.
void workOutSlot(World& w, const Terrain& t, SteerFrame& s, size_t i, int outsiderOnGoal) {
    const ObjectId e = s.units[i];
    ActiveSquadMember* sm = w.get<ActiveSquadMember>(e);
    ActiveSquad* sq = sm ? w.get<ActiveSquad>(sm->squad) : nullptr;
    if (!sq || !sq->arrived || sm->settled) return;
    sm->swapIn -= s.dt;
    if (sm->stuck < SLOT_SWAP_AFTER) return;
    const Transform* tr = w.get<Transform>(e);
    const float hx = s.goal[i].first - tr->x, hz = s.goal[i].second - tr->z;
    if (outsiderOnGoal >= 0 && sm->swapIn <= 0.0f) {
        moveSlotOffBlocker(t, s, *sq, sm->slot);
        sm->swapIn = SLOT_SWAP_SECONDS;
        sm->stuck = 0.0f;
        sm->bestGap = 1e9f;
        return;
    }
    const Vec2 mine = sq->finalSpots[(size_t)sm->slot];
    const float myGap = std::hypot(mine.x - tr->x, mine.z - tr->z);
    for (size_t c = 0; c < s.contacts.size(); ++c) {
        if (hx * s.contacts[c].first + hz * s.contacts[c].second >= 0.0f) continue;   // not in the way
        const std::uint32_t j = s.contactWho[c];
        ActiveSquadMember* other = w.get<ActiveSquadMember>(s.units[j]);
        if (!other || other->squad != sm->squad) continue;
        const Vec2 theirs = sq->finalSpots[(size_t)other->slot];
        const Vec2f& at = s.pos[j];
        const float now = std::hypot(mine.x - tr->x, mine.z - tr->z) + std::hypot(theirs.x - at.first, theirs.z - at.second);
        const float then = std::hypot(theirs.x - tr->x, theirs.z - tr->z) + std::hypot(mine.x - at.first, mine.z - at.second);
        if (sm->swapIn <= 0.0f && then + 0.05f < now) {
            std::swap(sm->slot, other->slot);
            for (ActiveSquadMember* m : { sm, other }) {
                m->settled = false;
                m->stuck = 0.0f;
                m->bestGap = 1e9f;
                m->retries = 0;
                m->swapIn = SLOT_SWAP_SECONDS;
            }
            return;
        }
        if (other->settled && sm->stuck >= SLOT_BESIDE_AFTER && myGap <= 2.5f * sq->spacing) {
            giveUpOnSlot(*sm);
            return;
        }
    }
}

// Units are circles. A unit doesn't walk into one it touches, it slides around it, so walking never presses units into
// each other. Those that do overlap ease apart, no faster than a set speed.
void steerVelocity(World& w, const Terrain& t, SteerFrame& s, size_t i, std::vector<std::uint32_t>& nearby) {
    const ObjectId e = s.units[i];
    const Transform* tr = w.get<Transform>(e);
    unitsNearby(s, tr, i, nearby);

    const Combat* combat = w.get<Combat>(e);
    const bool fighting = combat && combat->target != NO_OBJECT;
    float pushX = 0.0f, pushZ = 0.0f, sideX = 0.0f, sideZ = 0.0f;
    bool shoved = false, goalInside = false;
    s.contacts.clear();
    s.contactWho.clear();
    const ActiveSquadMember* myPlace = w.get<ActiveSquadMember>(e);
    int outsiderOnGoal = -1;
    for (const std::uint32_t j : nearby) {
        if (s.calmLead[i] != NO_OBJECT && s.calmLead[i] == s.calmLead[j]) continue;
        const std::pair<float, float>& o = s.pos[j];
        float nx = tr->x - o.first, nz = tr->z - o.second;
        const float touching = s.radius[i] + s.radius[j];
        const float d2 = nx * nx + nz * nz;
        if (d2 >= (touching + CONTACT_MARGIN) * (touching + CONTACT_MARGIN)) continue;
        float d = std::sqrt(d2);
        if (d < 1e-4f) {
            // Exactly on top of each other: part them by a direction of their own
            const float a = (float)e * 2.3999632f;
            nx = std::cos(a);
            nz = std::sin(a);
            d = 0.0f;
        } else {
            nx /= d;
            nz /= d;
        }
        s.contacts.push_back({ nx, nz });
        s.contactWho.push_back(j);
        const float overlap = touching - d;
        if (overlap > 0.0f) {
            pushX += nx * overlap * SEPARATE_RATE;
            pushZ += nz * overlap * SEPARATE_RATE;
        }
        const float gox = s.goal[i].first - o.first, goz = s.goal[i].second - o.second;
        if (gox * gox + goz * goz <= s.radius[j] * s.radius[j]) goalInside = true;
        // Near enough that this unit can't stand on its slot with that one where it is
        if (myPlace && gox * gox + goz * goz < touching * touching) {
            const ActiveSquadMember* theirPlace = w.get<ActiveSquadMember>(s.units[j]);
            if (!theirPlace || theirPlace->squad != myPlace->squad) outsiderOnGoal = (int)j;
        }
        // Standing around, with someone walking at it: it steps aside, unless it is in a fight
        if (!s.going[i] && s.going[j] && !fighting) {
            float hx = s.goal[j].first - o.first, hz = s.goal[j].second - o.second;
            const float hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-4f && hx * nx + hz * nz > 0.0f) {
                hx /= hl;
                hz /= hl;
                float side = hx * nz - hz * nx;
                if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;
                const float sign = side > 0.0f ? 1.0f : -1.0f;
                sideX += -hz * sign;
                sideZ += hx * sign;
                shoved = true;
            }
        }
    }

    workOutSlot(w, t, s, i, outsiderOnGoal);

    float vx = 0.0f, vz = 0.0f, wanted = 0.0f, headX = 0.0f, headZ = 0.0f;
    if (shoved) {
        const float sl = std::hypot(sideX, sideZ);
        if (sl > 1e-4f) {
            vx = sideX / sl * STEP_ASIDE_SPEED;
            vz = sideZ / sl * STEP_ASIDE_SPEED;
        }
    } else {
        const float dx = s.goal[i].first - tr->x, dz = s.goal[i].second - tr->z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        const float deadZone = s.calmLead[i] != NO_OBJECT ? unitWidth : 0.05f;
        if (dist > deadZone && s.speeds[i] > 0.0f) {
            wanted = std::min(s.speeds[i], dist * 2.5f);
            headX = dx / dist;
            headZ = dz / dist;
            vx = headX * wanted;
            vz = headZ * wanted;
        }
    }
    const auto slide = [&]() {
        for (int pass = 0; pass < 2; ++pass) {
            for (const Vec2f& n : s.contacts) {
                const float into = vx * n.first + vz * n.second;
                if (into < 0.0f) {
                    vx -= n.first * into;
                    vz -= n.second * into;
                }
            }
        }
    };
    slide();
    // Stopped dead by those in the way: go round, always by the same hand, so two meeting head on pass each other
    if (wanted > 0.0f && !goalInside && s.going[i] && !s.contacts.empty() && std::hypot(vx, vz) < wanted * 0.25f) {
        vx = -headZ * wanted * 0.5f;
        vz = headX * wanted * 0.5f;
        slide();
    }
    const float pushed = std::hypot(pushX, pushZ);
    if (pushed > MOST_SEPARATE_SPEED) {
        pushX *= MOST_SEPARATE_SPEED / pushed;
        pushZ *= MOST_SEPARATE_SPEED / pushed;
    }
    s.vel[i] = { vx + pushX, vz + pushZ };
}

void applySteering(World& w, Terrain& t, ObjectId e, Vec2f velocity, float dt, bool running) {
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
    s.units = w.view<Entity, Transform>();
    std::vector<ObjectId>& units = s.units;
    units.erase(std::remove_if(units.begin(), units.end(), [&](ObjectId e) { return outCold(w, e) || w.has<Carried>(e); }),
                units.end());
    s.vel.assign(units.size(), { 0.0f, 0.0f });
    s.speeds.assign(units.size(), 0.0f);
    s.goal.assign(units.size(), { 0.0f, 0.0f });
    s.going.assign(units.size(), 0);
    s.pinned.assign(units.size(), 0);
    s.calmLead.assign(units.size(), NO_OBJECT);

    for (ObjectId e : units) {
        if (const Combat* c = w.get<Combat>(e); c && c->target != NO_OBJECT) s.attacked.push_back(c->target);
    }
    std::sort(s.attacked.begin(), s.attacked.end());

    for (size_t i = 0; i < units.size(); ++i) steerGoal(w, t, s, i, running);

    s.pos.resize(units.size());
    s.radius.resize(units.size());
    s.byCell.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const Transform* tr = w.get<Transform>(units[i]);
        s.pos[i] = { tr->x, tr->z };
        s.radius[i] = w.get<Entity>(units[i])->radius;
        s.byCell[i] = { cellKey((int)std::floor(tr->x / CROWD_CELL), (int)std::floor(tr->z / CROWD_CELL)), (std::uint32_t)i };
    }
    std::sort(s.byCell.begin(), s.byCell.end());

    std::vector<std::uint32_t> nearby;
    for (size_t i = 0; i < units.size(); ++i) {
        if (!s.pinned[i]) steerVelocity(w, t, s, i, nearby);
    }
    for (size_t i = 0; i < units.size(); ++i) applySteering(w, t, units[i], s.vel[i], dt, running);
}

void cleanupActiveSquads(World& w) {
    std::vector<ObjectId> used;
    for (ObjectId e : w.view<ActiveSquadMember>()) used.push_back(w.get<ActiveSquadMember>(e)->squad);

    for (ObjectId sq : w.view<ActiveSquad>()) {
        if (std::find(used.begin(), used.end(), sq) == used.end()) w.destroy(sq);
    }
}

