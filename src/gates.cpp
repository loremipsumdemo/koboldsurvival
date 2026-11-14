#include "gates.h"
#include <algorithm>
#include <cmath>
#include "building.h"
#include "calendar.h"
#include "components.h"
#include "factions.h"
#include "hostility.h"
#include "mobs.h"
#include "props.h"
#include "schedule.h"
#include "squads.h"
#include "systems_internal.h"
#include "game_tables.h"
#include <functional>
#include <map>

namespace {
int facingOf(World& w, ObjectId gate) {
    const Prop* p = w.get<Prop>(gate);
    const float yaw = p ? p->yaw : 0.0f;
    return (((int)std::lround(yaw / 1.5707963f)) % 4 + 4) % 4;
}

// A unit of yours that can work a gate if it is placed right: awake, on its feet and not in a bed
bool ableToWork(World& w, ObjectId u, int faction) {
    if (factionOf(w, u) != faction || w.has<Mob>(u) || outCold(w, u) || w.has<Carried>(u) || w.has<Occupying>(u)) return false;
    return w.has<Commandable>(u) || w.has<Citizen>(u);
}
}

Vec2 gateOutward(World& w, ObjectId gate) {
    static const float DX[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, DZ[4] = { -1.0f, 0.0f, 1.0f, 0.0f };   // north, east, south, west
    const int f = facingOf(w, gate);
    return Vec2{ DX[f], DZ[f] };
}

Vec2 gateCenter(World& w, ObjectId gate) {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    pieceTiles(w, gate, x0, z0, x1, z1);
    return Vec2{ (float)(x0 + x1 + 1) * 0.5f, (float)(z0 + z1 + 1) * 0.5f };
}

float gateSideOf(World& w, ObjectId gate, float x, float z) {
    const Vec2 c = gateCenter(w, gate), out = gateOutward(w, gate);
    return (x - c.x) * out.x + (z - c.z) * out.z;
}

Vec2 gateSpot(World& w, ObjectId gate, float beyond) {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    pieceTiles(w, gate, x0, z0, x1, z1);
    const Vec2 c = gateCenter(w, gate), out = gateOutward(w, gate);
    // Half the footprint's depth along the facing
    const float half = (std::fabs(out.x) > 0.5f ? (float)(x1 - x0 + 1) : (float)(z1 - z0 + 1)) * 0.5f;
    const float d = beyond >= 0.0f ? half + beyond : -(half - beyond);
    return Vec2{ c.x + out.x * d, c.z + out.z * d };
}

float gateGap(World& w, ObjectId gate, float x, float z) {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    pieceTiles(w, gate, x0, z0, x1, z1);
    const float dx = std::max({ (float)x0 - x, 0.0f, x - (float)(x1 + 1) });
    const float dz = std::max({ (float)z0 - z, 0.0f, z - (float)(z1 + 1) });
    return std::hypot(dx, dz);
}

ObjectId gateOperator(World& w, ObjectId gate) {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    pieceTiles(w, gate, x0, z0, x1, z1);
    const float across = (float)std::max(x1 - x0 + 1, z1 - z0 + 1) * 0.5f + 3.0f;
    const Vec2 c = gateCenter(w, gate), out = gateOutward(w, gate);
    ObjectId best = NO_OBJECT;
    float bestGap = 0.0f;
    const int owner = systems_detail::ownerOf(w, gate);
    for (ObjectId u : w.view<Entity, Transform>()) {
        if (!ableToWork(w, u, owner)) continue;
        const Transform* t = w.get<Transform>(u);
        const float gap = gateGap(w, gate, t->x, t->z);
        bool ok = false;
        if (w.has<Upstairs>(u)) {
            ok = storeyOf(w, u) == 1 && gap <= 1.0f;   // on the walkway on top
        } else if (storeyOf(w, u) == 0) {
            const float side = (t->x - c.x) * out.x + (t->z - c.z) * out.z;
            const float along = std::fabs((t->x - c.x) * -out.z + (t->z - c.z) * out.x);
            ok = side < -0.5f && gap <= GATE_WORK_RANGE && along <= across;   // inside, never outside
        }
        if (ok && (best == NO_OBJECT || gap < bestGap)) {
            best = u;
            bestGap = gap;
        }
    }
    return best;
}

ObjectId gateAnswerer(World& w, ObjectId gate) {
    bool hasKeeper = false;
    for (ObjectId u : w.view<GateKeeper>()) {
        if (w.get<GateKeeper>(u)->gate != gate) continue;
        hasKeeper = true;
        if (ableToWork(w, u, systems_detail::ownerOf(w, gate))) return u;
    }
    return hasKeeper ? NO_OBJECT : gateOperator(w, gate);
}

bool hostileNearGate(World& w, ObjectId gate) {
    const Vec2 c = gateCenter(w, gate);
    const int owner = systems_detail::ownerOf(w, gate);
    for (ObjectId u : w.view<Entity, Transform>()) {
        if (outCold(w, u) || (owner == FACTION_PLAYER ? !hostileToPlayer(w, u) : !factionsHostile(w, factionOf(w, u), owner))) continue;
        const Transform* t = w.get<Transform>(u);
        if (distSq(t->x, t->z, c.x, c.z) <= GATE_SIGHT_RANGE * GATE_SIGHT_RANGE) return true;
    }
    return false;
}

std::vector<ObjectId> ownGates(World& w, int faction) {
    std::vector<ObjectId> out;
    for (ObjectId g : w.view<Prop, Lock>()) {
        if (w.has<Blueprint>(g) || systems_detail::ownerOf(w, g) != faction) continue;
        const PropInfo& info = propInfo(w.get<Prop>(g)->kind);
        if (info.perimeter && !info.solid) out.push_back(g);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void workGate(World& w, ObjectId gate, bool shut) {
    w.remove<GateNightShut>(gate);
    w.remove<GateOpening>(gate);
    setGateShut(w, gate, shut);
}

namespace {
// What a finished job leaves a gate as: it is shut or opened now, and what else is to be known of it is kept
void finishGateWork(World& w, const GateWork& job) {
    switch (job.kind) {
    case GateJob::Request:
        setGateShut(w, job.gate, false);
        w.remove<GateNightShut>(job.gate);
        w.add(job.gate, GateOpening{ GATE_OPEN_SECONDS });
        break;
    case GateJob::Night:
        setGateShut(w, job.gate, true);
        w.remove<GateOpening>(job.gate);
        w.add(job.gate, GateNightShut{});
        break;
    case GateJob::Manual: case GateJob::Dawn: case GateJob::Reshut:
        workGate(w, job.gate, job.shut);
        if (job.kind == GateJob::Manual) {
            if (job.shut) w.add(job.gate, GateHeldShut{});
            else w.remove<GateHeldShut>(job.gate);
        }
        break;
    }
}

bool gateBeingWorked(World& w, ObjectId gate) {
    for (ObjectId u : w.view<GateWork>()) {
        if (w.get<GateWork>(u)->gate == gate) return true;
    }
    return false;
}
}

bool assignGateWork(World& w, ObjectId gate, bool shut, GateJob kind, ObjectId worker) {
    if (!w.has<Prop>(gate) || gateShut(w, gate) == shut) return false;
    if (gateBeingWorked(w, gate)) return true;
    if (!shut && hostileNearGate(w, gate)) return false;   // never opened with a hostile in sight
    // Its keeper, if it can: awake, on its feet, and not far from it
    const int owner = systems_detail::ownerOf(w, gate);
    if (worker == NO_OBJECT || !ableToWork(w, worker, owner)) {
        for (ObjectId u : w.view<GateKeeper>()) {
            if (w.get<GateKeeper>(u)->gate == gate && ableToWork(w, u, owner) && !w.has<GateWork>(u) && storeyOf(w, u) == 0) worker = u;
        }
    }
    if (worker == NO_OBJECT || !ableToWork(w, worker, owner)) worker = gateOperator(w, gate);
    if (worker == NO_OBJECT || w.has<GateWork>(worker)) return false;
    GateWork job;
    job.gate = gate;
    job.shut = shut;
    job.kind = kind;
    w.add(worker, job);
    return true;
}

bool gateWorkStep(World& w, Terrain& t, ObjectId u, float dt) {
    GateWork* job = w.get<GateWork>(u);
    if (!job) return false;
    const Transform* ut = w.get<Transform>(u);
    const bool stale = !ut || !w.has<Prop>(job->gate) || w.has<Blueprint>(job->gate) || !ableToWork(w, u, factionOf(w, u)) || gateShut(w, job->gate) == job->shut ||
                       (!job->shut && hostileNearGate(w, job->gate));
    if (stale) {
        w.remove<GateWork>(u);
        return false;
    }
    // Where it works it: from just inside the gate, or from up on it
    const bool onTop = onPerimeterTop(w, u) && gateGap(w, job->gate, ut->x, ut->z) <= 1.0f;
    const Vec2 spot = gateSpot(w, job->gate, -1.0f);
    if (!onTop) {
        if (storeyOf(w, u) != 0) {
            w.remove<GateWork>(u);
            return false;
        }
        if (std::hypot(ut->x - spot.x, ut->z - spot.z) > GATE_KEEP_REACH + 0.5f) {
            if (!systems_detail::walkingNow(w, u)) systems_detail::autoWalk(w, t, u, spot, 0);
            return true;
        }
    }
    w.remove<SoloMove>(u);
    w.remove<Route>(u);
    job->left -= dt;
    if (job->left > 0.0f) return true;
    const GateWork done = *job;
    w.remove<GateWork>(u);
    finishGateWork(w, done);
    return false;
}

namespace {
struct RoutePiece {
    ObjectId id = NO_OBJECT;
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    Vec2 on{};   // a tile of its walkway, the one nearest its middle
    bool gate = false;
};

// Whether two footprints share an edge (or overlap): a diagonal touch is no way across
bool joined(const RoutePiece& a, const RoutePiece& b) {
    const int ax1 = a.x1 + 1, az1 = a.z1 + 1, bx1 = b.x1 + 1, bz1 = b.z1 + 1;
    const bool overlapX = a.x0 < bx1 && b.x0 < ax1, overlapZ = a.z0 < bz1 && b.z0 < az1;
    const bool touchX = ax1 == b.x0 || bx1 == a.x0, touchZ = az1 == b.z0 || bz1 == a.z0;
    return (overlapX && overlapZ) || (overlapX && touchZ) || (touchX && overlapZ);
}

// How far apart two footprints are across the ground (0 if they touch)
float footprintGap(int ax0, int az0, int ax1, int az1, const RoutePiece& b) {
    const float dx = (float)std::max({ ax0 - (b.x1 + 1), b.x0 - (ax1 + 1), 0 });
    const float dz = (float)std::max({ az0 - (b.z1 + 1), b.z0 - (az1 + 1), 0 });
    return std::hypot(dx, dz);
}

void computeRoute(World& w, PerimeterRoute& rt, int faction) {
    rt.available = false;
    rt.loop = false;
    rt.points.clear();
    rt.gateTops.clear();
    // The walkway tiles of each piece
    std::map<ObjectId, std::vector<Vec2>> tops;
    for (const auto& [lk, piece] : buildingMap(w).perimTop) {
        const long long tk = lk / 16;
        tops[piece].push_back(Vec2{ (float)(tk % 1000003LL) + 0.5f, (float)(tk / 1000003LL) + 0.5f });
    }
    std::vector<RoutePiece> pieces;
    std::vector<RoutePiece> slopes;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (w.has<Blueprint>(p) || systems_detail::ownerOf(w, p) != faction) continue;
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        if (!info.perimeter && !info.slope) continue;
        RoutePiece rp;
        rp.id = p;
        pieceTiles(w, p, rp.x0, rp.z0, rp.x1, rp.z1);
        if (info.slope) {
            slopes.push_back(rp);
            continue;
        }
        rp.gate = !info.solid;
        const auto it = tops.find(p);
        if (it == tops.end() || it->second.empty()) continue;
        const Transform* tr = w.get<Transform>(p);
        float best = 0.0f;
        bool first = true;
        for (const Vec2& v : it->second) {
            const float d = distSq(v.x, v.z, tr->x, tr->z);
            if (first || d < best) {
                best = d;
                rp.on = v;
                first = false;
            }
        }
        pieces.push_back(rp);
    }
    const int n = (int)pieces.size();
    if (n == 0) return;
    std::sort(pieces.begin(), pieces.end(), [](const RoutePiece& a, const RoutePiece& b) { return a.id < b.id; });
    std::vector<std::vector<int>> adj((size_t)n);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (!joined(pieces[(size_t)i], pieces[(size_t)j])) continue;
            adj[(size_t)i].push_back(j);
            adj[(size_t)j].push_back(i);
        }
    }
    // The largest run of joined pieces
    std::vector<int> comp((size_t)n, -1), size;
    for (int i = 0; i < n; ++i) {
        if (comp[(size_t)i] >= 0) continue;
        const int c = (int)size.size();
        size.push_back(0);
        std::vector<int> stack{ i };
        comp[(size_t)i] = c;
        while (!stack.empty()) {
            const int k = stack.back();
            stack.pop_back();
            ++size[(size_t)c];
            for (int j : adj[(size_t)k]) {
                if (comp[(size_t)j] < 0) {
                    comp[(size_t)j] = c;
                    stack.push_back(j);
                }
            }
        }
    }
    // A gate in a run of pieces that a Slope reaches can be stood on top of
    {
        std::vector<char> reachedRun(size.size(), 0);
        for (const RoutePiece& s : slopes) {
            for (int i = 0; i < n; ++i) {
                if (footprintGap(s.x0, s.z0, s.x1, s.z1, pieces[(size_t)i]) <= 1.5f) reachedRun[(size_t)comp[(size_t)i]] = 1;
            }
        }
        for (int i = 0; i < n; ++i) {
            if (pieces[(size_t)i].gate && reachedRun[(size_t)comp[(size_t)i]]) rt.gateTops[pieces[(size_t)i].id] = pieces[(size_t)i].on;
        }
    }
    const int largest = (int)(std::max_element(size.begin(), size.end()) - size.begin());
    std::vector<int> members;
    for (int i = 0; i < n; ++i) {
        if (comp[(size_t)i] == largest) members.push_back(i);
    }
    // Reached by a Slope: one standing against any piece of the run
    bool reached = false;
    for (const RoutePiece& s : slopes) {
        for (int i : members) {
            if (footprintGap(s.x0, s.z0, s.x1, s.z1, pieces[(size_t)i]) <= 1.5f) reached = true;
        }
    }
    if (!reached) return;

    bool ring = members.size() >= 3;
    for (int i : members) ring = ring && adj[(size_t)i].size() == 2;
    std::vector<int> order;
    if (ring) {
        // All the way round
        int prev = -1, cur = members.front();
        do {
            order.push_back(cur);
            const std::vector<int>& next = adj[(size_t)cur];
            const int go = next[0] != prev ? next[0] : next[1];
            prev = cur;
            cur = go;
        } while (cur != members.front() && order.size() <= members.size());
        rt.loop = true;
    } else {
        // Along every length and back, so that it goes on round and round: out from an end, and back through each branch
        int start = members.front();
        for (int i : members) {
            if (adj[(size_t)i].size() == 1) {
                start = i;
                break;
            }
        }
        std::vector<char> seen((size_t)n, 0);
        std::function<void(int)> walk = [&](int i) {
            seen[(size_t)i] = 1;
            order.push_back(i);
            for (int j : adj[(size_t)i]) {
                if (seen[(size_t)j]) continue;
                walk(j);
                order.push_back(i);
            }
        };
        walk(start);
        if (order.size() > 1 && order.back() == order.front()) order.pop_back();
    }
    for (int i : order) rt.points.push_back(pieces[(size_t)i].on);
    rt.available = !rt.points.empty();
}
}

