#include "stuck.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <vector>
#include "building.h"
#include "calendar.h"
#include "claims.h"
#include "systems_internal.h"

using namespace systems_detail;

namespace {
constexpr int REACH_LOOK_TILES = 6000;   // a search this far without a wall round it is in the open
constexpr int WINDOW_PAD = 24;
constexpr int WINDOW_MOST = 160;
constexpr float COST_OBJECT = 5.0f, COST_WALL = 20.0f, COST_LADDER = 40.0f, COST_STAIRS = 60.0f;
constexpr float BREAK_REACH = 1.9f;

int tileOf(float v) { return (int)std::floor(v); }

ObjectId pieceAt(const BuildingMap& m, int x, int z) {
    const auto it = m.piece.find(levelKey(x, z, 0));
    return it == m.piece.end() ? NO_OBJECT : it->second;
}

// What a tile costs to cross: 1 open, more where a wall, door, ladder or stairs of the building map shuts it, -1 for ground that
// nothing can break (water, cliffs)
float crossCost(World& w, const Terrain& t, const BuildingMap& m, int x, int z, ObjectId* blocker) {
    if (blocker) *blocker = NO_OBJECT;
    if (t.walkable(x, z)) return 1.0f;
    if (!t.blockedByBuilding(x, z)) return -1.0f;
    const ObjectId e = pieceAt(m, x, z);
    const Prop* p = e != NO_OBJECT ? w.get<Prop>(e) : nullptr;
    if (!p) return -1.0f;
    if (blocker) *blocker = e;
    const PropInfo& info = propInfo(p->kind);
    if (info.stairs) return COST_STAIRS;
    if (info.ladder) return COST_LADDER;
    return info.wallRule || info.door ? COST_WALL : COST_OBJECT;
}

// A tile window round both ends, kept to a size
struct Window {
    int x0, z0, x1, z1;
    bool has(int x, int z) const { return x >= x0 && z >= z0 && x <= x1 && z <= z1; }
    int w() const { return x1 - x0 + 1; }
    int h() const { return z1 - z0 + 1; }
    int at(int x, int z) const { return (z - z0) * w() + (x - x0); }
};

Window windowFor(const Terrain& t, int sx, int sz, int gx, int gz) {
    Window win{ std::min(sx, gx) - WINDOW_PAD, std::min(sz, gz) - WINDOW_PAD, std::max(sx, gx) + WINDOW_PAD, std::max(sz, gz) + WINDOW_PAD };
    if (win.w() > WINDOW_MOST) {
        win.x0 = sx - WINDOW_MOST / 2;
        win.x1 = sx + WINDOW_MOST / 2;
    }
    if (win.h() > WINDOW_MOST) {
        win.z0 = sz - WINDOW_MOST / 2;
        win.z1 = sz + WINDOW_MOST / 2;
    }
    win.x0 = std::max(0, win.x0);
    win.z0 = std::max(0, win.z0);
    win.x1 = std::min(t.size() - 1, win.x1);
    win.z1 = std::min(t.size() - 1, win.z1);
    return win;
}

void healthFor(World& w, ObjectId e) {
    if (w.has<Health>(e) || !w.has<Prop>(e)) return;
    const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
    const double tough = materialToughness(materialOf(w, e));
    double h = info.health > 0.0 ? info.health : 10.0;
    if (info.stairs) h = WALL_HEALTH * tough;
    else if (info.wallRule) h = WALL_HEALTH * tough;
    else if (info.door || info.ladder) h = DOOR_HEALTH * tough;
    w.add(e, Health{ h, h });
}
}

void setPathGoal(World& w, ObjectId u, Vec2 goal) {
    if (PathGoal* g = w.get<PathGoal>(u)) {
        g->goal = goal;
        g->tries = 0;
        g->hasMark = false;
        g->still = 0.0f;
        g->sidestep = 0.0f;
        return;
    }
    PathGoal g;
    g.goal = goal;
    w.add(u, g);
}

