#include "npc_builders.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include "behavior.h"
#include "behavior_groups.h"
#include "build.h"
#include "building.h"
#include "claims.h"
#include "components.h"
#include "factions.h"
#include "items.h"
#include "npc_camps.h"
#include "npc_roles.h"
#include "spatial.h"
#include "systems_internal.h"
#include <map>
#include <queue>
#include <set>

using namespace systems_detail;

namespace {
struct BuilderClock {
    float timer = 0.0f;
    double hours = 0.0;
};

bool npcAltarClaim(const Claim& c) {
    return !c.camp && c.owner != FACTION_PLAYER && factionCanClaim(c.owner) && !isMobFaction(c.owner);
}

// How much of an item a faction has in its stores (its own, or in its claim)
int stockOf(World& w, const std::vector<Claim>& claims, int faction, int item) {
    int total = 0;
    for (ObjectId p : w.view<Prop, Storage>()) {
        if (w.has<Blueprint>(p) || !propOfFaction(w, claims, p, faction)) continue;
        for (const ItemStack& s : w.get<Storage>(p)->slots) {
            if (s.item > 0 && baseItemId(s.item) == item) total += s.count;
        }
    }
    return total;
}

// Whether a point is in the turned plan's box, from where it is
bool inBox(const PlanSite& s, const BuildPlan& turned, float x, float z) {
    return x >= (float)s.x && z >= (float)s.z && x < (float)(s.x + turned.w) && z < (float)(s.z + turned.d);
}

const BuildPlan* findPlan(const std::string& name) {
    for (const BuildPlan& p : planLibrary()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// Whether a ghost may become a blueprint now: a piece on an upper floor needs that floor built under it
bool ghostReady(World& w, ObjectId g, int faction) {
    const Prop* p = w.get<Prop>(g);
    const Transform* tr = w.get<Transform>(g);
    if (!p || !tr) return false;
    const int level = storeyOf(w, g);
    if (level <= 0 || propInfo(p->kind).upperFloor) return true;
    return floorSurfaceAt(w, (int)std::floor(tr->x), (int)std::floor(tr->z), level, faction);
}

// Where a site's building stands, for the ghosts that no site owns
struct SiteBox {
    int faction, x, z, w, d;
};

// Auto-Construct for the faction's citizens, so they build what is laid
void giveBuilders(World& w, int faction) {
    BehaviorRule build;
    build.kind = BehaviorKind::AutoConstruct;
    for (ObjectId u : w.view<Citizen, Behaviors>()) {
        if (factionOf(w, u) != faction) continue;
        Behaviors* b = w.get<Behaviors>(u);
        const bool has = std::any_of(b->rules.begin(), b->rules.end(), [](const BehaviorRule& r) { return r.kind == BehaviorKind::AutoConstruct; });
        if (!has) addBehavior(b->rules, build);
    }
}

// The tasks your citizens start with (sleeping, the recreation tasks and the rest), for the faction's citizens, so they sleep, rest,
// socialize, sit, play and dance as yours do, on the schedule they keep (schedule.h). What an NPC faction's units were never given
// is added; the nap level is the same as yours.
void giveCitizenTasks(World& w, int faction) {
    static const std::vector<BehaviorRule> wanted = defaultCitizenBehaviors();
    for (ObjectId u : w.view<Citizen, Behaviors>()) {
        if (factionOf(w, u) != faction || w.has<Corpse>(u) || w.has<Mob>(u) || w.has<Fortified>(u)) continue;   // the Fortified keep their own tasks
        Behaviors* b = w.get<Behaviors>(u);
        bool changed = false;
        for (const BehaviorRule& r : wanted) {
            if (holdsBehavior(b->rules, r)) continue;
            if (addBehavior(b->rules, r, BEHAVIOR_TASKS_MAX)) changed = true;
        }
        for (BehaviorRule& r : b->rules) {
            if (r.kind == BehaviorKind::Rest && r.percent != percentRange(BehaviorKind::Rest).lo) {
                r.percent = percentRange(BehaviorKind::Rest).lo;
                changed = true;
            }
        }
        if (changed) b->pending = true;
    }
}

// Auto-Haul Ground Items and an Auto-Haul to each of the faction's stores in its claim, for its citizens, so what lies on the
// ground in its claim is put away, as it is for yours (behavior_groups.cpp). Stores that are gone leave their lists.
void giveHaulers(World& w, const std::vector<Claim>& claims, int faction) {
    std::vector<BehaviorRule> wanted;
    BehaviorRule ground;
    ground.kind = BehaviorKind::AutoHaulGround;
    wanted.push_back(ground);
    for (ObjectId p : w.view<Prop, Storage, Transform>()) {
        if (w.has<Blueprint>(p)) continue;
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        if (info.use != PropUse::Storage || info.claimsLand || info.stall) continue;
        const Owner* own = w.get<Owner>(p);
        if (own && own->faction != faction) continue;
        const Transform* t = w.get<Transform>(p);
        if (!claimAt(claims, t->x, t->z, faction)) continue;
        BehaviorRule haul;
        haul.kind = BehaviorKind::AutoHaul;
        haul.prop = p;
        haul.propKind = idx(w.get<Prop>(p)->kind);
        wanted.push_back(haul);
    }
    for (ObjectId u : w.view<Citizen, Behaviors>()) {
        if (factionOf(w, u) != faction || w.has<Corpse>(u) || w.has<Fortified>(u)) continue;
        Behaviors* b = w.get<Behaviors>(u);
        bool changed = false;
        const size_t before = b->rules.size();
        b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(),
                                      [&](const BehaviorRule& r) { return r.kind == BehaviorKind::AutoHaul && !w.has<Storage>(r.prop); }),
                       b->rules.end());
        changed = b->rules.size() != before;
        for (const BehaviorRule& r : wanted) {
            if (!holdsBehavior(b->rules, r) && addBehavior(b->rules, r)) changed = true;
        }
        if (changed) b->pending = true;
    }
}

// ---- The Perimeter, looked at once a game day --------------------------------------------------------------------------------
struct PerimeterDays {
    std::unordered_map<int, double> since;   // faction -> game hours since it last looked
    std::unordered_map<int, int> said;       // faction -> what was last said of its Perimeter: 1 secure, 2 exposed
};

constexpr double PERIMETER_LOOK_HOURS = 1.0;   // looked at once an in-game hour
constexpr float PERIMETER_MIN_RADIUS = 10.0f;   // a claim this small or smaller has no Perimeter
constexpr int PERIMETER_MOST_LAID = 24;         // pieces laid in one look
constexpr float PERIMETER_EXPAND = 8.0f;        // a claim this much bigger than its finished ring gets another
constexpr int PERIMETER_MOST_PENDING = 16;      // Perimeter blueprints waiting to be built at once

struct RingCell {
    int i, j;      // the cell, in 2 by 2 tiles from the Altar
    int dx, dz;    // the way it faces: out
    bool corner;   // no open side, only an open diagonal
};

// The ring of cells round an Altar: a cell counts if its farthest corner is inside the radius, the ones joined to the middle are
// filled, and the ring is every filled cell with an unfilled one among its eight neighbours. A ring cell faces the open side that
// points furthest from the middle (a corner, the two sides that make up its open diagonal).
std::vector<RingCell> perimeterRing(float claimRadius) {
    std::vector<RingCell> ring;
    const float rc = (claimRadius - 3.0f) * 0.5f;
    if (claimRadius <= PERIMETER_MIN_RADIUS || rc < 1.0f) return ring;
    auto valid = [&](int i, int j) { return (std::abs(i) + 0.5f) * (std::abs(i) + 0.5f) + (std::abs(j) + 0.5f) * (std::abs(j) + 0.5f) <= rc * rc; };
    std::set<std::pair<int, int>> filled{ { 0, 0 } };
    std::queue<std::pair<int, int>> q;
    q.push({ 0, 0 });
    const int N4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    while (!q.empty()) {
        const auto [i, j] = q.front();
        q.pop();
        for (const auto& d : N4) {
            const int ni = i + d[0], nj = j + d[1];
            if (!filled.count({ ni, nj }) && valid(ni, nj)) {
                filled.insert({ ni, nj });
                q.push({ ni, nj });
            }
        }
    }
    for (const auto& [i, j] : filled) {
        bool all = true;
        for (int a = -1; a <= 1; ++a) {
            for (int b = -1; b <= 1; ++b) all = all && filled.count({ i + a, j + b }) > 0;
        }
        if (all) continue;
        std::vector<std::pair<int, int>> cand;
        for (const auto& d : N4) {
            if (!filled.count({ i + d[0], j + d[1] })) cand.push_back({ d[0], d[1] });
        }
        const bool corner = cand.empty();
        if (corner) {
            for (int a = -1; a <= 1; a += 2) {
                for (int b = -1; b <= 1; b += 2) {
                    if (!filled.count({ i + a, j + b })) {
                        cand.push_back({ a, 0 });
                        cand.push_back({ 0, b });
                    }
                }
            }
        }
        std::pair<int, int> best = cand.front();
        int bestV = -(1 << 20);
        for (const auto& c : cand) {
            const int v = c.first * i + c.second * j;
            if (v > bestV) {
                bestV = v;
                best = c;
            }
        }
        ring.push_back(RingCell{ i, j, best.first, best.second, corner });
    }
    return ring;
}

// The way a facing (dx, dz) is turned, as the placement turns a piece (STAIRS_DX, STAIRS_DZ)
float facingYaw(int dx, int dz) {
    for (int d = 0; d < 4; ++d) {
        if (STAIRS_DX[d] == dx && STAIRS_DZ[d] == dz) return (float)d * 1.5707963f;
    }
    return 0.0f;
}

// Whether a prop may be cleared from a Perimeter's way: nothing of anyone's building, nothing of another faction's
bool clearable(World& w, ObjectId p, int faction) {
    if (w.has<Blueprint>(p) || w.has<Foundation>(p)) return false;
    const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
    if (info.foundation || info.wallRule || info.door || info.perimeter || info.tower || info.claimsLand || info.upperFloor || info.stairs ||
        info.ladder || info.onFoundation) {
        return false;
    }
    if (!breakableKind(w.get<Prop>(p)->kind)) return false;
    const Owner* o = w.get<Owner>(p);
    return !o || o->faction == faction;
}

// Where a ring cell's piece stands, snapped as the placement snaps it
void cellSpot(PropKind kind, float ax, float az, const RingCell& cell, float& x, float& z) {
    snapBuildSpot(kind, ax + 2.0f * cell.i, az + 2.0f * cell.j, x, z);
}

PropKind perimeterKind(bool wall) {
    PropKind out = PropKind::Wagon;
    for (int k = 0; k < propKindCount(); ++k) {
        const PropInfo& info = propInfo((PropKind)k);
        if (info.perimeter && info.solid == wall) out = (PropKind)k;
    }
    return out;
}

// Whether any tile of the 2 by 2 piece at a ring spot is covered
bool cellCovered(const std::set<long long>& covered, float x, float z) {
    const int bx = (int)std::floor(x - 0.5f), bz = (int)std::floor(z - 0.5f);
    return covered.count(tileKey(bx, bz)) || covered.count(tileKey(bx + 1, bz)) || covered.count(tileKey(bx, bz + 1)) ||
           covered.count(tileKey(bx + 1, bz + 1));
}

// The tiles of every Perimeter piece a faction has, built or laid
std::set<long long> perimeterCovered(World& w, int owner) {
    std::set<long long> covered;
    for (ObjectId p : w.view<Prop, Transform>()) {
        const Prop* pp = w.get<Prop>(p);
        const PropInfo& info = propInfo(pp->kind);
        if (!info.perimeter || ownerOf(w, p) != owner) continue;
        const Transform* tr = w.get<Transform>(p);
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        propTileRect(info, tr->x, tr->z, pp->yaw, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) covered.insert(tileKey(x, z));
        }
    }
    return covered;
}