const PerimeterRoute& perimeterRoute(World& w, int faction) {
    static const PerimeterRoute none;
    const auto& by = w.resource<PerimeterRoutes>().by;
    const auto it = by.find(faction);
    return it == by.end() ? none : it->second;
}

bool isPerimeterWork(World& w, ObjectId piece) {
    const Prop* p = w.get<Prop>(piece);
    if (!p) return false;
    const PropInfo& info = propInfo(p->kind);
    return info.perimeter || info.slope || info.tower || info.key == "GuardPost";
}

bool onPerimeterTop(World& w, ObjectId u) {
    const Transform* t = w.get<Transform>(u);
    if (!t || !w.has<Upstairs>(u) || storeyOf(w, u) != 1) return false;
    return buildingMap(w).perimTop.count(levelKey((int)std::floor(t->x), (int)std::floor(t->z), 1)) > 0;
}

bool climbDownPerimeter(World& w, const Terrain& t, ObjectId u) {
    Transform* tr = w.get<Transform>(u);
    if (!tr || !w.has<Upstairs>(u) || storeyOf(w, u) != 1) return false;
    const BuildingMap m = buildingMap(w);
    const auto it = m.perimTop.find(levelKey((int)std::floor(tr->x), (int)std::floor(tr->z), 1));
    if (it == m.perimTop.end()) return false;
    const ObjectId piece = it->second;
    const Vec2 facing = gateOutward(w, piece);   // the way it faces; the inner side is behind it
    const Vec2 spot = gateSpot(w, piece, -1.0f);
    // The nearest open ground on the inner side, looking further in and then along the wall
    const float sx = -facing.x, sz = -facing.z;   // inward
    const float ax = -facing.z, az = facing.x;    // along
    bool found = false;
    Vec2 at{};
    float best = 0.0f;
    for (int d = 0; d <= 4 && !found; ++d) {
        for (int k = -4; k <= 4; ++k) {
            const float x = spot.x + sx * (float)d + ax * (float)k, z = spot.z + sz * (float)d + az * (float)k;
            if (!t.inBounds((int)std::floor(x), (int)std::floor(z)) || !systems_detail::openTile(t, x, z)) continue;
            const float dist = distSq(x, z, tr->x, tr->z);
            if (!found || dist < best) {
                found = true;
                best = dist;
                at = Vec2{ x, z };
            }
        }
    }
    if (!found) return false;
    tr->x = at.x;
    tr->z = at.z;
    tr->y = t.standHeight(at.x, at.z);
    w.remove<Upstairs>(u);
    w.remove<SoloMove>(u);
    w.remove<Route>(u);
    w.remove<Climbing>(u);
    w.remove<Patrolling>(u);
    return true;
}

