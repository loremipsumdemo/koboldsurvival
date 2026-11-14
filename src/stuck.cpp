#include "patrons.h"
#include "stuck.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "building.h"
#include "calendar.h"
#include "claims.h"
#include "factions.h"
#include "systems_internal.h"

using namespace systems_detail;

namespace {
constexpr int REACH_LOOK_TILES = 6000;   // a search this far without a wall round it is in the open
constexpr int WINDOW_PAD = 24;
constexpr int WINDOW_MOST = 160;
constexpr float COST_OBJECT = 5.0f, COST_WALL = 20.0f, COST_LADDER = 40.0f, COST_STAIRS = 60.0f;
constexpr float BREAK_REACH = 0.9f;   // from the nearest edge of what it breaks (a unit beside a tile is about 0.5 from it)

int tileOf(float v) { return (int)std::floor(v); }

// The piece that closes each ground tile, for every kind of piece that closes any (not only the ones in the building map, which holds
// what stands on foundations: a Perimeter Wall, Gate or Watchtower is not in it). A tile that two pieces close has the first.
using BlockerMap = std::unordered_map<long long, ObjectId>;

BlockerMap blockerMap(World& w) {
    BlockerMap map;
    BlockFootprint fp;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (!blockFootprint(w, e, fp)) continue;
        for (const auto& [x, z] : fp.tiles) map.emplace(tileKey(x, z), e);
    }
    return map;
}

// The health a piece has, or would have once it is hit: set where it is first needed (healthFor)
double defaultHealth(World& w, ObjectId e) {
    const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
    const double tough = materialToughness(materialOf(w, e));
    if (info.perimeter) return (info.solid ? WALL_HEALTH : DOOR_HEALTH) * tough * PERIMETER_STRENGTH;
    if (info.tower) return WALL_HEALTH * 4.0;
    if (info.stairs || info.wallRule) return WALL_HEALTH * tough;
    if (info.door || info.ladder) return DOOR_HEALTH * tough;
    return info.health > 0.0 ? info.health : 10.0;
}

// What breaking a piece costs a unit that is shut in: by kind, and by how tough it is, so that the weakest way out is the one taken
float pieceCost(World& w, ObjectId e) {
    const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
    float base = info.stairs ? COST_STAIRS : info.ladder ? COST_LADDER : (info.wallRule || info.door || info.perimeter || info.tower) ? COST_WALL : COST_OBJECT;
    const Health* h = w.get<Health>(e);
    const double hp = h ? h->max : defaultHealth(w, e);
    return std::max(base, (float)(COST_WALL * hp / WALL_HEALTH));
}