void notePathfind(World& w, ObjectId u) {
    PathPace* p = w.get<PathPace>(u);
    if (!p) p = &w.add(u, PathPace{});
    p->wait = REPATH_SECONDS;
    p->pending = false;
}

bool deferPath(World& w, ObjectId u, Vec2 target, int storey, bool whole) {
    PathPace* p = w.get<PathPace>(u);
    if (!p || p->wait <= 0.0f || !w.resource<SimStepping>().on || !w.has<Transform>(u)) return false;
    // A whole walk being kept stays one, when only its way is asked for again
    if (!(p->pending && p->whole && !whole)) {
        p->whole = whole;
        p->storey = storey;
    }
    p->pending = true;
    p->target = target;
    if (whole) w.remove<Route>(u);
    SoloMove* so = w.get<SoloMove>(u);
    if (!so) so = &w.add(u, SoloMove{});
    if (so->next < so->waypoints.size()) {
        so->waypoints.back() = target;   // goes on with the walk it has
    } else {
        so->waypoints.assign(1, target);
        so->next = 0;
        so->held = true;
    }
    return true;
}

void notePathFailed(World& w, ObjectId u, Vec2 target) {
    if (!w.has<Entity>(u)) return;
    PathPace* p = w.get<PathPace>(u);
    if (!p) p = &w.add(u, PathPace{});
    if (p->quiet > 0.0f && std::hypot(p->failed.x - target.x, p->failed.z - target.z) < 1.0f) return;
    p->failed = target;
    p->quiet = PATH_FAIL_QUIET_SECONDS;
    queueLog(w, langf("MISC_PATH_FAILED", { nameOf(w, u), std::to_string(tileOf(target.x)), std::to_string(tileOf(target.z)) }),
             LogKind::PathFail, npcOnly(w, u));
}

void notePathResult(World& w, const Terrain& t, ObjectId u, Vec2 from, Vec2 target, const std::vector<Vec2>& path) {
    const Vec2 goal = t.snap(target);
    const Vec2 end = path.empty() ? from : path.back();
    if (std::hypot(end.x - goal.x, end.z - goal.z) > PATH_FAIL_GAP) notePathFailed(w, u, target);
}

void noteTaskFailed(World& w, ObjectId u, const std::string& task) {
    if (!w.has<Entity>(u)) return;
    queueLog(w, langf("MISC_TASK_FAILED", { nameOf(w, u), task }), LogKind::TaskFail, npcOnly(w, u));
}

void pathPaceSystem(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<PathPace>()) {
        PathPace* p = w.get<PathPace>(u);
        if (!p) continue;
        p->wait -= dt;
        p->quiet -= dt;
        if (p->wait > 0.0f) continue;
        if (!p->pending) {
            if (p->quiet <= 0.0f) w.remove<PathPace>(u);
            continue;
        }
        const PathPace kept = *p;
        p->pending = false;
        // Only if it still means to go there: an order since, or the walk given up, drops the plan
        SoloMove* so = w.get<SoloMove>(u);
        if (!so || !w.has<Transform>(u) || so->waypoints.empty() || so->waypoints.back().x != kept.target.x ||
            so->waypoints.back().z != kept.target.z) {
            if (so) so->held = false;
            continue;
        }
        if (kept.whole) walkSoloTo(w, t, u, kept.target, kept.storey);
        else planSoloMove(w, t, u, kept.target);
    }
}