void gateSystem(World& w, double clockSeconds, float dt) {
    struct Watch {
        float timer = 0.0f;
    };
    Watch& watch = w.resource<Watch>();
    watch.timer += dt;
    if (watch.timer < 1.0f) return;
    const float step = watch.timer;
    watch.timer = 0.0f;

    // Yours, and every NPC faction with an Altar's claim: each keeps its own gates, posts, keepers and route
    const std::vector<Claim> claims = claimsOf(w);
    std::vector<int> factions{ FACTION_PLAYER };
    for (const Claim& c : claims) {
        if (c.camp || c.owner == FACTION_PLAYER || !factionCanClaim(c.owner) || isMobFaction(c.owner)) continue;
        if (std::find(factions.begin(), factions.end(), c.owner) == factions.end()) factions.push_back(c.owner);
    }
    PropKind postKind;
    const bool hasPostKind = propKindNamed("GuardPost", postKind);
    const int hourNow = clockOf(clockSeconds).hour;

    for (const int faction : factions) {
        const std::vector<ObjectId> gates = ownGates(w, faction);
        // A unit with no claim of its faction around it (outside the Perimeter altogether) takes no duty, and keeps none: it is sent in first
        auto inClaim = [&](ObjectId u) {
            const Transform* t = w.get<Transform>(u);
            return t && claimAt(claims, t->x, t->z, faction) != nullptr;
        };
        // Duties that have lost what they were for
        for (ObjectId u : w.view<GateKeeper>()) {
            if (factionOf(w, u) != faction) continue;
            const GateKeeper* k = w.get<GateKeeper>(u);
            if (!w.has<Fortified>(u) || outCold(w, u) || !inClaim(u) || !std::binary_search(gates.begin(), gates.end(), k->gate)) w.remove<GateKeeper>(u);
        }
        std::vector<ObjectId> posts;
        if (hasPostKind) {
            for (ObjectId p : w.view<Prop, Transform>()) {
                if (w.get<Prop>(p)->kind == postKind && !w.has<Blueprint>(p) && systems_detail::ownerOf(w, p) == faction) posts.push_back(p);
            }
            std::sort(posts.begin(), posts.end());
        }
        for (ObjectId u : w.view<GuardPostDuty>()) {
            if (factionOf(w, u) != faction) continue;
            const GuardPostDuty* d = w.get<GuardPostDuty>(u);
            if (!w.has<Fortified>(u) || outCold(w, u) || !inClaim(u) || w.has<GateKeeper>(u) || !std::binary_search(posts.begin(), posts.end(), d->post)) {
                w.remove<GuardPostDuty>(u);
            }
        }

        // The Fortified are given their duties in order: each gate's keeper first, then a guard at each Guard Post. What is left over marches
        // the route.
        const std::vector<ObjectId> fortified = fortifiedUnits(w, faction);
        {
            std::vector<ObjectId> free;
            for (ObjectId u : fortified) {
                if (outCold(w, u) || w.has<Carried>(u) || !w.get<Transform>(u) || !inClaim(u)) continue;
                if (!w.has<GateKeeper>(u) && !w.has<GuardPostDuty>(u)) free.push_back(u);
            }
            auto nearestFree = [&](float x, float z) {
                size_t best = 0;
                float bestD = -1.0f;
                for (size_t i = 0; i < free.size(); ++i) {
                    const Transform* t = w.get<Transform>(free[i]);
                    const float d = distSq(t->x, t->z, x, z);
                    if (bestD < 0.0f || d < bestD) {
                        bestD = d;
                        best = i;
                    }
                }
                return best;
            };
            for (ObjectId g : gates) {
                if (free.empty()) break;
                bool held = false;
                for (ObjectId u : w.view<GateKeeper>()) held = held || w.get<GateKeeper>(u)->gate == g;
                if (held) continue;
                const Vec2 c = gateCenter(w, g);
                const size_t i = nearestFree(c.x, c.z);
                w.add(free[i], GateKeeper{ g });
                free.erase(free.begin() + (long)i);
            }
            for (ObjectId p : posts) {
                if (free.empty()) break;
                bool held = false;
                for (ObjectId u : w.view<GuardPostDuty>()) held = held || w.get<GuardPostDuty>(u)->post == p;
                if (held) continue;
                const Transform* pt = w.get<Transform>(p);
                const size_t i = nearestFree(pt->x, pt->z);
                w.add(free[i], GuardPostDuty{ p });
                free.erase(free.begin() + (long)i);
            }
        }

        // The route: looked at again when the faction's pieces change, and while it is not available at most once an hour (only with
        // Fortified units)
        {
            PerimeterRoute& rt = w.resource<PerimeterRoutes>().by[faction];
            const long long hourCount = (long long)(clockSeconds / 3600.0);
            const std::uint64_t rev = w.revision<Prop>() + 31u * w.revision<Blueprint>();
            if (fortified.empty()) {
                rt.computed = false;
                rt.available = false;
            } else if (!rt.computed || (rev != rt.rev && (rt.available || hourCount != rt.hour)) || (!rt.available && hourCount != rt.hour)) {
                computeRoute(w, rt, faction);
                rt.computed = true;
                rt.rev = rev;
                rt.hour = hourCount;
            }
        }
        if (gates.empty()) continue;

        const bool night = perimeterClosedAtHour(w, hourNow, faction);   // its Perimeter Closed hours (schedule.h; an NPC faction's Gate Hours rule)
        for (ObjectId g : gates) {
            const bool shut = gateShut(w, g);
            const ObjectId operatorUnit = gateAnswerer(w, g);
            if (night) w.remove<GateHeldShut>(g);   // a gate shut by hand opens again after the closed hours
            // Who is at the gate: friendly units just outside, asking to come in, and anyone standing in it
            bool asking = false, inGate = false;
            for (ObjectId u : w.view<Entity, Transform>()) {
                if (factionOf(w, u) != faction || w.has<Mob>(u) || outCold(w, u) || storeyOf(w, u) != 0) continue;
                const Transform* t = w.get<Transform>(u);
                const float gap = gateGap(w, g, t->x, t->z);
                if (gap <= 0.3f) inGate = true;
                else if (gap <= GATE_REQUEST_RANGE && gateSideOf(w, g, t->x, t->z) > 0.0f) asking = true;
            }
            if (shut) {
                if (operatorUnit == NO_OBJECT) continue;
                // In the open hours a shut gate is opened (unless someone shut it by hand); in the closed hours one is opened for whoever
                // asks, if nothing hostile is in sight
                if (!night && !w.has<GateHeldShut>(g)) {
                    assignGateWork(w, g, false, GateJob::Dawn, operatorUnit);
                    continue;
                }
                if (asking && !hostileNearGate(w, g)) assignGateWork(w, g, false, GateJob::Request, operatorUnit);
                continue;
            }
            // Open: one opened for a request stays while anyone is at it or in it, and then shuts after a while
            if (GateOpening* go = w.get<GateOpening>(g)) {
                if (!night) {
                    w.remove<GateOpening>(g);   // the open hours: it stays open
                    continue;
                }
                if (asking || inGate) go->left = GATE_OPEN_SECONDS;
                else go->left -= step;
                if (go->left <= 0.0f && operatorUnit != NO_OBJECT && !asking && !inGate) assignGateWork(w, g, true, GateJob::Reshut, operatorUnit);
                continue;
            }
            // Closed hours: shut, from inside or on top, once nobody is in it or asking
            if (night && operatorUnit != NO_OBJECT && !inGate && !asking) assignGateWork(w, g, true, GateJob::Night, operatorUnit);
        }
    }
}