// What a tile costs to cross: 1 open, more where a piece shuts it, -1 for ground that nothing can break (water, cliffs)
float crossCost(World& w, const Terrain& t, const BlockerMap& blockers, int x, int z, ObjectId* blocker) {
    if (blocker) *blocker = NO_OBJECT;
    if (t.walkable(x, z)) return 1.0f;
    if (!t.blockedByBuilding(x, z)) return -1.0f;
    const auto it = blockers.find(tileKey(x, z));
    if (it == blockers.end() || !w.has<Prop>(it->second)) return -1.0f;
    if (blocker) *blocker = it->second;
    return pieceCost(w, it->second);
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
    const double h = defaultHealth(w, e);
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
    // The unit shows an X over its head for a second, and your own units are heard too. Failing again while it shows keeps it showing,
    // without another sound.
    if (TaskFailedEmote* e = w.get<TaskFailedEmote>(u)) {
        e->left = TASK_FAILED_EMOTE_SECONDS;
        return;
    }
    w.add(u, TaskFailedEmote{});
    if (factionOf(w, u) == FACTION_PLAYER && !w.has<Mob>(u)) queueSoundFrom(w, Sfx::TaskFailed, u);
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
    const BlockerMap m = blockerMap(w);
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
    // The tiles it covers, of whatever size (a gone footprint, a gate that has opened meanwhile, is a piece in the way no more)
    BlockFootprint fp;
    if (!blockFootprint(w, target, fp)) return true;
    int x0 = fp.tiles.front().first, x1 = x0, z0 = fp.tiles.front().second, z1 = z0;
    for (const auto& [x, z] : fp.tiles) {
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        z0 = std::min(z0, z); z1 = std::max(z1, z);
    }
    // How far it stands from the nearest edge of that rectangle
    const float gx = ut->x - std::clamp(ut->x, (float)x0, (float)(x1 + 1)), gz = ut->z - std::clamp(ut->z, (float)z0, (float)(z1 + 1));
    if (std::hypot(gx, gz) > BREAK_REACH) {
        if (moving) return false;
        // To the open tile beside it (along any side) that is nearest
        float bestD = 1e30f;
        Vec2 spot{ ut->x, ut->z };
        for (int z = z0 - 1; z <= z1 + 1; ++z) {
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                const bool inX = x >= x0 && x <= x1, inZ = z >= z0 && z <= z1;
                if (inX == inZ || !t.walkable(x, z)) continue;   // a tile beside a side, not inside or at a corner
                const float dd = std::hypot((float)x + 0.5f - ut->x, (float)z + 0.5f - ut->z);
                if (dd < bestD) {
                    bestD = dd;
                    spot = Vec2{ (float)x + 0.5f, (float)z + 0.5f };
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
    if (const Owner* ow = w.get<Owner>(target)) patronEvent(w, PatronEvent::Damage, factionOf(w, u), ow->faction);
    queueSoundFrom(w, Sfx::Hit, target);
    if (h->value > 0.0) return false;
    const PropInfo& info = propInfo(w.get<Prop>(target)->kind);
    queueLog(w, langf("MISC_DOOR_SMASHED", { nameOf(w, u), lang(info.nameKey) }), LogKind::Seen, LogWho(false, u));
    destroyBuilding(w, target);
    return true;
}
}

// A unit on the ground that stands in a tile that is closed (a wall built on it, a foundation or door that came to be there, the
// ground made steep) is moved to the nearest open spot. It is seen so on two looks a second apart, so that a tile that is only
// changing is let be. Units carried, on a prop, climbing, or on an upper floor or an Airship are not looked at.
namespace {
// How many open tiles reach from here over open ground, counting no more than `cap`
int openAreaSize(const Terrain& t, int sx, int sz, int cap) {
    std::unordered_set<long long> seen;
    std::vector<std::pair<int, int>> open;
    open.push_back({ sx, sz });
    seen.insert(tileKey(sx, sz));
    while (!open.empty() && (int)seen.size() < cap) {
        const auto [x, z] = open.back();
        open.pop_back();
        const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i], nz = z + dz[i];
            if (!t.walkable(nx, nz) || !seen.insert(tileKey(nx, nz)).second) continue;
            open.push_back({ nx, nz });
        }
    }
    return (int)seen.size();
}

// What the trapped units need to know about the world, worked out once for a pass: the claims, and the tiles a blueprint will close
struct ExitContext {
    std::vector<Claim> claims;
    std::unordered_set<long long> closing;
};

ExitContext exitContext(World& w) {
    ExitContext c;
    c.claims = claimsOf(w);
    BlockFootprint fp;
    for (ObjectId e : w.view<Prop, Transform, Blueprint>()) {
        if (!blockFootprint(w, e, fp, true)) continue;
        for (const auto& [x, z] : fp.tiles) c.closing.insert(tileKey(x, z));
    }
    return c;
}

// The open tile a trapped unit is best put on, or false if none is near. See stuck.h for what makes one better than another.
bool exitSpot(World& w, const Terrain& t, ObjectId u, Vec2 at, const ExitContext& ctx, Vec2& out) {
    const int ux = tileOf(at.x), uz = tileOf(at.z);
    const LastOpenSpot* last = w.get<LastOpenSpot>(u);
    const int faction = factionOf(w, u);
    struct Candidate {
        float score;
        int x, z;
    };
    std::vector<Candidate> found;
    for (int z = uz - TRAPPED_SEARCH_TILES; z <= uz + TRAPPED_SEARCH_TILES; ++z) {
        for (int x = ux - TRAPPED_SEARCH_TILES; x <= ux + TRAPPED_SEARCH_TILES; ++x) {
            if (!t.walkable(x, z)) continue;
            float score = std::hypot((float)x + 0.5f - at.x, (float)z + 0.5f - at.z);
            if (last) score += TRAPPED_LAST_WEIGHT * std::hypot((float)x + 0.5f - last->x, (float)z + 0.5f - last->z);
            found.push_back({ score, x, z });
        }
    }
    if (found.empty()) return false;
    const size_t look = std::min<size_t>(found.size(), TRAPPED_CANDIDATES);
    std::partial_sort(found.begin(), found.begin() + (std::ptrdiff_t)look, found.end(),
                      [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
    const Claim* here = claimAt(ctx.claims, at.x, at.z);
    float best = 1e30f;
    for (size_t i = 0; i < look; ++i) {
        const Candidate& c = found[i];
        float score = c.score;
        const bool fromHere = last && std::hypot((float)c.x + 0.5f - last->x, (float)c.z + 0.5f - last->z) <= 2.0f;
        if (!fromHere && openAreaSize(t, c.x, c.z, TRAPPED_POCKET_TILES) < TRAPPED_POCKET_TILES) score += TRAPPED_POCKET_COST;
        if (const Claim* cl = claimAt(ctx.claims, (float)c.x + 0.5f, (float)c.z + 0.5f);
            cl && cl != here && cl->owner != faction && factionsHostile(w, faction, cl->owner)) {
            score += TRAPPED_ENEMY_COST;
        }
        if (ctx.closing.count(tileKey(c.x, c.z))) score += TRAPPED_BLUEPRINT_COST;
        if (score < best) {
            best = score;
            out = Vec2{ (float)c.x + 0.5f, (float)c.z + 0.5f };
        }
    }
    return best < 1e30f;
}

// The upper-floor kind of the same: `up` is the floor it is on. A floor that is gone altogether is for climbSystem, which brings it down.
void freeTrappedUpstairs(World& w, const Terrain& t, ObjectId u, Transform& ut, const Upstairs& up) {
    const BuildingMap& m = cachedBuildingMap(w);
    const int tx = tileOf(ut.x), tz = tileOf(ut.z), level = up.level;
    if (floorTileOpen(w, m, tx, tz, level) || !floorSurfaceOn(w, m, tx, tz, level)) {
        w.remove<Trapped>(u);
        return;
    }
    if (!w.has<Trapped>(u)) {
        w.add(u, Trapped{});
        return;
    }
    float bestD = 1e30f;
    int bx = 0, bz = 0;
    for (int z = tz - TRAPPED_SEARCH_TILES / 2; z <= tz + TRAPPED_SEARCH_TILES / 2; ++z) {
        for (int x = tx - TRAPPED_SEARCH_TILES / 2; x <= tx + TRAPPED_SEARCH_TILES / 2; ++x) {
            if (!floorTileOpen(w, m, x, z, level)) continue;
            const float d = std::hypot((float)x + 0.5f - ut.x, (float)z + 0.5f - ut.z);
            if (d < bestD) {
                bestD = d;
                bx = x;
                bz = z;
            }
        }
    }
    if (bestD >= 1e30f) return;   // no floor to stand on near: it stays, and is looked at again
    ut.x = (float)bx + 0.5f;
    ut.z = (float)bz + 0.5f;
    ut.y = t.heightAt(ut.x, ut.z) + storeyBaseHeight(level);
    if (const auto it = m.foundation.find(tileKey(bx, bz)); it != m.foundation.end()) w.get<Upstairs>(u)->foundation = it->second;
    w.remove<SoloMove>(u);
    w.remove<Route>(u);
    w.remove<Trapped>(u);
    queueLog(w, langf("MISC_UNSTUCK", { nameOf(w, u), std::to_string(bx), std::to_string(bz) }), LogKind::Seen, LogWho(false, u));
}
}

void freeTrappedSystem(World& w, Terrain& t, float dt) {
    static float wait = 0.0f;
    wait += dt;
    if (wait < FREE_TRAPPED_SECONDS) return;
    wait = 0.0f;
    std::unique_ptr<ExitContext> ctx;   // only worked out if someone is to be moved
    for (ObjectId u : w.view<Entity, Transform>()) {
        Transform* ut = w.get<Transform>(u);
        if (!ut || w.has<Carried>(u) || w.has<Occupying>(u) || w.has<Climbing>(u)) {
            w.remove<Trapped>(u);
            continue;
        }
        // On an upper floor: stood where a wall or a stairs' step now is (one built under it, stairs put up where it stood), it is
        // moved to the nearest tile of that floor it can stand on
        if (const Upstairs* up = w.get<Upstairs>(u)) {
            freeTrappedUpstairs(w, t, u, *ut, *up);
            continue;
        }
        if (storeyOf(w, u) != 0) {
            w.remove<Trapped>(u);
            continue;
        }
        if (openTile(t, ut->x, ut->z)) {
            w.remove<Trapped>(u);
            // Where it stands in the open, to come back to if the ground under it closes
            if (t.isFree(ut->x, ut->z)) {
                if (LastOpenSpot* l = w.get<LastOpenSpot>(u)) *l = LastOpenSpot{ ut->x, ut->z };
                else w.add(u, LastOpenSpot{ ut->x, ut->z });
            }
            continue;
        }
        if (!w.has<Trapped>(u)) {
            w.add(u, Trapped{});
            continue;
        }
        if (!ctx) ctx = std::make_unique<ExitContext>(exitContext(w));
        Vec2 to{ 0.0f, 0.0f };
        if (!exitSpot(w, t, u, Vec2{ ut->x, ut->z }, *ctx, to)) continue;   // nowhere near is open: it stays, and is looked at again
        ut->x = to.x;
        ut->z = to.z;
        ut->y = t.standHeight(to.x, to.z);
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
        w.remove<Trapped>(u);
        queueLog(w, langf("MISC_UNSTUCK", { nameOf(w, u), std::to_string(tileOf(to.x)), std::to_string(tileOf(to.z)) }), LogKind::Seen,
                 LogWho(false, u));
    }
}

namespace {
// A unit that is on its feet on the ground and has a walk under way: the ones a forced check looks at
bool walkWatched(World& w, ObjectId u) {
    if (!w.has<Transform>(u) || w.has<Route>(u) || w.has<Climbing>(u) || w.has<Carried>(u) || w.has<Occupying>(u) || w.has<PlayingDead>(u) ||
        outCold(w, u) || storeyOf(w, u) != 0) {
        return false;
    }
    const SoloMove* so = w.get<SoloMove>(u);
    return so && so->next < so->waypoints.size();
}

// Which task the unit is on, as far as a walk goes: the step it is taking and what it is about (0 for a walk of its own)
unsigned long long taskOf(World& w, ObjectId u) {
    const Behaviors* b = w.get<Behaviors>(u);
    if (!b || !b->busy) return 0;
    return (((unsigned long long)(unsigned)idx(b->step.kind) + 1ull) << 32) ^ (unsigned long long)(unsigned)b->stepProp;
}

void forceStuckCheck(World& w, Terrain& t, ObjectId u, Vec2 here, Vec2 goal) {
    const std::string gx = std::to_string(tileOf(goal.x)), gz = std::to_string(tileOf(goal.z));
    queueLog(w, langf("MISC_STUCK_NOMOVE", { nameOf(w, u), gx, gz, std::to_string((int)STUCK_FORCE_SECONDS) }), LogKind::Stuck, npcOnly(w, u));
    const bool reachable = stuckReachable(w, t, here, goal);
    queueLog(w, langf(reachable ? "MISC_STUCK_REPLAN" : "MISC_STUCK_NOWAY", { nameOf(w, u), gx, gz }), LogKind::Stuck, npcOnly(w, u));
    // The stuck look follows this walk from here on, and its next look helps it along at once
    const PathGoal* old = w.get<PathGoal>(u);
    if (!old || std::hypot(old->goal.x - goal.x, old->goal.z - goal.z) > 0.01f) setPathGoal(w, u, goal);
    PathGoal* g = w.get<PathGoal>(u);
    g->mark = here;
    g->hasMark = true;
    g->still = STUCK_STILL_SECONDS;
    // No open way: the walk is dropped, so that it stands idle short of its goal and the shut-in rounds (wander, then break) begin
    if (!reachable) w.remove<SoloMove>(u);
}

void moveWatchSystem(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<MoveWatch>()) {
        if (!walkWatched(w, u)) w.remove<MoveWatch>(u);
    }
    for (ObjectId u : w.view<SoloMove, Transform>()) {
        if (!walkWatched(w, u)) continue;
        const SoloMove* so = w.get<SoloMove>(u);
        const Transform* ut = w.get<Transform>(u);
        const Vec2 here{ ut->x, ut->z }, goal = so->waypoints.back();
        const unsigned long long task = taskOf(w, u);
        MoveWatch* m = w.get<MoveWatch>(u);
        if (!m || m->task != task || std::hypot(m->goal.x - goal.x, m->goal.z - goal.z) > 0.01f) {
            // A new walk or task: the clock starts from here
            if (!m) m = &w.add(u, MoveWatch{});
            *m = MoveWatch{ here, goal, task, 0.0f };
            continue;
        }
        m->elapsed += dt;
        if (m->elapsed < STUCK_FORCE_SECONDS) continue;
        m->elapsed = 0.0f;
        const Vec2 was = m->at;
        m->at = here;
        if (std::hypot(here.x - was.x, here.z - was.z) >= STUCK_STILL_DISTANCE) continue;   // it has been getting somewhere
        forceStuckCheck(w, t, u, here, goal);
    }
}
}

void stuckSystem(World& w, Terrain& t, Rng& rng, float dt) {
    for (ObjectId u : w.view<TaskFailedEmote>()) {
        if ((w.get<TaskFailedEmote>(u)->left -= dt) <= 0.0f) w.remove<TaskFailedEmote>(u);
    }
    freeTrappedSystem(w, t, dt);
    moveWatchSystem(w, t, dt);
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