bool stuckReachable(World& w, const Terrain& t, Vec2 from, Vec2 goal) {
    (void)w;
    const Vec2 sg = t.snap(goal);
    const int sx = tileOf(from.x), sz = tileOf(from.z), gx = tileOf(sg.x), gz = tileOf(sg.z);
    if (sx == gx && sz == gz) return true;
    const Window win = windowFor(t, sx, sz, gx, gz);
    if (!win.has(sx, sz) || !win.has(gx, gz)) return true;
    std::vector<char> seen((size_t)win.w() * (size_t)win.h(), 0);
    std::vector<int> open;
    open.push_back(win.at(sx, sz));
    seen[(size_t)win.at(sx, sz)] = 1;
    int looked = 0;
    while (!open.empty()) {
        const int k = open.back();
        open.pop_back();
        const int x = win.x0 + k % win.w(), z = win.z0 + k / win.w();
        if (x == gx && z == gz) return true;
        if (++looked >= REACH_LOOK_TILES) return true;
        const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i], nz = z + dz[i];
            if (!win.has(nx, nz) || seen[(size_t)win.at(nx, nz)] || !t.walkable(nx, nz)) continue;
            seen[(size_t)win.at(nx, nz)] = 1;
            open.push_back(win.at(nx, nz));
        }
    }
    return false;
}

ObjectId stuckBlocker(World& w, const Terrain& t, Vec2 from, Vec2 goal) {
    const Vec2 sg = t.snap(goal);
    const int sx = tileOf(from.x), sz = tileOf(from.z), gx = tileOf(sg.x), gz = tileOf(sg.z);
    const Window win = windowFor(t, sx, sz, gx, gz);
    if (!win.has(sx, sz) || !win.has(gx, gz)) return NO_OBJECT;
    const BuildingMap m = buildingMap(w);
    const size_t n = (size_t)win.w() * (size_t)win.h();
    std::vector<float> best(n, 1e30f);
    std::vector<int> from_(n, -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    best[(size_t)win.at(sx, sz)] = 0.0f;
    open.push({ 0.0f, win.at(sx, sz) });
    const int goalK = win.at(gx, gz);
    while (!open.empty()) {
        const auto [cost, k] = open.top();
        open.pop();
        if (cost > best[(size_t)k]) continue;
        if (k == goalK) break;
        const int x = win.x0 + k % win.w(), z = win.z0 + k / win.w();
        const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i], nz = z + dz[i];
            if (!win.has(nx, nz)) continue;
            const float c = crossCost(w, t, m, nx, nz, nullptr);
            if (c < 0.0f) continue;
            const int nk = win.at(nx, nz);
            if (cost + c < best[(size_t)nk]) {
                best[(size_t)nk] = cost + c;
                from_[(size_t)nk] = k;
                open.push({ cost + c, nk });
            }
        }
    }
    if (from_[(size_t)goalK] < 0) return NO_OBJECT;
    // From the goal back to here, the shut tile nearest here along the way
    ObjectId first = NO_OBJECT;
    for (int k = goalK; k >= 0 && k != win.at(sx, sz); k = from_[(size_t)k]) {
        ObjectId blocker = NO_OBJECT;
        crossCost(w, t, m, win.x0 + k % win.w(), win.z0 + k / win.w(), &blocker);
        if (blocker != NO_OBJECT) first = blocker;
    }
    return first;
}