// The ring a faction is working on, worked out from what it has laid (nothing is remembered): of the rings the claim's radius or
// any smaller one makes, the one most of whose cells have a piece, the biggest of equals. With nothing laid, the claim's own.
float activeRing(const std::set<long long>& covered, float cx, float cz, float claimRadius) {
    if (covered.empty() || claimRadius <= PERIMETER_MIN_RADIUS) return claimRadius;
    const PropKind wall = perimeterKind(true);
    float ax = 0.0f, az = 0.0f;
    snapBuildSpot(wall, cx, cz, ax, az);
    float best = claimRadius;
    int bestN = 0;
    for (float p = claimRadius; p > PERIMETER_MIN_RADIUS + 1.0f; p -= 0.25f) {
        int n = 0;
        for (const RingCell& cell : perimeterRing(p)) {
            float x = 0.0f, z = 0.0f;
            cellSpot(wall, ax, az, cell, x, z);
            if (cellCovered(covered, x, z)) ++n;
        }
        if (n > bestN) {
            bestN = n;
            best = p;
        }
    }
    return best;
}

// The tiles a faction's new buildings keep out of: the ring its Perimeter would stand on, and a tile round it (a Perimeter piece
// may not touch a foundation). Nothing for a claim of 10 radius or less.
std::unordered_set<long long> perimeterReserve(float cx, float cz, float radius, float other) {
    std::unordered_set<long long> out;
    const PropKind wall = perimeterKind(true);
    float ax = 0.0f, az = 0.0f;
    snapBuildSpot(wall, cx, cz, ax, az);
    for (const float r : { radius, other }) {
        for (const RingCell& cell : perimeterRing(r)) {
            float x = 0.0f, z = 0.0f;
            cellSpot(wall, ax, az, cell, x, z);
            const int bx = (int)std::floor(x - 0.5f), bz = (int)std::floor(z - 0.5f);
            for (int tz = bz - 1; tz <= bz + 2; ++tz) {
                for (int tx = bx - 1; tx <= bx + 2; ++tx) out.insert(tileKey(tx, tz));
            }
        }
    }
    return out;
}