namespace {
void setWalk(World& w, ObjectId u, Vec2 to) {
    w.remove<Route>(u);
    SoloMove& so = w.add(u, SoloMove{});
    so.waypoints.assign(1, to);
    so.next = 0;
}

// Walks up to what it is breaking, and swings at it. True once it is gone.
bool breakStep(World& w, Terrain& t, ObjectId u, Stuck& st, float d, bool moving) {
    const ObjectId target = st.breaking;
    const Transform* bt = w.has<Prop>(target) ? w.get<Transform>(target) : nullptr;
    const Transform* ut = w.get<Transform>(u);
    if (!bt) return true;
    if (std::hypot(bt->x - ut->x, bt->z - ut->z) > BREAK_REACH) {
        if (moving) return false;
        // To the open tile beside it that is nearest
        const int bx = tileOf(bt->x), bz = tileOf(bt->z);
        float bestD = 1e30f;
        Vec2 spot{ ut->x, ut->z };
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0) == (dz == 0) || !t.walkable(bx + dx, bz + dz)) continue;
                const float dd = std::hypot((float)(bx + dx) + 0.5f - ut->x, (float)(bz + dz) + 0.5f - ut->z);
                if (dd < bestD) {
                    bestD = dd;
                    spot = Vec2{ (float)(bx + dx) + 0.5f, (float)(bz + dz) + 0.5f };
                }
            }
        }
        st.idle += d;
        if (st.idle > 20.0f) return true;   // it cannot get to it: it gives it up
        setWalk(w, u, spot);
        return false;
    }
    w.remove<SoloMove>(u);
    st.swing -= d;
    if (st.swing > 0.0f) return false;
    st.swing = STUCK_SWING_SECONDS;
    healthFor(w, target);
    Health* h = w.get<Health>(target);
    if (!h) return true;
    double hit = 2.0;
    if (w.has<Combat>(u)) hit = std::max(1.0, (double)numbersOf(w, u).attack);
    h->value -= hit;
    queueSoundFrom(w, Sfx::Hit, target);
    if (h->value > 0.0) return false;
    const PropInfo& info = propInfo(w.get<Prop>(target)->kind);
    queueLog(w, langf("MISC_DOOR_SMASHED", { nameOf(w, u), lang(info.nameKey) }), LogKind::Seen);
    destroyBuilding(w, target);
    return true;
}
}