// A blueprint of a kind laid where the rules allow, after clearing any breakable props that are all that is in the way
ObjectId placeClearing(World& w, Terrain& t, const std::vector<Claim>& claims, int owner, PropKind kind, float x, float z, float yaw) {
    BuildCheck check = checkBuildSpot(w, t, kind, x, z, 0, owner, &claims, yaw);
    if (check == BuildCheck::Blocked) {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        propTileRect(propInfo(kind), x, z, yaw, x0, z0, x1, z1);
        std::vector<ObjectId> away;
        for (ObjectId p : w.view<Prop, Transform>()) {
            const Transform* pt = w.get<Transform>(p);
            const int px = (int)std::floor(pt->x), pz = (int)std::floor(pt->z);
            if (px >= x0 && px <= x1 && pz >= z0 && pz <= z1 && clearable(w, p, owner)) away.push_back(p);
        }
        for (ObjectId p : away) {
            releasePropLinks(w, p);
            w.destroy(p);
        }
        if (!away.empty()) check = checkBuildSpot(w, t, kind, x, z, 0, owner, &claims, yaw);
    }
    return check == BuildCheck::Ok ? placeCostedProp(w, t, kind, x, z, yaw, owner) : NO_OBJECT;
}

PropKind slopeKind() {
    PropKind out = PropKind::Wagon;
    for (int k = 0; k < propKindCount(); ++k) {
        if (propInfo((PropKind)k).slope) out = (PropKind)k;
    }
    return out;
}

int dirOfYawQuarter(float yaw) { return (((int)std::lround(yaw / 1.5707963f)) % 4 + 4) % 4; }

// A faction keeps a Slope up to the wall top at the east-most, west-most, north-most and south-most of its Perimeter Walls that
// can still be reached from inside: the Slope's top beside a tile of the wall's inner (back) row, rising toward the wall. If it has
// more than four Slopes, those that are not at one of those four places are pulled down.
void slopeUpkeep(World& w, Terrain& t, const std::vector<Claim>& claims, const Claim& c) {
    const PropKind slope = slopeKind();
    if (!propInfo(slope).slope) return;
    struct Wall {
        float x, z;
        int d;
    };
    std::vector<Wall> walls;
    struct Existing {
        ObjectId e;
        float x, z;
        int d;
    };
    std::vector<Existing> have;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (ownerOf(w, p) != c.owner) continue;
        const Prop* pp = w.get<Prop>(p);
        const PropInfo& info = propInfo(pp->kind);
        const Transform* tr = w.get<Transform>(p);
        if (info.perimeter && info.solid && !w.has<Blueprint>(p)) walls.push_back(Wall{ tr->x, tr->z, dirOfYawQuarter(pp->yaw) });
        else if (info.slope) have.push_back(Existing{ p, tr->x, tr->z, dirOfYawQuarter(pp->yaw) });
    }
    if (walls.empty()) return;
    std::vector<bool> keep(have.size(), false);
    auto existingAt = [&](float x, float z, int d) {
        for (size_t i = 0; i < have.size(); ++i) {
            if (have[i].d == d && std::abs(have[i].x - x) < 0.6f && std::abs(have[i].z - z) < 0.6f) {
                keep[i] = true;
                return true;
            }
        }
        return false;
    };
    for (int side = 0; side < 4; ++side) {
        std::vector<const Wall*> order;
        // The walls on that side of the claim's middle, nearest first to the point on the claim's edge in that direction: the
        // far-out ones that are also the most in line with it. Each is tried until a Slope fits (breakable props are cleared).
        const float tipX = c.x + (side == 0 ? c.radius : side == 1 ? -c.radius : 0.0f);
        const float tipZ = c.z + (side == 3 ? c.radius : side == 2 ? -c.radius : 0.0f);
        for (const Wall& wl : walls) {
            const bool onSide = side == 0 ? wl.x > c.x : side == 1 ? wl.x < c.x : side == 2 ? wl.z < c.z : wl.z > c.z;
            if (onSide) order.push_back(&wl);
        }
        std::sort(order.begin(), order.end(), [&](const Wall* a, const Wall* b) {
            return distSq(a->x, a->z, tipX, tipZ) < distSq(b->x, b->z, tipX, tipZ);
        });
        bool done = false;
        for (const Wall* wl : order) {
            if (done) break;
            const int fx = STAIRS_DX[wl->d], fz = STAIRS_DZ[wl->d];
            const int bx = (int)std::floor(wl->x - 0.5f), bz = (int)std::floor(wl->z - 0.5f);
            for (int a = 0; a < 2 && !done; ++a) {
                for (int b = 0; b < 2 && !done; ++b) {
                    const float relX = bx + a + 0.5f - wl->x, relZ = bz + b + 0.5f - wl->z;
                    if (relX * fx + relZ * fz >= 0.0f) continue;   // not on the wall's inner (back) row
                    const float x = (float)(bx + a - 2 * fx) + 0.5f, z = (float)(bz + b - 2 * fz) + 0.5f;   // middle of the three tiles
                    const float yaw = (float)wl->d * 1.5707963f;
                    if (existingAt(x, z, wl->d)) {
                        done = true;
                    } else if (const ObjectId e = placeClearing(w, t, claims, c.owner, slope, x, z, yaw); e != NO_OBJECT) {
                        if (takesMaterial(slope)) w.add(e, Material{ BuildMaterial::Wood });
                        done = true;
                    }
                }
            }
        }
    }
    if (have.size() > 4) {
        for (size_t i = 0; i < have.size(); ++i) {
            if (!keep[i] && w.has<Prop>(have[i].e)) destroyBuilding(w, have[i].e);
        }
    }
}