void stuckSystem(World& w, Terrain& t, Rng& rng, float dt) {
    for (ObjectId u : w.view<PathGoal, Transform>()) {
        PathGoal* g = w.get<PathGoal>(u);
        if (!g) continue;
        g->elapsed += dt;
        if (g->elapsed < STUCK_LOOK_SECONDS) continue;
        const float d = g->elapsed;
        g->elapsed = 0.0f;
        if (outCold(w, u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || w.has<Occupying>(u) || storeyOf(w, u) != 0) {
            w.remove<Stuck>(u);
            w.remove<PathGoal>(u);
            continue;
        }
        const Transform* ut = w.get<Transform>(u);
        const Vec2 here{ ut->x, ut->z };
        if (std::hypot(g->goal.x - here.x, g->goal.z - here.z) <= 1.5f) {
            w.remove<Stuck>(u);
            w.remove<PathGoal>(u);
            continue;
        }
        const bool moving = walkingNow(w, u);
        Stuck* st = w.get<Stuck>(u);
        if (st) {
            st->hours += (double)d * GAME_SECONDS_PER_REAL_SECOND / 3600.0;
            if (st->hours >= STUCK_FAINT_HOURS) {
                // A day shut in: it loses consciousness, and the day starts again
                knockOut(w, u);
                w.remove<Stuck>(u);
                w.remove<PathGoal>(u);
                continue;
            }
            if (st->breaking != NO_OBJECT) {
                if (breakStep(w, t, u, *st, d, moving)) {
                    st->breaking = NO_OBJECT;
                    st->idle = 0.0f;
                    st->swing = 0.0f;
                    planSoloMove(w, t, u, g->goal);   // the way may be open now
                }
                continue;
            }
            if (st->wander > 0.0f) {
                st->wander -= d;
                if (st->wander <= 0.0f) {
                    st->wander = 0.0f;
                    planSoloMove(w, t, u, g->goal);   // and tries again
                }
                continue;
            }
        }
        if (moving) {
            if (st) st->idle = 0.0f;
            if (g->sidestep > 0.0f) {
                g->sidestep -= d;
                if (g->sidestep <= 0.0f) {
                    g->sidestep = 0.0f;
                    planSoloMove(w, t, u, g->goal);   // back to the way
                    g->hasMark = false;
                }
                continue;
            }
            const SoloMove* so = w.get<SoloMove>(u);
            if (!so || w.has<Route>(u) || w.has<Climbing>(u) || so->next >= so->waypoints.size()) {
                g->hasMark = false;
                continue;
            }
            if (!g->hasMark || std::hypot(here.x - g->mark.x, here.z - g->mark.z) >= STUCK_STILL_DISTANCE) {
                g->mark = here;
                g->hasMark = true;
                g->still = 0.0f;
                g->tries = 0;
                continue;
            }
            g->still += d;
            if (g->still < STUCK_STILL_SECONDS) continue;
            g->still = 0.0f;
            ++g->tries;
            if (g->tries == 1) {
                // Plans again with the tile it is pushing at shut, for that plan only
                const Vec2 next = so->waypoints[so->next];
                const float len = std::hypot(next.x - here.x, next.z - here.z);
                const int ax = tileOf(here.x + (len > 0.01f ? (next.x - here.x) / len : 0.0f) * 0.9f);
                const int az = tileOf(here.z + (len > 0.01f ? (next.z - here.z) / len : 0.0f) * 0.9f);
                const bool shut = (ax != tileOf(here.x) || az != tileOf(here.z)) && t.walkable(ax, az);
                if (shut) t.setBlocked(ax, az, true);
                planSoloMove(w, t, u, g->goal);
                if (shut) t.setBlocked(ax, az, false);
                if (!walkingNow(w, u)) planSoloMove(w, t, u, g->goal);   // no way round: the old way, then
            } else if (g->tries == 2) {
                // Steps aside at random for a moment
                for (int attempt = 0; attempt < 8; ++attempt) {
                    const float a = rng.unit() * 6.2831853f, r = 1.0f + rng.unit() * 1.5f;
                    const Vec2 to{ here.x + std::cos(a) * r, here.z + std::sin(a) * r };
                    if (!t.isFree(to.x, to.z) || !t.lineClear(here, to)) continue;
                    setWalk(w, u, to);
                    g->sidestep = STUCK_SIDESTEP_SECONDS;
                    break;
                }
                if (g->sidestep <= 0.0f) planSoloMove(w, t, u, g->goal);
            } else {
                // Gives the walk up; if it is shut in, the rest is as for a unit that has stopped
                w.remove<SoloMove>(u);
                g->tries = 0;
                g->hasMark = false;
            }
            continue;
        }
        g->hasMark = false;
        // Idle short of its goal: shut in, or only done with the walk?
        if (stuckReachable(w, t, here, g->goal)) {
            w.remove<Stuck>(u);
            // Open ground to it, yet it stopped short: its way is planned again a time or two before it is let be
            if (g->tries < 2) {
                ++g->tries;
                planSoloMove(w, t, u, g->goal);
            } else {
                w.remove<PathGoal>(u);
            }
            continue;
        }
        if (!st) st = &w.add(u, Stuck{});
        st->idle += d;
        if (st->idle < STUCK_IDLE_SECONDS) continue;
        st->idle = 0.0f;
        ++st->rounds;
        if (st->rounds >= STUCK_ROUNDS_BEFORE_BREAKING) {
            const ObjectId blocker = stuckBlocker(w, t, here, g->goal);
            if (blocker != NO_OBJECT) {
                st->breaking = blocker;
                st->swing = 0.0f;
                continue;
            }
        }
        // Moves about at random for a few seconds
        for (int attempt = 0; attempt < 8; ++attempt) {
            const float a = rng.unit() * 6.2831853f, r = 1.0f + rng.unit() * 3.0f;
            const Vec2 to{ here.x + std::cos(a) * r, here.z + std::sin(a) * r };
            if (!t.isFree(to.x, to.z) || !t.lineClear(here, to)) continue;
            setWalk(w, u, to);
            break;
        }
        st->wander = STUCK_WANDER_MIN_SECONDS + rng.unit() * (STUCK_WANDER_MAX_SECONDS - STUCK_WANDER_MIN_SECONDS);
    }
}