void perimeterHour(World& w, Terrain& t, const std::vector<Claim>& claims, double hours) {
    PerimeterDays& days = w.resource<PerimeterDays>();
    for (const Claim& c : claims) {
        if (c.camp || !factionCanClaim(c.owner) || isMobFaction(c.owner) || c.radius <= PERIMETER_MIN_RADIUS) continue;
        double& since = days.since[c.owner];
        since += hours;
        if (since < PERIMETER_LOOK_HOURS) continue;
        since = 0.0;
        const bool npc = c.owner != FACTION_PLAYER;

        // The faction's Perimeter pieces: built ones close tiles, and all of them (laid ones too) are in the way of more
        std::set<long long> closed, covered;
        int gates = 0, built = 0, laidBefore = 0;
        std::map<int, int> byMaterial;
        struct Piece {
            ObjectId e;
            bool gate, pending;
            std::vector<long long> tiles;
        };
        std::vector<Piece> pieces;
        for (ObjectId p : w.view<Prop, Transform>()) {
            const Prop* pp = w.get<Prop>(p);
            const PropInfo& info = propInfo(pp->kind);
            if (!info.perimeter || ownerOf(w, p) != c.owner) continue;
            const Transform* tr = w.get<Transform>(p);
            int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
            propTileRect(info, tr->x, tr->z, pp->yaw, x0, z0, x1, z1);
            const bool done = !w.has<Blueprint>(p);
            Piece piece{ p, !info.solid, !done, {} };
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    covered.insert(tileKey(x, z));
                    piece.tiles.push_back(tileKey(x, z));
                    if (done) closed.insert(tileKey(x, z));
                }
            }
            pieces.push_back(std::move(piece));
            if (done) {
                ++built;
                ++byMaterial[(int)materialOf(w, p)];
            } else {
                ++laidBefore;
            }
            if (!info.solid) ++gates;
        }
        if (built == 0 && !npc) continue;   // yours with no Perimeter at all: nothing to say

        // Whether anything can walk out of the claim from the Altar: walls and gates count as shut, as a gate can be
        std::unordered_set<long long> seen;
        std::queue<std::pair<int, int>> q;
        const int sx = (int)std::floor(c.x), sz = (int)std::floor(c.z);
        q.push({ sx, sz });
        seen.insert(tileKey(sx, sz));
        bool escaped = false;
        while (!q.empty() && !escaped) {
            const auto [x, z] = q.front();
            q.pop();
            const float dx = x + 0.5f - c.x, dz = z + 0.5f - c.z;
            if (dx * dx + dz * dz > c.radius * c.radius) {
                escaped = true;
                break;
            }
            const int step[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto& d : step) {
                const int nx = x + d[0], nz = z + d[1];
                const long long k = tileKey(nx, nz);
                if (seen.count(k) || closed.count(k) || !t.walkable(nx, nz) || t.blockedByBuilding(nx, nz)) continue;
                seen.insert(k);
                q.push({ nx, nz });
            }
        }

        // An NPC faction keeps its Perimeter to the ring its claim has now (nothing is remembered of how it was laid: the ring is
        // worked out again each look, so a claim that has grown gets its new ring), with a Gate, and clears what is in the way
        int laid = 0, missing = 0;
        if (npc) {
            // It starts once it has a foundation of its own down, and is not laid faster than it can be built
            bool housed = built > 0;
            for (ObjectId p : w.view<Prop, Foundation>()) {
                if (housed) break;
                housed = !w.has<Blueprint>(p) && ownerOf(w, p) == c.owner;
            }
            BuildMaterial material = BuildMaterial::Wood;
            int most = -1;
            for (const auto& [m, n] : byMaterial) {
                if (n > most) {
                    most = n;
                    material = (BuildMaterial)m;
                }
            }
            const PropKind wallKind = perimeterKind(true), gateKind = perimeterKind(false);
            // The ring in hand is finished first. Only when it is, and the claim has grown more than 8 beyond it, is a bigger one begun.
            float ringP = activeRing(covered, c.x, c.z, c.radius);
            std::vector<RingCell> ring = perimeterRing(ringP);
            float ax = 0.0f, az = 0.0f;
            snapBuildSpot(wallKind, c.x, c.z, ax, az);
            auto isCovered = [&](float x, float z) { return cellCovered(covered, x, z); };
            auto countMissing = [&]() {
                int n = 0;
                for (const RingCell& cell : ring) {
                    float x = 0.0f, z = 0.0f;
                    cellSpot(wallKind, ax, az, cell, x, z);
                    if (!isCovered(x, z)) ++n;
                }
                return n;
            };
            missing = countMissing();
            if (missing == 0 && c.radius - ringP > PERIMETER_EXPAND) {
                ringP = c.radius;
                ring = perimeterRing(ringP);
                missing = countMissing();
            }
            // Which pieces are on this ring, whether it has a Gate, and whether all its pieces are built
            std::unordered_set<long long> ringTiles;
            for (const RingCell& cell : ring) {
                float x = 0.0f, z = 0.0f;
                cellSpot(wallKind, ax, az, cell, x, z);
                const int bx = (int)std::floor(x - 0.5f), bz = (int)std::floor(z - 0.5f);
                for (int tz = bz; tz <= bz + 1; ++tz) {
                    for (int tx = bx; tx <= bx + 1; ++tx) ringTiles.insert(tileKey(tx, tz));
                }
            }
            int gatesIn = 0;
            bool unbuilt = false;
            std::vector<ObjectId> stale;
            for (const Piece& pc : pieces) {
                const bool on = std::any_of(pc.tiles.begin(), pc.tiles.end(), [&](long long k) { return ringTiles.count(k) > 0; });
                if (!on) stale.push_back(pc.e);
                else {
                    if (pc.gate) ++gatesIn;
                    if (pc.pending) unbuilt = true;
                }
            }
            auto place = [&](PropKind kind, float x, float z, float yaw) {
                BuildCheck check = checkBuildSpot(w, t, kind, x, z, 0, c.owner, &claims, yaw);
                if (check == BuildCheck::Blocked) {
                    // Props in the way that can be broken are cleared, and it is tried again
                    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
                    propTileRect(propInfo(kind), x, z, yaw, x0, z0, x1, z1);
                    std::vector<ObjectId> away;
                    for (ObjectId p : w.view<Prop, Transform>()) {
                        const Transform* pt = w.get<Transform>(p);
                        const int px = (int)std::floor(pt->x), pz = (int)std::floor(pt->z);
                        if (px >= x0 && px <= x1 && pz >= z0 && pz <= z1 && clearable(w, p, c.owner)) away.push_back(p);
                    }
                    for (ObjectId p : away) {
                        releasePropLinks(w, p);
                        w.destroy(p);
                    }
                    if (!away.empty()) check = checkBuildSpot(w, t, kind, x, z, 0, c.owner, &claims, yaw);
                }
                if (check != BuildCheck::Ok) return false;
                const ObjectId e = placeCostedProp(w, t, kind, x, z, yaw, c.owner);
                if (e == NO_OBJECT) return false;
                w.add(e, Material{ material });
                int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
                propTileRect(propInfo(kind), x, z, yaw, x0, z0, x1, z1);
                for (int tz = z0; tz <= z1; ++tz) {
                    for (int tx = x0; tx <= x1; ++tx) covered.insert(tileKey(tx, tz));
                }
                ++laid;
                return true;
            };
            if (housed && missing > 0 && laidBefore < PERIMETER_MOST_PENDING) {
                // The Gate first: two cells beside each other on a straight run facing the same way, nearest the middle of that side
                if (gatesIn == 0) {
                    const int faces[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
                    for (const auto& f : faces) {
                        if (gatesIn > 0) break;
                        std::vector<std::pair<int, std::pair<const RingCell*, const RingCell*>>> pairs;
                        for (const RingCell& a : ring) {
                            if (a.corner || a.dx != f[0] || a.dz != f[1]) continue;
                            for (const RingCell& b : ring) {
                                if (b.corner || b.dx != f[0] || b.dz != f[1]) continue;
                                const bool beside = f[0] != 0 ? (a.i == b.i && b.j == a.j + 1) : (a.j == b.j && b.i == a.i + 1);
                                if (beside) pairs.push_back({ std::abs(f[0] != 0 ? a.j : a.i), { &a, &b } });
                            }
                        }
                        std::sort(pairs.begin(), pairs.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
                        for (const auto& pr : pairs) {
                            float x1 = 0.0f, z1 = 0.0f, x2 = 0.0f, z2 = 0.0f, gx = 0.0f, gz = 0.0f;
                            cellSpot(wallKind, ax, az, *pr.second.first, x1, z1);
                            cellSpot(wallKind, ax, az, *pr.second.second, x2, z2);
                            snapBuildSpot(gateKind, (x1 + x2) * 0.5f, (z1 + z2) * 0.5f, gx, gz);
                            if (place(gateKind, gx, gz, facingYaw(f[0], f[1]))) {
                                ++gatesIn;
                                break;
                            }
                        }
                    }
                }
                for (const RingCell& cell : ring) {
                    if (laid >= PERIMETER_MOST_LAID || laidBefore + laid >= PERIMETER_MOST_PENDING) break;
                    float x = 0.0f, z = 0.0f;
                    cellSpot(wallKind, ax, az, cell, x, z);
                    if (isCovered(x, z)) continue;
                    place(wallKind, x, z, facingYaw(cell.dx, cell.dz));
                }
            }
            // What is still missing, after what was just laid
            missing = countMissing();
            // Once the ring is whole, built and has a Gate, and the Perimeter is secure, an older inner ring is taken down
            if (missing == 0 && !unbuilt && !escaped && gatesIn > 0) {
                for (ObjectId e : stale) {
                    if (!w.has<Prop>(e)) continue;
                    if (w.has<Blueprint>(e)) destroyBuilding(w, e);
                    else deconstructBuilding(w, e);
                }
            }
            slopeUpkeep(w, t, claims, c);
            if (!housed && built == 0) continue;   // nothing built yet: nothing said
        }

        // Said when it changes (and once, the first time), not every look
        const int state = !escaped && missing == 0 ? 1 : 2;
        int& before = days.said[c.owner];
        const std::string name = factionName(c.owner);
        if (laid > 0) {
            queueLog(w, langf("MISC_PERIMETER_MENDING", { name, std::to_string(laid) }), LogKind::Claim, LogWho(npc, c.altar));
            before = 2;
        } else if (before != state) {
            queueLog(w, langf(state == 1 ? "MISC_PERIMETER_SECURE" : "MISC_PERIMETER_EXPOSED", { name }), LogKind::Claim, LogWho(npc, c.altar));
            before = state;
        }
    }
}
}

bool npcNextMaterial(const NpcBuildCount& have, BuildMaterial& out) {
    if (have.total >= NPC_BUILDINGS_MOST) return false;
    if (have.wood < NPC_BUILDINGS_WOOD) {
        out = BuildMaterial::Wood;
        return true;
    }
    if (have.woodComplete < NPC_BUILDINGS_WOOD) return false;
    if (have.stone < NPC_BUILDINGS_STONE) {
        out = BuildMaterial::Stone;
        return true;
    }
    if (have.metal < NPC_BUILDINGS_METAL) {
        out = BuildMaterial::Iron;
        return true;
    }
    return false;
}

void npcBuilderSystem(World& w, Terrain& t, Rng& rng, float realSeconds, double gameSeconds) {
    BuilderClock& clock = w.resource<BuilderClock>();
    clock.timer += realSeconds;
    clock.hours += gameSeconds / 3600.0;
    if (clock.timer < NPC_BUILDERS_LOOK_SECONDS) return;
    const double hours = clock.hours;
    clock.timer = 0.0f;
    clock.hours = 0.0;
    const std::vector<Claim> claims = claimsOf(w);
    perimeterHour(w, t, claims, hours);
    npcRolesHour(w, hours);   // each NPC faction's share of citizens, military and Fortified (npc_roles.h)

    // The buildings under way, NPC factions' and yours (laid from the Build tab's Building Blueprint): when what is laid of one is
    // built, the ghosts of its next stage become blueprints. An NPC building that makes no progress is condemned, then pulled down.
    std::unordered_set<int> building;
    std::vector<SiteBox> boxes;
    // Auto Placed pieces of the world's blueprints, by faction, read once
    std::vector<ObjectId> autoPlaced;
    for (ObjectId b : w.view<Blueprint, Prop, Transform>()) {
        if (w.get<Blueprint>(b)->autoPlaced) autoPlaced.push_back(b);
    }
    for (ObjectId e : w.view<PlanSite, Prop>()) {
        const int faction = ownerOf(w, e);
        PlanSite* sitePtr = w.get<PlanSite>(e);
        if (!sitePtr) continue;
        PlanSite& site = *sitePtr;
        const BuildPlan* plan = findPlan(site.plan);
        if (!plan || faction < 0) {
            w.remove<PlanSite>(e);
            continue;
        }
        const BuildPlan turned = planTurned(*plan, site.turns);
        site.hours += hours;
        boxes.push_back(SiteBox{ faction, site.x, site.z, turned.w, turned.d });
        building.insert(faction);

        // Condemned: its units are asked to leave, and once the day is up and nobody is on its ground floor it comes down
        if (site.demolishHours >= 0.0) {
            site.demolishHours += hours;
            const Transform* at = w.get<Transform>(e);
            const BuildingMap map = buildingMap(w);
            const std::vector<long long> tiles = buildingTiles(map, (int)std::floor(at->x), (int)std::floor(at->z));
            const bool inside = evacuateBuilding(w, t, tiles);
            if (site.demolishHours >= NPC_DEMOLISH_HOURS && !inside) pullDownBuilding(w, t, tiles);
            continue;
        }

        // What is left of it to build: its Auto Placed pieces in its box, those laid and those still ghosts
        int remaining = 0, supplied = 0, lowestGhost = 1 << 20, ghostCount = 0;
        bool laid = false;
        for (ObjectId b : autoPlaced) {
            if (!w.has<Blueprint>(b)) continue;   // built, or cancelled, since the list was made
            const Transform* bt = w.get<Transform>(b);
            if (ownerOf(w, b) != faction || !inBox(site, turned, bt->x, bt->z)) continue;
            const Blueprint* bp = w.get<Blueprint>(b);
            ++remaining;
            if (bp->ghost) {
                ++ghostCount;
                lowestGhost = std::min(lowestGhost, bp->stage);
            } else {
                laid = true;
                supplied += bp->supplied;
            }
        }
        if (remaining == 0) {
            w.remove<PlanSite>(e);   // built, or what is left of it cancelled
            boxes.pop_back();
            continue;
        }
        // Everything before the next stage is built: its ghosts become blueprints, and what costs nothing is put down
        if (!laid && ghostCount > 0) {
            int made = 0;
            for (ObjectId b : autoPlaced) {
                if (!w.has<Blueprint>(b)) continue;
                Blueprint* bp = w.get<Blueprint>(b);
                const Transform* bt = w.get<Transform>(b);
                if (!bp->ghost || bp->stage != lowestGhost || ownerOf(w, b) != faction || !inBox(site, turned, bt->x, bt->z)) continue;
                if (!ghostReady(w, b, faction)) continue;
                bp->ghost = false;
                ++made;
            }
            if (made > 0 && lowestGhost > site.stage) {
                for (const PlanPiece* p : planStagePieces(turned, lowestGhost)) {
                    if (propInfo(p->kind).materials <= 0.0) placePlanPiece(w, t, *p, site.x, site.z, faction, (BuildMaterial)site.material);
                }
                site.stage = lowestGhost;
            }
            supplied += made * 7919;   // a ghost made a blueprint is progress
        }
        // Progress, or not: an NPC building that stands idle for NPC_STALL_HOURS is condemned. Yours never are.
        const int mark = remaining * 10007 + supplied;
        if (mark != site.mark) {
            site.mark = mark;
            site.idleHours = 0.0;
        } else {
            site.idleHours += hours;
        }
        if (faction != FACTION_PLAYER && site.idleHours >= NPC_STALL_HOURS) {
            const Transform* at = w.get<Transform>(e);
            const BuildingMap map = buildingMap(w);
            const std::vector<long long> tiles = buildingTiles(map, (int)std::floor(at->x), (int)std::floor(at->z));
            const std::unordered_set<long long> in(tiles.begin(), tiles.end());
            for (ObjectId p : w.view<Prop, Transform>()) {
                const Transform* pt = w.get<Transform>(p);
                if (in.count(tileKey((int)std::floor(pt->x), (int)std::floor(pt->z)))) w.add(p, Condemned{});
            }
            site.demolishHours = 0.0;
        }
    }
    claimBedSystem(w, t);
    // Ghosts that no building owns any more (the first tile of its foundation was cancelled) go
    for (ObjectId b : autoPlaced) {
        if (!w.has<Blueprint>(b) || !w.get<Blueprint>(b)->ghost) continue;
        const Transform* bt = w.get<Transform>(b);
        const int faction = ownerOf(w, b);
        const bool owned = std::any_of(boxes.begin(), boxes.end(), [&](const SiteBox& s) {
            return s.faction == faction && bt->x >= (float)s.x && bt->z >= (float)s.z && bt->x < (float)(s.x + s.w) && bt->z < (float)(s.z + s.d);
        });
        if (owned) continue;
        releasePropLinks(w, b);
        w.destroy(b);
    }

    // A faction that is not building looks for a house it could start
    BuildingMap map;
    bool mapped = false;
    std::vector<BuildingCluster> clusters;
    for (const Claim& c : claims) {
        if (!npcAltarClaim(c)) continue;
        giveBuilders(w, c.owner);
        giveHaulers(w, claims, c.owner);
        giveCitizenTasks(w, c.owner);
        if (building.count(c.owner)) continue;
        // Perimeter first: no new building is begun while its Perimeter is not whole (once it has a home to build it from)
        if (factionRules(w, c.owner).buildOrder == BuildOrder::PerimeterFirst && c.radius > PERIMETER_MIN_RADIUS &&
            w.resource<PerimeterDays>().said[c.owner] == 2) {
            continue;
        }
        if (!mapped) {
            map = buildingMap(w);
            clusters = buildingClusters(w, map);
            mapped = true;
        }
        NpcBuildCount have;
        for (const BuildingCluster& b : clusters) {
            if (b.owner != c.owner || distSq(b.centerX(), b.centerZ(), c.x, c.z) > c.radius * c.radius) continue;
            ++have.total;
            if (b.material == BuildMaterial::Stone) ++have.stone;
            else if (b.material == BuildMaterial::Iron) ++have.metal;
            else ++have.wood;
            if (b.material == BuildMaterial::Wood && b.complete) ++have.woodComplete;
        }
        BuildMaterial material = BuildMaterial::Wood;
        if (!npcNextMaterial(have, material)) continue;
        // Wood is what it can always try for. Stone and metal buildings wait until it has some of that in its stores, so that a
        // faction with none does not lay one it will never finish, have it condemned, and lay it again.
        if (material != BuildMaterial::Wood && stockOf(w, claims, c.owner, materialItem(material)) <= 0) continue;

        // Any plan it may build, one of them by chance: a free slot and room are all it needs to start, not the stock
        std::vector<const BuildPlan*> plans;
        for (const BuildPlan& plan : planLibrary()) {
            if (plan.npcAllowed) plans.push_back(&plan);   // the Load list's NPC toggle off: only you place it
        }
        if (plans.empty()) continue;
        const BuildPlan& plan = *plans[(size_t)rng.range(0, (int)plans.size() - 1)];
        const float ringNow = activeRing(perimeterCovered(w, c.owner), c.x, c.z, c.radius);
        const std::unordered_set<long long> reserve = perimeterReserve(c.x, c.z, ringNow, c.radius);
        const BuildPlan turnedBy[4] = { planTurned(plan, 0), planTurned(plan, 1), planTurned(plan, 2), planTurned(plan, 3) };
        const std::vector<PlanRect> rectsBy[4] = { planFoundationRects(turnedBy[0]), planFoundationRects(turnedBy[1]),
                                                   planFoundationRects(turnedBy[2]), planFoundationRects(turnedBy[3]) };
        for (int attempt = 0; attempt < NPC_PLACE_TRIES; ++attempt) {
            const int turns = rng.range(0, 3);
            const BuildPlan& turned = turnedBy[turns];
            const float angle = rng.unit() * 6.2831853f, radius = std::sqrt(rng.unit()) * c.radius;
            const int x = (int)std::floor(c.x + std::cos(angle) * radius - (float)turned.w * 0.5f);
            const int z = (int)std::floor(c.z + std::sin(angle) * radius - (float)turned.d * 0.5f);
            // All of its box inside the claim, and inside the ring its Perimeter will stand on (a claim of 10 or less has none)
            const float cx[2] = { (float)x, (float)(x + turned.w) }, cz[2] = { (float)z, (float)(z + turned.d) };
            const float room = c.radius > PERIMETER_MIN_RADIUS ? ringNow - 7.0f : c.radius;
            bool inside = true;
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) inside = inside && distSq(cx[i], cz[j], c.x, c.z) <= room * room;
            }
            if (!inside) continue;
            bool onRing = false;
            for (int tz = z; tz < z + turned.d && !onRing; ++tz) {
                for (int tx = x; tx < x + turned.w && !onRing; ++tx) onRing = reserve.count(tileKey(tx, tz)) > 0;
            }
            if (onRing) continue;
            // A tile clear all round it of any other foundation, so that it is a building of its own
            bool clear = true;
            for (int tz = z - 1; tz <= z + turned.d && clear; ++tz) {
                for (int tx = x - 1; tx <= x + turned.w && clear; ++tx) clear = !map.foundation.count(tileKey(tx, tz));
            }
            if (!clear || !planFoundationFits(w, t, rectsBy[turns], x, z, c.owner)) continue;
            const ObjectId anchor = placePlanAll(w, t, turned, rectsBy[turns], x, z, c.owner, material);
            if (anchor == NO_OBJECT) continue;
            PlanSite site;
            site.plan = plan.name;
            site.x = x;
            site.z = z;
            site.turns = turns;
            site.material = (int)material;
            w.add(anchor, site);
            break;
        }
    }
}
