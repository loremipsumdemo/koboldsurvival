#include "building.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_set>
#include "airship.h"
#include "encumbrance.h"
#include "experience.h"
#include "factions.h"
#include "hostility.h"
#include "inventory.h"
#include "items.h"
#include "systems_internal.h"
#include "world_labels.h"

using namespace systems_detail;

namespace {
int tileOf(float v) { return (int)std::floor(v); }
int keyX(long long k) { return (int)(k % 1000003LL); }
int keyZ(long long k) { return (int)(k / 1000003LL); }

const PropInfo* infoOf(World& w, ObjectId e) {
    const Prop* p = w.get<Prop>(e);
    return p ? &propInfo(p->kind) : nullptr;
}

int ownerOf(World& w, ObjectId prop) {
    const Owner* o = w.get<Owner>(prop);
    return o ? o->faction : -1;
}

// 1 at Carpentry 1, up to 2 at the top level
double carpentryPace(World& w, ObjectId u) {
    return 2.0 / workHoursFor(levelOf(statsOf(w, u), StatId::Carpentry));
}

// The floor a builder works a piece from: an upper floor is built from the floor under it
int workStorey(World& w, ObjectId prop) {
    const PropInfo* info = infoOf(w, prop);
    const int level = storeyOf(w, prop);
    return info && info->upperFloor ? std::max(0, level - 1) : level;
}

// How far u is from the piece's tile, or very far if it is on another floor than the piece is worked from
float gapTo(World& w, ObjectId u, ObjectId prop) {
    const Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    if (!ut || !pt) return 1e9f;
    const int su = storeyOf(w, u);
    const PropInfo* info = infoOf(w, prop);
    if (su != workStorey(w, prop) && !(info && info->upperFloor && su == storeyOf(w, prop))) return 1e9f;
    if (const Foundation* f = w.get<Foundation>(prop)) {
        const float cx = std::clamp(ut->x, (float)f->x0, (float)(f->x0 + f->w));
        const float cz = std::clamp(ut->z, (float)f->z0, (float)(f->z0 + f->h));
        return std::hypot(ut->x - cx, ut->z - cz);
    }
    return std::max(0.0f, std::hypot(ut->x - pt->x, ut->z - pt->z) - 0.5f);
}

// Where to walk to reach the piece
Vec2 approachOf(World& w, ObjectId u, ObjectId prop) {
    const Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    if (const Foundation* f = w.get<Foundation>(prop); f && ut) {
        return Vec2{ std::clamp(ut->x, (float)f->x0 + 0.5f, (float)(f->x0 + f->w) - 0.5f),
                     std::clamp(ut->z, (float)f->z0 + 0.5f, (float)(f->z0 + f->h) - 0.5f) };
    }
    return pt ? Vec2{ pt->x, pt->z } : Vec2{ 0.0f, 0.0f };
}

bool walking(World& w, ObjectId u) {
    if (w.has<Route>(u) || w.has<Climbing>(u)) return true;
    const SoloMove* so = w.get<SoloMove>(u);
    return so && so->next < so->waypoints.size();
}

BehaviorRule* ruleOf(Behaviors& b, BehaviorKind kind) {
    for (BehaviorRule& r : b.rules) {
        if (r.kind == kind) return &r;
    }
    return nullptr;
}

ObjectId mapGet(const std::unordered_map<long long, ObjectId>& m, long long key) {
    const auto it = m.find(key);
    return it == m.end() ? NO_OBJECT : it->second;
}

bool isWallOrDoor(World& w, ObjectId p) {
    const PropInfo* info = p != NO_OBJECT ? infoOf(w, p) : nullptr;
    return info && (info->wallRule || info->door);
}

// A finished floor of owner's on this tile and level: the foundation for level 0, else an upper floor or a stairs' top
bool mapFloorSurface(World& w, const BuildingMap& m, int x, int z, int level, int owner) {
    ObjectId f = level <= 0 ? mapGet(m.foundation, tileKey(x, z)) : mapGet(m.floor, levelKey(x, z, level));
    if (f == NO_OBJECT && level > 0) f = mapGet(m.stairTop, levelKey(x, z, level));
    return f != NO_OBJECT && !w.has<Blueprint>(f) && (owner < 0 || ownerOf(w, f) == owner);
}

// Whether a unit on this floor can stand on this tile: a finished floor with no finished wall or stairs' step on it
bool floorWalkable(World& w, const BuildingMap& m, int x, int z, int level) {
    if (!mapFloorSurface(w, m, x, z, level, -1)) return false;
    const ObjectId p = mapGet(m.piece, levelKey(x, z, level));
    if (p == NO_OBJECT || w.has<Blueprint>(p)) return true;
    const PropInfo* info = infoOf(w, p);
    return !(info && (info->wallRule || info->stairs));
}

bool anyFloorOn(World& w, const BuildingMap& m, const std::vector<long long>& tiles, int level, int owner) {
    for (long long k : tiles) {
        if (mapFloorSurface(w, m, keyX(k), keyZ(k), level, owner)) return true;
    }
    return false;
}

bool edgesClosedMap(World& w, const BuildingMap& m, const std::vector<long long>& tiles, int level) {
    if (tiles.empty()) return false;
    const std::unordered_set<long long> in(tiles.begin(), tiles.end());
    for (long long k : tiles) {
        const int x = keyX(k), z = keyZ(k);
        const bool edge = !in.count(tileKey(x + 1, z)) || !in.count(tileKey(x - 1, z)) || !in.count(tileKey(x, z + 1)) ||
                          !in.count(tileKey(x, z - 1));
        if (edge && !isWallOrDoor(w, mapGet(m.piece, levelKey(x, z, level)))) return false;
    }
    return true;
}

int dirOfYaw(float yaw) { return (((int)std::lround(yaw / 1.5707963f)) % 4 + 4) % 4; }

bool isStairs(World& w, ObjectId e) {
    const PropInfo* info = infoOf(w, e);
    return info && info->stairs;
}

// Stairs' tiles from the foot up (the middle one where the prop stands), and the tile past the top
void stairsTiles(World& w, ObjectId e, int xs[4], int zs[4]) {
    const Transform* tr = w.get<Transform>(e);
    const Prop* p = w.get<Prop>(e);
    const int d = p ? dirOfYaw(p->yaw) : 0;
    const int mx = tr ? tileOf(tr->x) : 0, mz = tr ? tileOf(tr->z) : 0;
    for (int i = 0; i < 4; ++i) {
        xs[i] = mx + (i - 1) * STAIRS_DX[d];
        zs[i] = mz + (i - 1) * STAIRS_DZ[d];
    }
}

// Whether a unit can stand on this tile of this floor, to step onto stairs from: on the ground, a foundation tile with
// no finished wall or stairs' step; up a floor, a floor it can walk
bool stairsFootOpen(World& w, const BuildingMap& m, int x, int z, int level) {
    if (level > 0) return floorWalkable(w, m, x, z, level);
    if (mapGet(m.foundation, tileKey(x, z)) == NO_OBJECT) return false;
    const ObjectId p = mapGet(m.piece, levelKey(x, z, 0));
    const PropInfo* info = p != NO_OBJECT && !w.has<Blueprint>(p) ? infoOf(w, p) : nullptr;
    return !(info && (info->wallRule || info->stairs));
}

// Where units get on and off stairs on the floor they stand on: the middles of the open floor tiles beside their bottom
// step (in front of it first, then its sides)
std::vector<Vec2> stairsLandings(World& w, const BuildingMap& m, ObjectId e) {
    int xs[4], zs[4];
    stairsTiles(w, e, xs, zs);
    const int d = dirOfYaw(w.get<Prop>(e) ? w.get<Prop>(e)->yaw : 0.0f), lv = storeyOf(w, e);
    const int dx = STAIRS_DX[d], dz = STAIRS_DZ[d];
    const int side[3][2] = { { -dx, -dz }, { dz, -dx }, { -dz, dx } };
    std::vector<Vec2> out;
    for (const auto& s : side) {
        const int x = xs[0] + s[0], z = zs[0] + s[1];
        if (stairsFootOpen(w, m, x, z, lv)) out.push_back(Vec2{ x + 0.5f, z + 0.5f });
    }
    return out;
}

// The landing nearest to a spot, or the tile in front of the bottom step if none is open
Vec2 nearestLanding(World& w, const BuildingMap& m, ObjectId e, Vec2 near) {
    const std::vector<Vec2> spots = stairsLandings(w, m, e);
    if (spots.empty()) {
        int xs[4], zs[4];
        stairsTiles(w, e, xs, zs);
        return Vec2{ 2.0f * xs[0] - xs[1] + 0.5f, 2.0f * zs[0] - zs[1] + 0.5f };
    }
    Vec2 best = spots.front();
    for (const Vec2& s : spots) {
        if (std::hypot(s.x - near.x, s.z - near.z) < std::hypot(best.x - near.x, best.z - near.z)) best = s;
    }
    return best;
}

// Where a unit coming up a ladder steps off: the nearest floor tile beside it on the floor above. Stairs: their top
// step, which is a tile of the floor above; they can't be used with no open tile beside their bottom step.
bool ladderTop(World& w, const BuildingMap& m, ObjectId ladder, Vec2& out) {
    const Transform* lt = w.get<Transform>(ladder);
    if (!lt) return false;
    if (isStairs(w, ladder)) {
        int xs[4], zs[4];
        stairsTiles(w, ladder, xs, zs);
        if (stairsLandings(w, m, ladder).empty()) return false;
        out = Vec2{ xs[2] + 0.5f, zs[2] + 0.5f };
        return true;
    }
    const int x = tileOf(lt->x), z = tileOf(lt->z), up = storeyOf(w, ladder) + 1;
    float best = -1.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx == 0 && dz == 0) || !floorWalkable(w, m, x + dx, z + dz, up)) continue;
            const float d = (float)(dx * dx + dz * dz);
            if (best < 0.0f || d < best) {
                best = d;
                out = Vec2{ x + dx + 0.5f, z + dz + 0.5f };
            }
        }
    }
    return best >= 0.0f;
}

// A walk on one upper floor, tile to tile: empty if the goal can't be reached
std::vector<Vec2> floorPath(World& w, const BuildingMap& m, int level, Vec2 from, Vec2 to) {
    const int sx = tileOf(from.x), sz = tileOf(from.z), gx = tileOf(to.x), gz = tileOf(to.z);
    if (!floorWalkable(w, m, gx, gz, level)) return {};
    if (sx == gx && sz == gz) return { to };
    std::unordered_map<long long, long long> parent;
    std::deque<long long> open;
    const long long start = tileKey(sx, sz), goal = tileKey(gx, gz);
    parent[start] = start;
    open.push_back(start);
    bool found = false;
    while (!open.empty() && parent.size() < 8192) {
        const long long k = open.front();
        open.pop_front();
        if (k == goal) {
            found = true;
            break;
        }
        const int x = keyX(k), z = keyZ(k);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const int nx = x + dx, nz = z + dz;
                if (!floorWalkable(w, m, nx, nz, level)) continue;
                if (dx != 0 && dz != 0 && (!floorWalkable(w, m, x + dx, z, level) || !floorWalkable(w, m, x, z + dz, level))) continue;
                const long long nk = tileKey(nx, nz);
                if (parent.count(nk)) continue;
                parent[nk] = k;
                open.push_back(nk);
            }
        }
    }
    if (!found) return {};
    std::vector<Vec2> tiles;
    for (long long k = goal; k != start; k = parent[k]) tiles.push_back(Vec2{ keyX(k) + 0.5f, keyZ(k) + 0.5f });
    std::reverse(tiles.begin(), tiles.end());
    // Keep only the turns, and end on the goal itself
    std::vector<Vec2> out;
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (i + 1 < tiles.size() && i > 0) {
            const Vec2 a = tiles[i - 1], b = tiles[i], c = tiles[i + 1];
            if (std::fabs((b.x - a.x) - (c.x - b.x)) < 1e-3f && std::fabs((b.z - a.z) - (c.z - b.z)) < 1e-3f) continue;
        }
        out.push_back(tiles[i]);
    }
    if (!out.empty()) out.back() = to;
    return out;
}

// Which part of a floor a tile is in: tiles that can be walked between share a number
std::unordered_map<long long, int> floorParts(World& w, const BuildingMap& m, int level) {
    std::unordered_map<long long, int> part;
    int next = 1;
    std::vector<long long> seeds;
    for (const auto& [lk, e] : m.floor) seeds.push_back(lk);
    for (const auto& [lk, e] : m.stairTop) seeds.push_back(lk);
    for (const long long lk : seeds) {
        if ((int)(lk % 16) != level) continue;
        const long long tk = lk / 16;
        if (part.count(tk) || !floorWalkable(w, m, keyX(tk), keyZ(tk), level)) continue;
        std::deque<long long> open{ tk };
        part[tk] = next;
        while (!open.empty()) {
            const long long k = open.front();
            open.pop_front();
            const int x = keyX(k), z = keyZ(k);
            const int dxs[4] = { 1, -1, 0, 0 }, dzs[4] = { 0, 0, 1, -1 };
            for (int i = 0; i < 4; ++i) {
                const long long nk = tileKey(x + dxs[i], z + dzs[i]);
                if (part.count(nk) || !floorWalkable(w, m, x + dxs[i], z + dzs[i], level)) continue;
                part[nk] = next;
                open.push_back(nk);
            }
        }
        ++next;
    }
    return part;
}

bool planRouteTo(World& w, const Terrain& t, ObjectId u, Vec2 goal, int goalStorey, Route& out);

// Plans a walk from the unit to goal on goalStorey by way of ladders. False if there is no way. A goal on an upper floor
// that can't be reached, as a corner tile walled in, is swapped for the nearest tile that can, near enough to work from.
bool planRoute(World& w, const Terrain& t, ObjectId u, Vec2 goal, int goalStorey, Route& out) {
    if (planRouteTo(w, t, u, goal, goalStorey, out)) return true;
    if (goalStorey <= 0) return false;
    const BuildingMap m = buildingMap(w);
    const std::unordered_map<long long, int> parts = floorParts(w, m, goalStorey);
    // The parts of this floor a ladder comes to
    std::unordered_set<int> reached;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (!info.ladder || w.has<Blueprint>(e)) continue;
        const int level = storeyOf(w, e);
        std::vector<Vec2> spots = isStairs(w, e) ? stairsLandings(w, m, e) : std::vector<Vec2>{ climbFoot(w, e) };
        if (level == goalStorey - 1) {
            Vec2 top{ 0.0f, 0.0f };
            if (!ladderTop(w, m, e, top)) continue;
            spots = { top };
        } else if (level != goalStorey) {
            continue;
        }
        for (const Vec2& spot : spots) {
            const auto it = parts.find(tileKey(tileOf(spot.x), tileOf(spot.z)));
            if (it != parts.end()) reached.insert(it->second);
        }
    }
    if (const Upstairs* up = w.get<Upstairs>(u); up && up->level == goalStorey) {
        const Transform* ut = w.get<Transform>(u);
        const auto it = parts.find(tileKey(tileOf(ut->x), tileOf(ut->z)));
        if (it != parts.end()) reached.insert(it->second);
    }
    Vec2 best = goal;
    float bestD2 = -1.0f;
    for (const auto& [tk, part] : parts) {
        if (!reached.count(part)) continue;
        const float x = keyX(tk) + 0.5f, z = keyZ(tk) + 0.5f;
        const float d2 = (x - goal.x) * (x - goal.x) + (z - goal.z) * (z - goal.z);
        if (bestD2 < 0.0f || d2 < bestD2) {
            bestD2 = d2;
            best = Vec2{ x, z };
        }
    }
    if (bestD2 < 0.0f || bestD2 < 1e-4f) return false;
    return planRouteTo(w, t, u, best, goalStorey, out);
}

bool planRouteTo(World& w, const Terrain& t, ObjectId u, Vec2 goal, int goalStorey, Route& out) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    const BuildingMap m = buildingMap(w);
    struct Node {
        int level;
        Vec2 p;
        ObjectId ladder;
    };
    // 0 the start, 1 the goal, then each usable ladder's foot and head, side by side. Stairs have a pair for each open
    // tile beside their bottom step, so a walk can come at them from any side; flights on floor after floor chain up.
    std::vector<Node> nodes = { { storeyOf(w, u), Vec2{ ut->x, ut->z }, NO_OBJECT }, { goalStorey, goal, NO_OBJECT } };
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (!info.ladder || w.has<Blueprint>(e)) continue;
        Vec2 top;
        if (!ladderTop(w, m, e, top)) continue;
        const int level = storeyOf(w, e);
        const std::vector<Vec2> feet = info.stairs ? stairsLandings(w, m, e) : std::vector<Vec2>{ climbFoot(w, e) };
        for (const Vec2& foot : feet) {
            nodes.push_back({ level, foot, e });
            nodes.push_back({ level + 1, top, e });
        }
    }
    std::unordered_map<int, std::unordered_map<long long, int>> parts;
    auto partOf = [&](const Node& n) -> long long {
        const int x = tileOf(n.p.x), z = tileOf(n.p.z);
        if (n.level == 0) return t.areaAt(x, z);
        auto it = parts.find(n.level);
        if (it == parts.end()) it = parts.emplace(n.level, floorParts(w, m, n.level)).first;
        const auto p = it->second.find(tileKey(x, z));
        return p == it->second.end() ? 0 : p->second;
    };
    const size_t n = nodes.size();
    std::vector<long long> part(n);
    for (size_t i = 0; i < n; ++i) part[i] = partOf(nodes[i]);
    auto climbPair = [&](size_t a, size_t b) { return a >= 2 && b >= 2 && a / 2 == b / 2 && a != b; };
    auto linked = [&](size_t a, size_t b, float& cost) {
        if (climbPair(a, b)) {
            cost = isStairs(w, nodes[a].ladder) ? 3.0f : CLIMB_SECONDS * 2.0f;
            return true;
        }
        if (nodes[a].level != nodes[b].level) return false;
        const bool sameTile = tileOf(nodes[a].p.x) == tileOf(nodes[b].p.x) && tileOf(nodes[a].p.z) == tileOf(nodes[b].p.z);
        if (!sameTile && (part[a] == 0 || part[a] != part[b])) return false;
        cost = std::hypot(nodes[a].p.x - nodes[b].p.x, nodes[a].p.z - nodes[b].p.z);
        return true;
    };
    std::vector<float> dist(n, 1e30f);
    std::vector<int> prev(n, -1);
    std::vector<char> done(n, 0);
    dist[0] = 0.0f;
    for (size_t round = 0; round < n; ++round) {
        size_t best = n;
        for (size_t i = 0; i < n; ++i) {
            if (!done[i] && dist[i] < 1e29f && (best == n || dist[i] < dist[best])) best = i;
        }
        if (best == n || best == 1) break;
        done[best] = 1;
        for (size_t j = 0; j < n; ++j) {
            float cost = 0.0f;
            if (done[j] || !linked(best, j, cost)) continue;
            if (dist[best] + cost < dist[j]) {
                dist[j] = dist[best] + cost;
                prev[j] = (int)best;
            }
        }
    }
    if (prev[1] < 0) return false;
    std::vector<size_t> path;
    for (int k = 1; k >= 0; k = prev[(size_t)k]) path.push_back((size_t)k);
    std::reverse(path.begin(), path.end());
    out = Route{};
    out.goal = goal;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Node& a = nodes[path[i]];
        const Node& b = nodes[path[i + 1]];
        if (climbPair(path[i], path[i + 1])) {
            if (out.legs.empty() || out.legs.back().ladder != NO_OBJECT) out.legs.push_back(RouteLeg{ a.level, a.p, NO_OBJECT });
            out.legs.back().ladder = a.ladder;
            out.legs.back().off = b.p;
        } else {
            out.legs.push_back(RouteLeg{ a.level, b.p, NO_OBJECT });
        }
    }
    return !out.legs.empty();
}
}

const char* pieceRefusalKey(PieceCheck c) {
    switch (c) {
    case PieceCheck::TooSmall: return "MISC_FOUNDATION_TOO_SMALL";
    case PieceCheck::TooBig: return "MISC_FOUNDATION_TOO_BIG";
    case PieceCheck::Unwalkable: return "MISC_BUILD_UNWALKABLE";
    case PieceCheck::Blocked: return "MISC_BUILD_BLOCKED";
    case PieceCheck::OutsideClaim: return "MISC_BUILD_OUTSIDE_CLAIM";
    case PieceCheck::NeedsFoundation: return "MISC_BUILD_NEEDS_FOUNDATION";
    case PieceCheck::FoundationUnfinished: return "MISC_BUILD_FOUNDATION_UNFINISHED";
    case PieceCheck::Taken: return "MISC_BUILD_TILE_TAKEN";
    case PieceCheck::EdgesOpen: return "MISC_BUILD_EDGES_OPEN";
    case PieceCheck::NeedsFloor: return "MISC_BUILD_NEEDS_FLOOR";
    case PieceCheck::TooSteep: return "MISC_FOUNDATION_TOO_STEEP";
    case PieceCheck::Ok: break;
    }
    return "";
}

int woodLogItem() {
    static const int item = itemTagged(ITEM_TAG_WOOD_LOG);
    return item;
}

bool takesMaterial(PropKind kind) {
    const PropInfo& i = propInfo(kind);
    return i.foundation || i.wallRule || i.door || i.upperFloor || i.ladder || i.stairs;
}

BuildMaterial materialOf(World& w, ObjectId prop) {
    const Material* m = w.get<Material>(prop);
    return m ? m->kind : BuildMaterial::Wood;
}

int materialItem(BuildMaterial m) {
    static const int iron = itemTagged(ITEM_TAG_IRON_INGOT), crystal = itemTagged(ITEM_TAG_DRAGON_CRYSTAL);
    static const int stone = itemTagged(ITEM_TAG_STONE);
    switch (m) {
    case BuildMaterial::Stone: return stone;
    case BuildMaterial::Iron: return iron;
    case BuildMaterial::Crystal: return crystal;
    default: return woodLogItem();
    }
}

double materialToughness(BuildMaterial m) {
    switch (m) {
    case BuildMaterial::Stone: return 1.5;
    case BuildMaterial::Iron: return 2.0;
    case BuildMaterial::Crystal: return 3.0;
    default: return 1.0;
    }
}

const char* materialNameKey(BuildMaterial m) {
    switch (m) {
    case BuildMaterial::Stone: return "UILABEL_MATERIAL_STONE";
    case BuildMaterial::Iron: return "UILABEL_MATERIAL_IRON";
    case BuildMaterial::Crystal: return "UILABEL_MATERIAL_CRYSTAL";
    default: return "UILABEL_MATERIAL_WOOD";
    }
}

bool isBuildingPiece(PropKind kind) {
    const PropInfo& i = propInfo(kind);
    return i.foundation || i.onFoundation;
}

// One unit of material per tile: a foundation tile or an upper floor tile takes one
int materialsNeeded(World& w, ObjectId prop) {
    const PropInfo* info = infoOf(w, prop);
    if (!info || info->materials <= 0.0) return 0;
    double tiles = 1.0;
    if (const Foundation* f = w.get<Foundation>(prop)) tiles = (double)f->w * (double)f->h;
    return std::max(1, (int)std::ceil(info->materials * tiles - 1e-9));
}

double buildSecondsNeeded(World& w, ObjectId prop) {
    const PropInfo* info = infoOf(w, prop);
    if (!info) return 1.0;
    double tiles = 1.0;
    if (const Foundation* f = w.get<Foundation>(prop)) tiles = (double)f->w * (double)f->h;
    return std::max(0.5 / BUILD_SPEED, info->buildSeconds * tiles / BUILD_SPEED);
}

bool isFinished(World& w, ObjectId prop) { return w.has<Prop>(prop) && !w.has<Blueprint>(prop); }

void pieceTiles(World& w, ObjectId prop, int& x0, int& z0, int& x1, int& z1) {
    if (const Foundation* f = w.get<Foundation>(prop)) {
        x0 = f->x0;
        z0 = f->z0;
        x1 = f->x0 + f->w - 1;
        z1 = f->z0 + f->h - 1;
        return;
    }
    const Transform* tr = w.get<Transform>(prop);
    x0 = x1 = tr ? tileOf(tr->x) : 0;
    z0 = z1 = tr ? tileOf(tr->z) : 0;
    if (tr && isStairs(w, prop)) {
        int xs[4], zs[4];
        stairsTiles(w, prop, xs, zs);
        x0 = std::min(xs[0], xs[2]);
        x1 = std::max(xs[0], xs[2]);
        z0 = std::min(zs[0], zs[2]);
        z1 = std::max(zs[0], zs[2]);
    }
}

int stairsDir(World& w, ObjectId stairs) {
    const Prop* p = w.get<Prop>(stairs);
    return p ? dirOfYaw(p->yaw) : 0;
}

BuildingMap buildingMap(World& w) {
    BuildingMap m;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (const Foundation* f = w.get<Foundation>(e)) {
            for (int z = f->z0; z < f->z0 + f->h; ++z) {
                for (int x = f->x0; x < f->x0 + f->w; ++x) m.foundation[tileKey(x, z)] = e;
            }
            continue;
        }
        if (!info.onFoundation) continue;
        const Transform* tr = w.get<Transform>(e);
        const long long k = levelKey(tileOf(tr->x), tileOf(tr->z), storeyOf(w, e));
        if (info.stairs) {
            int xs[4], zs[4];
            stairsTiles(w, e, xs, zs);
            for (int i = 0; i < 3; ++i) m.piece[levelKey(xs[i], zs[i], storeyOf(w, e))] = e;
            m.stairTop[levelKey(xs[2], zs[2], storeyOf(w, e) + 1)] = e;
            continue;
        }
        if (info.upperFloor) m.floor[k] = e;
        else m.piece[k] = e;
    }
    return m;
}

bool floorSurfaceOn(World& w, const BuildingMap& m, int x, int z, int level, int owner) {
    return mapFloorSurface(w, m, x, z, level, owner);
}

int stairsStepAt(World& w, const BuildingMap& m, int x, int z, int level, ObjectId& stairs) {
    stairs = mapGet(m.piece, levelKey(x, z, level));
    if (stairs == NO_OBJECT || !isStairs(w, stairs)) {
        stairs = NO_OBJECT;
        return -1;
    }
    int xs[4], zs[4];
    stairsTiles(w, stairs, xs, zs);
    for (int i = 0; i < 3; ++i) {
        if (xs[i] == x && zs[i] == z) return i;
    }
    stairs = NO_OBJECT;
    return -1;
}

ObjectId foundationAt(World& w, int x, int z) {
    for (ObjectId e : w.view<Foundation>()) {
        if (w.get<Foundation>(e)->contains(x, z)) return e;
    }
    return NO_OBJECT;
}

ObjectId groundPieceAt(World& w, int x, int z, int level) {
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (!info.onFoundation || info.upperFloor) continue;
        const Transform* tr = w.get<Transform>(e);
        if (tileOf(tr->x) == x && tileOf(tr->z) == z && storeyOf(w, e) == level) return e;
    }
    return NO_OBJECT;
}

ObjectId upperFloorAt(World& w, int x, int z, int level) {
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (!propInfo(w.get<Prop>(e)->kind).upperFloor || w.has<Blueprint>(e)) continue;
        const Transform* tr = w.get<Transform>(e);
        if (tileOf(tr->x) == x && tileOf(tr->z) == z && storeyOf(w, e) == level) return e;
    }
    return NO_OBJECT;
}

bool floorSurfaceAt(World& w, int x, int z, int level, int owner) {
    const ObjectId f = level <= 0 ? foundationAt(w, x, z) : upperFloorAt(w, x, z, level);
    return f != NO_OBJECT && !w.has<Blueprint>(f) && (owner < 0 || ownerOf(w, f) == owner);
}

std::vector<long long> buildingTiles(const BuildingMap& map, int x, int z) {
    std::vector<long long> out;
    const long long start = tileKey(x, z);
    if (!map.foundation.count(start)) return out;
    std::unordered_set<long long> seen{ start };
    std::deque<long long> open{ start };
    while (!open.empty()) {
        const long long k = open.front();
        open.pop_front();
        out.push_back(k);
        const int kx = keyX(k), kz = keyZ(k);
        const long long next[4] = { tileKey(kx + 1, kz), tileKey(kx - 1, kz), tileKey(kx, kz + 1), tileKey(kx, kz - 1) };
        for (long long nk : next) {
            if (seen.count(nk) || !map.foundation.count(nk)) continue;
            seen.insert(nk);
            open.push_back(nk);
        }
    }
    return out;
}

PieceCheck checkFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner) {
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    const int fw = x1 - x0 + 1, fh = z1 - z0 + 1;
    if (fw > FOUNDATION_MAX || fh > FOUNDATION_MAX) return PieceCheck::TooBig;
    const BuildingMap m = buildingMap(w);
    // Props in the way, on the ground
    std::unordered_set<long long> propTiles;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (info.airship || info.foundation || storeyOf(w, e) != 0) continue;
        const Transform* tr = w.get<Transform>(e);
        propTiles.insert(tileKey(tileOf(tr->x), tileOf(tr->z)));
    }
    const std::vector<Claim> claims = claimsOf(w);
    bool touches = false;
    int fresh = 0;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (const ObjectId f = mapGet(m.foundation, tileKey(x, z)); f != NO_OBJECT) {
                if (ownerOf(w, f) != owner) return PieceCheck::Blocked;
                touches = true;
                continue;
            }
            ++fresh;
            const long long side[4] = { tileKey(x + 1, z), tileKey(x - 1, z), tileKey(x, z + 1), tileKey(x, z - 1) };
            for (long long k : side) {
                const ObjectId f = mapGet(m.foundation, k);
                if (f != NO_OBJECT && ownerOf(w, f) == owner) touches = true;
            }
            if (!t.walkable(x, z)) return PieceCheck::Unwalkable;
            if (propTiles.count(tileKey(x, z))) return PieceCheck::Blocked;
            const Claim* c = claimAt(claims, x + 0.5f, z + 0.5f);
            if (!c || c->owner != owner) return PieceCheck::OutsideClaim;
        }
    }
    if (fresh == 0) return PieceCheck::Taken;
    // A new building starts at least 3 by 3; from there it can be extended by any shape that touches it
    if (!touches && (fw < FOUNDATION_MIN || fh < FOUNDATION_MIN)) return PieceCheck::TooSmall;
    // The ground must be flat enough to level: the rectangle's corners and the buildings it joins (their level where
    // finished, else their ground) all within the spread
    float lo = 1e30f, hi = -1e30f;
    auto take = [&](float h) { lo = std::min(lo, h); hi = std::max(hi, h); };
    for (int z = z0; z <= z1 + 1; ++z) {
        for (int x = x0; x <= x1 + 1; ++x) take(t.heightAt((float)x, (float)z));
    }
    if (touches) {
        std::unordered_set<long long> seen;
        for (int z = z0 - 1; z <= z1 + 1; ++z) {
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                const long long k = tileKey(x, z);
                const ObjectId f = mapGet(m.foundation, k);
                if (f == NO_OBJECT || ownerOf(w, f) != owner || seen.count(k)) continue;
                for (long long bk : buildingTiles(m, x, z)) {
                    if (!seen.insert(bk).second) continue;
                    const int bx = keyX(bk), bz = keyZ(bk);
                    if (!w.has<Blueprint>(mapGet(m.foundation, bk))) {
                        take(t.heightAt(bx + 0.5f, bz + 0.5f));
                        continue;
                    }
                    for (int cz = bz; cz <= bz + 1; ++cz) {
                        for (int cx = bx; cx <= bx + 1; ++cx) take(t.heightAt((float)cx, (float)cz));
                    }
                }
            }
        }
    }
    if (hi - lo > FOUNDATION_LEVEL_SPREAD) return PieceCheck::TooSteep;
    return PieceCheck::Ok;
}

void editGroundCorner(World& w, Terrain& t, int x, int z, float h) {
    w.resource<GroundEdits>().corners[Terrain::cornerKey(x, z)] = h;
    t.setCornerHeight(x, z, h);
}

void levelFoundationGround(World& w, Terrain& t, ObjectId foundation) {
    const Foundation* fd = w.get<Foundation>(foundation);
    if (!fd) return;
    const BuildingMap m = buildingMap(w);
    const std::vector<long long> tiles = buildingTiles(m, fd->x0, fd->z0);
    if (tiles.empty()) return;
    // The level: a finished tile's, else the average of the building's ground at its corners
    bool haveLevel = false;
    float level = 0.0f;
    for (long long k : tiles) {
        const ObjectId f = mapGet(m.foundation, k);
        if (f == foundation || w.has<Blueprint>(f)) continue;
        level = t.heightAt(keyX(k) + 0.5f, keyZ(k) + 0.5f);
        haveLevel = true;
        break;
    }
    // The corners to set: those of the unfinished tiles, but not one a finished foundation tile (of any building) has,
    // which stays as it is
    std::unordered_set<long long> corners;
    for (long long k : tiles) {
        if (!w.has<Blueprint>(mapGet(m.foundation, k))) continue;
        const int x = keyX(k), z = keyZ(k);
        for (int cz = z; cz <= z + 1; ++cz) {
            for (int cx = x; cx <= x + 1; ++cx) corners.insert(Terrain::cornerKey(cx, cz));
        }
    }
    for (auto it = corners.begin(); it != corners.end();) {
        const int cx = keyX(*it), cz = keyZ(*it);
        bool finishedHasIt = false;
        for (int dz = -1; dz <= 0 && !finishedHasIt; ++dz) {
            for (int dx = -1; dx <= 0 && !finishedHasIt; ++dx) {
                const ObjectId f = mapGet(m.foundation, tileKey(cx + dx, cz + dz));
                finishedHasIt = f != NO_OBJECT && f != foundation && !w.has<Blueprint>(f);
            }
        }
        it = finishedHasIt ? corners.erase(it) : std::next(it);
    }
    if (corners.empty()) return;
    if (!haveLevel) {
        double sum = 0.0;
        for (long long c : corners) sum += t.heightAt((float)keyX(c), (float)keyZ(c));
        level = (float)(sum / (double)corners.size());
    }
    // What stands on or beside the reshaped ground, and the ground's height under it before
    int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
    for (long long c : corners) {
        x0 = std::min(x0, keyX(c) - 1); x1 = std::max(x1, keyX(c));
        z0 = std::min(z0, keyZ(c) - 1); z1 = std::max(z1, keyZ(c));
    }
    std::vector<std::pair<ObjectId, float>> standing;
    for (ObjectId e : w.view<Transform>()) {
        if (w.has<Carried>(e)) continue;
        const Transform* tr = w.get<Transform>(e);
        if (tr->x < x0 || tr->x >= x1 + 1 || tr->z < z0 || tr->z >= z1 + 1) continue;
        standing.push_back({ e, t.heightAt(tr->x, tr->z) });
    }
    for (long long c : corners) editGroundCorner(w, t, keyX(c), keyZ(c), level);
    for (const auto& [e, before] : standing) {
        Transform* tr = w.get<Transform>(e);
        tr->y += t.heightAt(tr->x, tr->z) - before;
    }
    buildingTilesChanged(w, x0, z0, x1, z1);
}

ObjectId placeFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner, BuildMaterial material) {
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    const BuildingMap m = buildingMap(w);
    ObjectId first = NO_OBJECT;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (m.foundation.count(tileKey(x, z))) continue;
            const ObjectId e = placeBuiltProp(w, t, PropKind::Foundation, x + 0.5f, z + 0.5f, 0.0f, owner);
            w.add(e, Foundation{ x, z, 1, 1, {} });
            w.add(e, Blueprint{});
            if (material != BuildMaterial::Wood) w.add(e, Material{ material });
            if (first == NO_OBJECT) first = e;
        }
    }
    return first;
}

PieceCheck checkPiece(World& w, PropKind kind, int x, int z, int owner, int level, int dir) {
    const PropInfo& info = propInfo(kind);
    const BuildingMap m = buildingMap(w);
    if (info.stairs) {
        // Three tiles of this floor in a row, with nothing on them and no floor over them, in one building of yours. The
        // bottom step is stepped onto from the floor beside it; the top step is a tile of the floor above.
        if (level < 0 || level + 1 > STOREY_MAX) return PieceCheck::NeedsFloor;
        dir = ((dir % 4) + 4) % 4;
        const ObjectId mid = mapGet(m.foundation, tileKey(x, z));
        if (mid == NO_OBJECT || ownerOf(w, mid) != owner) return PieceCheck::NeedsFoundation;
        const std::vector<long long> tiles = buildingTiles(m, x, z);
        const std::unordered_set<long long> in(tiles.begin(), tiles.end());
        for (int i = -1; i <= 1; ++i) {
            const int tx = x + i * STAIRS_DX[dir], tz = z + i * STAIRS_DZ[dir];
            const ObjectId f = mapGet(m.foundation, tileKey(tx, tz));
            if (f == NO_OBJECT || !in.count(tileKey(tx, tz))) return PieceCheck::NeedsFoundation;
            if (w.has<Blueprint>(f)) return PieceCheck::FoundationUnfinished;
            if (level > 0 && !mapFloorSurface(w, m, tx, tz, level, owner)) return PieceCheck::NeedsFloor;
            if (mapGet(m.piece, levelKey(tx, tz, level)) != NO_OBJECT) return PieceCheck::Taken;
            if (mapGet(m.floor, levelKey(tx, tz, level + 1)) != NO_OBJECT) return PieceCheck::Taken;
            if (mapGet(m.stairTop, levelKey(tx, tz, level)) != NO_OBJECT) return PieceCheck::Taken;
        }
        return PieceCheck::Ok;
    }
    const ObjectId f = mapGet(m.foundation, tileKey(x, z));
    if (f == NO_OBJECT || ownerOf(w, f) != owner) return PieceCheck::NeedsFoundation;
    if (w.has<Blueprint>(f)) return PieceCheck::FoundationUnfinished;
    if (level < 0 || level > STOREY_MAX) return PieceCheck::NeedsFloor;
    if (info.upperFloor) {
        // A floor over this one, on the level above
        if (level + 1 > STOREY_MAX) return PieceCheck::NeedsFloor;
        // It may span a hole in this floor, as over a ladder from the floor below: the walls round it hold it up
        if (level > 0 && !mapFloorSurface(w, m, x, z, level, owner) && !anyFloorOn(w, m, buildingTiles(m, x, z), level, owner)) {
            return PieceCheck::NeedsFloor;
        }
        if (mapGet(m.floor, levelKey(x, z, level + 1)) != NO_OBJECT) return PieceCheck::Taken;
        const ObjectId under = mapGet(m.piece, levelKey(x, z, level));
        if (under != NO_OBJECT && propInfo(w.get<Prop>(under)->kind).ladder) return PieceCheck::Taken;
        return PieceCheck::Ok;
    }
    if (level > 0 && !mapFloorSurface(w, m, x, z, level, owner)) return PieceCheck::NeedsFloor;
    if (mapGet(m.piece, levelKey(x, z, level)) != NO_OBJECT) return PieceCheck::Taken;
    // Stairs' top step is walked on
    if (level > 0 && mapGet(m.stairTop, levelKey(x, z, level)) != NO_OBJECT) return PieceCheck::Taken;
    if (info.ladder) {
        if (level + 1 > STOREY_MAX) return PieceCheck::NeedsFloor;
        if (mapGet(m.floor, levelKey(x, z, level + 1)) != NO_OBJECT) return PieceCheck::Taken;
    }
    return PieceCheck::Ok;
}

bool edgesClosed(World& w, ObjectId foundation, int level) {
    const Foundation* f = w.get<Foundation>(foundation);
    if (!f) return false;
    const BuildingMap m = buildingMap(w);
    return edgesClosedMap(w, m, buildingTiles(m, f->x0, f->z0), level);
}

void collapseCheck(World& w) {
    const BuildingMap m = buildingMap(w);
    std::unordered_set<long long> seen;
    std::vector<ObjectId> doomed;
    std::vector<std::pair<ObjectId, int>> drop;   // props on fallen floors, and the floor they were on
    bool fell = false;
    for (const auto& [k, fe] : m.foundation) {
        if (seen.count(k)) continue;
        const std::vector<long long> tiles = buildingTiles(m, keyX(k), keyZ(k));
        seen.insert(tiles.begin(), tiles.end());
        const std::unordered_set<long long> in(tiles.begin(), tiles.end());
        for (int level = 1; level <= STOREY_MAX; ++level) {
            bool finishedFloor = false, any = false;
            int standing = 0;
            for (long long tk : tiles) {
                const ObjectId fl = mapGet(m.floor, levelKey(keyX(tk), keyZ(tk), level));
                if (fl != NO_OBJECT) {
                    any = true;
                    finishedFloor = finishedFloor || !w.has<Blueprint>(fl);
                }
                const ObjectId p = mapGet(m.piece, levelKey(keyX(tk), keyZ(tk), level - 1));
                if (isWallOrDoor(w, p) && !w.has<Blueprint>(p)) ++standing;
            }
            if (!any) break;
            if (standing > 0 || !finishedFloor) continue;
            // Nothing holds this floor up: it and everything above it on this building fall in
            for (ObjectId e : w.view<Prop, Transform>()) {
                const Transform* tr = w.get<Transform>(e);
                const int s = storeyOf(w, e);
                if (s < level || !in.count(tileKey(tileOf(tr->x), tileOf(tr->z)))) continue;
                if (isBuildingPiece(w.get<Prop>(e)->kind)) doomed.push_back(e);
                else drop.push_back({ e, s });
            }
            // What lay on those floors falls too
            for (ObjectId e : w.view<GroundItem, Transform>()) {
                const Transform* tr = w.get<Transform>(e);
                const int s = storeyOf(w, e);
                if (s >= level && in.count(tileKey(tileOf(tr->x), tileOf(tr->z)))) drop.push_back({ e, s });
            }
            fell = true;
            break;
        }
    }
    for (ObjectId e : doomed) {
        releasePropLinks(w, e);
        w.destroy(e);
    }
    for (const auto& [e, s] : drop) {
        if (Transform* tr = w.get<Transform>(e)) tr->y -= storeyBaseHeight(s);
        w.remove<Storey>(e);
    }
    if (fell) queueLog(w, lang("MISC_FLOORS_COLLAPSE"), LogKind::Seen);
}

ObjectId placePiece(World& w, const Terrain& t, PropKind kind, int x, int z, int owner, BuildMaterial material, int level, int dir) {
    const PropInfo& info = propInfo(kind);
    const BuildingMap m = buildingMap(w);
    float yaw = info.stairs ? (float)(((dir % 4) + 4) % 4) * 1.5707963f : 0.0f;
    if (info.door) {
        // Across the doorway: in line with the walls beside it, or along the building's edge
        const bool ns = isWallOrDoor(w, mapGet(m.piece, levelKey(x, z - 1, level))) || isWallOrDoor(w, mapGet(m.piece, levelKey(x, z + 1, level)));
        const bool ew = isWallOrDoor(w, mapGet(m.piece, levelKey(x - 1, z, level))) || isWallOrDoor(w, mapGet(m.piece, levelKey(x + 1, z, level)));
        const bool westOrEastEdge = !m.foundation.count(tileKey(x - 1, z)) || !m.foundation.count(tileKey(x + 1, z));
        if ((ns && !ew) || (!ns && !ew && westOrEastEdge)) yaw = 1.5707963f;
    }
    const int storey = info.upperFloor ? level + 1 : level;
    const ObjectId e = placeBuiltProp(w, t, kind, x + 0.5f, z + 0.5f, yaw, owner);
    if (Transform* tr = w.get<Transform>(e)) {
        const float ground = t.heightAt(x + 0.5f, z + 0.5f);
        // An upper floor's slab sits under its top; anything else stands on its floor's top
        if (info.upperFloor) tr->y = ground + floorSlabHeight(storey);
        else if (storey > 0) tr->y = ground + storeyBaseHeight(storey);
    }
    if (storey > 0) w.add(e, Storey{ storey });
    w.remove<Lock>(e);   // a door locks once it is built
    if (takesMaterial(kind) && material != BuildMaterial::Wood) w.add(e, Material{ material });
    if (info.materials > 0.0) w.add(e, Blueprint{});
    else finishBlueprint(w, const_cast<Terrain&>(t), e);
    return e;
}

int supplyBlueprint(World& w, ObjectId blueprint, int logs) {
    Blueprint* bp = w.get<Blueprint>(blueprint);
    if (!bp || logs <= 0) return 0;
    const int took = std::clamp(materialsNeeded(w, blueprint) - bp->supplied, 0, logs);
    bp->supplied += took;
    return took;
}

bool finishBlueprint(World& w, Terrain& t, ObjectId prop) {
    const PropInfo* info = infoOf(w, prop);
    if (!info) return false;
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    pieceTiles(w, prop, x0, z0, x1, z1);
    const int level = storeyOf(w, prop);
    // Units standing where it goes, on its floor, are asked to step away, and it waits. (A unit beside it doesn't stop it:
    // Auto-Construct finishes corners first, so a unit isn't walled into a corner tile.)
    const int bx0 = x0, bz0 = z0, bx1 = x1, bz1 = z1;
    std::vector<ObjectId> inTheWay;
    if (!info->upperFloor) {
        for (ObjectId u : w.view<Entity, Transform>()) {
            if (w.has<Corpse>(u) || outCold(w, u) || w.has<Carried>(u) || w.has<Occupying>(u) || storeyOf(w, u) != level) continue;
            const Transform* ut = w.get<Transform>(u);
            const int ux = tileOf(ut->x), uz = tileOf(ut->z);
            if (ux >= bx0 && ux <= bx1 && uz >= bz0 && uz <= bz1) inTheWay.push_back(u);
        }
    }
    if (!inTheWay.empty()) {
        const BuildingMap m = buildingMap(w);
        for (ObjectId u : inTheWay) {
            if (walking(w, u)) continue;
            const Transform* ut = w.get<Transform>(u);
            const int ux = tileOf(ut->x), uz = tileOf(ut->z);
            bool moved = false;
            for (int ring = 1; ring <= FOUNDATION_MAX && !moved; ++ring) {
                for (int dz = -ring; dz <= ring && !moved; ++dz) {
                    for (int dx = -ring; dx <= ring && !moved; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                        const int x = ux + dx, z = uz + dz;
                        if (x >= bx0 && x <= bx1 && z >= bz0 && z <= bz1) continue;
                        if (level == 0 ? !t.walkable(x, z) : !floorWalkable(w, m, x, z, level)) continue;
                        leaveSquad(w, u);
                        w.remove<IdleSpot>(u);
                        walkSoloTo(w, t, u, Vec2{ x + 0.5f, z + 0.5f }, level);
                        moved = true;
                    }
                }
            }
        }
        if (Blueprint* bp = w.get<Blueprint>(prop)) bp->retryIn = BLOCKED_RETRY_SECONDS;
        return false;
    }
    // A foundation's ground is levelled, with the rest of its building's, before it is finished
    if (info->foundation) levelFoundationGround(w, t, prop);
    w.remove<Blueprint>(prop);
    const double tough = materialToughness(materialOf(w, prop));
    buildingTilesChanged(w, x0, z0, x1, z1);
    if (info->wallRule) {
        w.add(prop, Health{ WALL_HEALTH * tough, WALL_HEALTH * tough });
    }
    if (info->door) {
        w.add(prop, Lock{ true, std::max(1, info->lockLevel), ownerOf(w, prop) });
        w.add(prop, Health{ DOOR_HEALTH * tough, DOOR_HEALTH * tough });
    }
    queueSoundFrom(w, Sfx::DoneTree, prop);
    queueLog(w, langf("MISC_BUILT", { lang(info->nameKey) }), LogKind::Other, ownerOf(w, prop) != FACTION_PLAYER);
    return true;
}

void rebuildBlocking(World& w, Terrain& t) {
    t.clearBlocked();
    t.clearDecks();
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (w.has<Blueprint>(e)) continue;
        if (const Foundation* f = w.get<Foundation>(e)) {
            for (int z = f->z0; z < f->z0 + f->h; ++z) {
                for (int x = f->x0; x < f->x0 + f->w; ++x) t.setDeck(x, z, true);
            }
            continue;
        }
        if (storeyOf(w, e) != 0) continue;
        if (isStairs(w, e)) {
            // Stairs' steps are walked up from their foot, not across
            int xs[4], zs[4];
            stairsTiles(w, e, xs, zs);
            for (int i = 0; i < 3; ++i) t.setBlocked(xs[i], zs[i], true);
            continue;
        }
        if (!propInfo(w.get<Prop>(e)->kind).wallRule) continue;
        const Transform* tr = w.get<Transform>(e);
        t.setBlocked(tileOf(tr->x), tileOf(tr->z), true);
    }
    w.resource<BuildingsChanged>().dirty = false;
    w.resource<BuildingsChanged>().tiles.clear();
    collapseCheck(w);
}

void buildingTilesChanged(World& w, int x0, int z0, int x1, int z1) {
    std::vector<std::pair<int, int>>& tiles = w.resource<BuildingsChanged>().tiles;
    for (int z = std::min(z0, z1); z <= std::max(z0, z1); ++z) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) tiles.push_back({ x, z });
    }
}

namespace {
// Whether a walk from a through the rest of its waypoints passes near any of the tiles
bool routeNear(Vec2 from, const std::vector<Vec2>& waypoints, size_t next, const std::vector<std::pair<int, int>>& tiles) {
    constexpr float NEAR = 1.25f;
    auto segNear = [&](Vec2 a, Vec2 b) {
        const float dx = b.x - a.x, dz = b.z - a.z, len2 = dx * dx + dz * dz;
        for (const auto& [tx, tz] : tiles) {
            const float cx = tx + 0.5f, cz = tz + 0.5f;
            const float s = len2 > 1e-6f ? std::clamp(((cx - a.x) * dx + (cz - a.z) * dz) / len2, 0.0f, 1.0f) : 0.0f;
            const float ex = a.x + dx * s - cx, ez = a.z + dz * s - cz;
            if (ex * ex + ez * ez <= NEAR * NEAR) return true;
        }
        return false;
    };
    Vec2 at = from;
    for (size_t i = next; i < waypoints.size(); ++i) {
        if (segNear(at, waypoints[i])) return true;
        at = waypoints[i];
    }
    return false;
}
}

void refreshBuildingTiles(World& w, Terrain& t) {
    BuildingsChanged& bc = w.resource<BuildingsChanged>();
    if (bc.tiles.empty()) return;
    std::vector<std::pair<int, int>> tiles;
    tiles.swap(bc.tiles);
    std::sort(tiles.begin(), tiles.end());
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    std::unordered_set<long long> changed;
    int x0 = tiles.front().first, x1 = x0, z0 = tiles.front().second, z1 = z0;
    for (const auto& [x, z] : tiles) {
        changed.insert(tileKey(x, z));
        t.setBlocked(x, z, false);
        t.setDeck(x, z, false);
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        z0 = std::min(z0, z); z1 = std::max(z1, z);
    }
    // Only the changed tiles are worked out again: a finished ground-floor wall closes its tile, a finished foundation
    // tile decks its own
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (w.has<Blueprint>(e)) continue;
        if (const Foundation* f = w.get<Foundation>(e)) {
            if (f->x0 > x1 || f->x0 + f->w - 1 < x0 || f->z0 > z1 || f->z0 + f->h - 1 < z0) continue;
            for (const auto& [x, z] : tiles) {
                if (f->contains(x, z)) t.setDeck(x, z, true);
            }
            continue;
        }
        if (storeyOf(w, e) != 0) continue;
        if (isStairs(w, e)) {
            int xs[4], zs[4];
            stairsTiles(w, e, xs, zs);
            for (int i = 0; i < 3; ++i) {
                if (changed.count(tileKey(xs[i], zs[i]))) t.setBlocked(xs[i], zs[i], true);
            }
            continue;
        }
        if (!propInfo(w.get<Prop>(e)->kind).wallRule) continue;
        const Transform* tr = w.get<Transform>(e);
        if (changed.count(tileKey(tileOf(tr->x), tileOf(tr->z)))) t.setBlocked(tileOf(tr->x), tileOf(tr->z), true);
    }
    // Walks that pass near a changed tile are planned again, around a new wall or through a gap left by a lost one
    for (ObjectId u : w.view<SoloMove, Transform>()) {
        if (w.has<Upstairs>(u) || w.has<Climbing>(u)) continue;
        SoloMove* so = w.get<SoloMove>(u);
        const Transform* ut = w.get<Transform>(u);
        if (so->next >= so->waypoints.size() || !routeNear(Vec2{ ut->x, ut->z }, so->waypoints, so->next, tiles)) continue;
        planSoloMove(w, t, u, so->waypoints.back());
    }
    for (ObjectId sq : w.view<ActiveSquad>()) {
        ActiveSquad* a = w.get<ActiveSquad>(sq);
        if (a->arrived || a->next >= a->waypoints.size() || !routeNear(Vec2{ a->cx, a->cz }, a->waypoints, a->next, tiles)) continue;
        planActiveSquad(w, t, sq, a->waypoints.back());
    }
    for (ObjectId u : w.view<ChaseRoute>()) {
        ChaseRoute* r = w.get<ChaseRoute>(u);
        r->checkIn = 0.0f;
        r->planIn = 0.0f;
        r->waypoints.clear();
        r->next = 0;
    }
    // Units on the changed tiles take its new height (onto a new deck, or down off a lost one)
    for (ObjectId u : w.view<Entity, Transform>()) {
        if (w.has<Upstairs>(u) || w.has<Carried>(u) || w.has<Occupying>(u) || w.has<Climbing>(u)) continue;
        Transform* ut = w.get<Transform>(u);
        if (changed.count(tileKey(tileOf(ut->x), tileOf(ut->z)))) ut->y = t.standHeight(ut->x, ut->z);
    }
    collapseCheck(w);
}

WallShape wallShapeFor(int mask) {
    static const int BASE[6] = { 0, 2, 10, 6, 14, 15 };   // east; east and west; east and south; and west too; all four
    auto turn = [](int m) { return ((m << 1) | (m >> 3)) & 15; };   // north to east, east to south, and so on
    int bits = 0;
    for (int b = 0; b < 4; ++b) bits += (mask >> b) & 1;
    int shape = 0;
    if (bits == 1) shape = 1;
    else if (bits == 2) shape = (mask == 10 || mask == 5) ? 2 : 3;
    else if (bits == 3) shape = 4;
    else if (bits == 4) shape = 5;
    int m = BASE[shape];
    for (int k = 0; k < 4; ++k) {
        if (m == mask) return WallShape{ shape, -1.5707963f * (float)k };
        m = turn(m);
    }
    return WallShape{ shape, 0.0f };
}

WallShape wallShapeOf(World& w, ObjectId wall) {
    const Transform* tr = w.get<Transform>(wall);
    if (!tr) return {};
    const int x = tileOf(tr->x), z = tileOf(tr->z), level = storeyOf(w, wall);
    auto joins = [&](int nx, int nz) { return isWallOrDoor(w, groundPieceAt(w, nx, nz, level)); };
    const int mask = (joins(x, z - 1) ? 1 : 0) | (joins(x + 1, z) ? 2 : 0) | (joins(x, z + 1) ? 4 : 0) | (joins(x - 1, z) ? 8 : 0);
    return wallShapeFor(mask);
}

bool doorLets(World& w, ObjectId door, ObjectId u) {
    const Lock* l = w.get<Lock>(door);
    if (!l || !l->locked || w.has<Blueprint>(door)) return true;
    const int f = factionOf(w, u);
    return f == l->faction || factionsAllied(w, f, l->faction);
}

void doorSystem(World& w, float dt) {
    for (ObjectId u : w.view<AtDoor>()) {
        AtDoor& at = *w.get<AtDoor>(u);
        at.since += dt;
        const Lock* l = w.get<Lock>(at.door);
        const PropInfo* info = infoOf(w, at.door);
        const bool wall = info && info->wallRule;
        const bool barrier = wall ? w.has<Prop>(at.door) : (l && l->locked);
        if (at.since > AT_DOOR_FORGET_SECONDS || !barrier || w.has<Commandable>(u) || outCold(w, u)) {
            if (at.since > AT_DOOR_FORGET_SECONDS || !barrier) w.remove<AtDoor>(u);
            continue;
        }
        const int owner = wall ? ownerOf(w, at.door) : l->faction;
        if (owner < 0) continue;
        const bool hostile = owner == FACTION_PLAYER ? hostileToPlayer(w, u) : factionsHostile(w, factionOf(w, u), owner);
        if (!hostile) continue;
        if (const Working* wk = w.get<Working>(u); wk && wk->prop == at.door) continue;
        if (!wall && levelOf(statsOf(w, u), StatId::Lockpicking) >= l->level && !w.has<Mob>(u)) {
            tryStartUnlock(w, u, at.door);
            continue;
        }
        Combat* c = w.get<Combat>(u);
        Health* h = w.get<Health>(at.door);
        if (!c || !h || c->swingLeft > 0.0f) continue;
        const CombatNumbers n = numbersOf(w, u);
        c->swingLeft = n.swingTime;
        h->value -= std::max(1.0, (double)n.attack);
        queueSoundFrom(w, Sfx::Hit, at.door);
        if (h->value <= 0.0) {
            queueLog(w, langf("MISC_DOOR_SMASHED", { nameOf(w, u), info ? lang(info->nameKey) : std::string() }), LogKind::Seen);
            const ObjectId door = at.door;
            destroyBuilding(w, door);
            for (ObjectId o : w.view<AtDoor>()) {
                if (w.get<AtDoor>(o)->door == door) w.remove<AtDoor>(o);
            }
            return;
        }
    }
}

int bedHouseOwner(World& w, ObjectId bed) {
    const Transform* tr = w.get<Transform>(bed);
    if (!tr) return -1;
    const ObjectId f = foundationAt(w, tileOf(tr->x), tileOf(tr->z));
    return f != NO_OBJECT && !w.has<Blueprint>(f) ? ownerOf(w, f) : -1;
}

bool bedOpenTo(World& w, ObjectId bed, ObjectId u) {
    if (w.has<BedPublic>(bed)) return true;
    const int owner = bedHouseOwner(w, bed);
    return owner < 0 || owner == factionOf(w, u);
}

bool isUpstairs(World& w, ObjectId u) { return w.has<Upstairs>(u); }

Vec2 nearestFloorSpot(World& w, ObjectId, Vec2 target, int level) {
    const BuildingMap m = buildingMap(w);
    Vec2 best = target;
    float bestD2 = -1.0f;
    std::vector<long long> tiles;
    for (const auto& [lk, e] : m.floor) tiles.push_back(lk);
    for (const auto& [lk, e] : m.stairTop) tiles.push_back(lk);
    for (const long long lk : tiles) {
        if ((int)(lk % 16) != level) continue;
        const long long tk = lk / 16;
        const int x = keyX(tk), z = keyZ(tk);
        if (!floorWalkable(w, m, x, z, level)) continue;
        const float cx = x + 0.5f, cz = z + 0.5f;
        const float d2 = (cx - target.x) * (cx - target.x) + (cz - target.z) * (cz - target.z);
        if (bestD2 < 0.0f || d2 < bestD2) {
            bestD2 = d2;
            best = Vec2{ cx, cz };
        }
    }
    return best;
}

bool canClimbUp(World& w, ObjectId ladder) {
    const PropInfo* info = infoOf(w, ladder);
    if (!info || !info->ladder || w.has<Blueprint>(ladder)) return false;
    const BuildingMap m = buildingMap(w);
    Vec2 top;
    return ladderTop(w, m, ladder, top);
}

Vec2 climbFoot(World& w, ObjectId ladder) {
    if (isStairs(w, ladder)) {
        const BuildingMap m = buildingMap(w);
        int xs[4], zs[4];
        stairsTiles(w, ladder, xs, zs);
        // In front of the bottom step if it is open, else beside it
        return nearestLanding(w, m, ladder, Vec2{ 2.0f * xs[0] - xs[1] + 0.5f, 2.0f * zs[0] - zs[1] + 0.5f });
    }
    const Transform* lt = w.get<Transform>(ladder);
    return lt ? Vec2{ lt->x, lt->z } : Vec2{ 0.0f, 0.0f };
}

bool climbTop(World& w, ObjectId ladder, Vec2& out) {
    const BuildingMap m = buildingMap(w);
    return ladderTop(w, m, ladder, out);
}

bool atClimbSpot(World& w, ObjectId u, ObjectId ladder) {
    const Transform* ut = w.get<Transform>(u);
    const Transform* lt = w.get<Transform>(ladder);
    if (!ut || !lt) return false;
    const int ls = storeyOf(w, ladder), us = storeyOf(w, u);
    if (us != ls && us != ls + 1) return false;
    // The tile it goes from: the ladder's own (its head is beside it), or the stairs' bottom step (from an open floor tile
    // beside it) or their top step
    int ax = tileOf(lt->x), az = tileOf(lt->z);
    const int ux = tileOf(ut->x), uz = tileOf(ut->z);
    const BuildingMap m = buildingMap(w);
    if (isStairs(w, ladder)) {
        int xs[4], zs[4];
        stairsTiles(w, ladder, xs, zs);
        const int i = us == ls ? 0 : 2;
        ax = xs[i];
        az = zs[i];
        if (us == ls && !stairsFootOpen(w, m, ux, uz, ls)) return false;
    }
    if (std::max(std::abs(ux - ax), std::abs(uz - az)) > 1) return false;
    // Inside the building: on one of its foundation tiles, so not outside its walls
    if (!m.foundation.count(tileKey(ux, uz))) return false;
    const std::vector<long long> tiles = buildingTiles(m, ax, az);
    return std::find(tiles.begin(), tiles.end(), tileKey(ux, uz)) != tiles.end();
}

namespace {
// Stairs walked, at the unit's own pace: from where it stands onto the bottom step and up to the top one, or onto the
// top step, down to the bottom one and off onto the floor beside it. Each step is a third of the way up; crossing from
// one height to the next is a hop, noted by how far along the walk it comes.
void planStairsWalk(World& w, const Terrain& t, ObjectId u, ObjectId stairs, bool down, const Vec2* off, Climbing& c) {
    const Transform* ut = w.get<Transform>(u);
    const BuildingMap m = buildingMap(w);
    int xs[4], zs[4];
    stairsTiles(w, stairs, xs, zs);
    const Vec2 bottom{ xs[0] + 0.5f, zs[0] + 0.5f }, top{ xs[2] + 0.5f, zs[2] + 0.5f };
    const Vec2 here{ ut->x, ut->z };
    if (down) c.path = { here, top, bottom, off ? *off : nearestLanding(w, m, stairs, Vec2{ 2.0f * bottom.x - xs[1] - 0.5f, 2.0f * bottom.z - zs[1] - 0.5f }) };
    else c.path = { here, bottom, top };
    const int ls = storeyOf(w, stairs);
    const float ground = t.heightAt(bottom.x, bottom.z);
    const float low = ls == 0 ? t.standHeight(bottom.x, bottom.z) - ground : storeyBaseHeight(ls);
    const float rise = storeyBaseHeight(ls + 1) - low;
    float len = 0.0f;
    for (size_t i = 1; i < c.path.size(); ++i) len += std::hypot(c.path[i].x - c.path[i - 1].x, c.path[i].z - c.path[i - 1].z);
    // The height of the step under a point, or off the stairs, the floor it set off from (first half) or gets off onto
    auto heightAt = [&](Vec2 p, float along) {
        const int x = tileOf(p.x), z = tileOf(p.z);
        for (int i = 0; i < 3; ++i) {
            if (x == xs[i] && z == zs[i]) return low + rise * (float)(i + 1) / 3.0f;
        }
        return (along < len * 0.5f) == down ? low + rise : low;
    };
    c.startHeight = heightAt(here, 0.0f);
    c.hops.clear();
    float along = 0.0f, last = c.startHeight;
    constexpr float STEP = 0.02f;
    for (size_t i = 1; i < c.path.size(); ++i) {
        const Vec2 a = c.path[i - 1], b = c.path[i];
        const float seg = std::hypot(b.x - a.x, b.z - a.z);
        for (float s = STEP; s <= seg + 1e-4f; s += STEP) {
            const float k = std::min(1.0f, s / std::max(seg, 1e-4f));
            const float h = heightAt(Vec2{ a.x + (b.x - a.x) * k, a.z + (b.z - a.z) * k }, along + s);
            if (std::fabs(h - last) > 1e-3f) {
                c.hops.push_back(Climbing::Hop{ along + s - STEP * 0.5f, last, h });
                last = h;
            }
        }
        along += seg;
    }
    c.whole = c.left = std::max(0.2f, len / std::max(0.3f, walkSpeedOf(w, u)));
}
}

bool startClimb(World& w, const Terrain& t, ObjectId u, ObjectId ladder, const Vec2* off) {
    const Transform* lt = w.get<Transform>(ladder);
    const Transform* ut = w.get<Transform>(u);
    const PropInfo* info = infoOf(w, ladder);
    if (!lt || !ut || !info || !info->ladder || w.has<Blueprint>(ladder) || w.has<Climbing>(u) || outCold(w, u)) return false;
    const int ls = storeyOf(w, ladder), us = storeyOf(w, u);
    const bool routed = w.has<Route>(u);
    const bool down = us == ls + 1;
    if (down) {
        if (!atClimbSpot(w, u, ladder)) return false;
    } else if (us != ls || !canClimbUp(w, ladder) || !atClimbSpot(w, u, ladder)) {
        return false;
    }
    Climbing c{ ladder, CLIMB_SECONDS, down, CLIMB_SECONDS };
    if (info->stairs) planStairsWalk(w, t, u, ladder, down, off, c);
    if (!down) {
        if (w.has<Carrying>(u)) putDown(w, u);
        leaveSquad(w, u);
    }
    if (!routed) w.remove<SoloMove>(u);
    w.add(u, std::move(c));
    return true;
}

void moveUpstairs(World& w, Terrain& t, ObjectId u, Vec2 target) {
    if (!w.has<Upstairs>(u)) return;
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    walkSoloTo(w, t, u, target, -1);
}

void moveToFloor(World& w, Terrain& t, ObjectId u, Vec2 target, int storey) {
    if (!w.has<Commandable>(u) || cannotTakeOrder(w, u)) return;
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    walkSoloTo(w, t, u, target, storey);
}

namespace {
// Units walking up or down stairs last step, and the stairs: to catch any stopped partway. Not saved.
struct StairWalkers {
    std::vector<std::pair<ObjectId, ObjectId>> units;
};
}

void climbSystem(World& w, Terrain& t, float dt) {
    std::vector<ObjectId> done;
    for (ObjectId u : w.view<Climbing, Transform>()) {
        Climbing& c = *w.get<Climbing>(u);
        const Transform* lt = w.get<Transform>(c.ladder);
        if (!lt || outCold(w, u) || w.has<Carried>(u)) {
            w.remove<Climbing>(u);
            continue;
        }
        c.left -= dt;
        if (c.left <= 0.0f) done.push_back(u);
        if (isStairs(w, c.ladder) && c.whole > 0.0f && c.path.size() >= 2) {
            // Along its walk at its pace, at the height of the step under it, with a quick hop at each change of height
            // that goes a little past the new height and lands on it
            float len = 0.0f;
            for (size_t i = 1; i < c.path.size(); ++i) len += std::hypot(c.path[i].x - c.path[i - 1].x, c.path[i].z - c.path[i - 1].z);
            const float gone = std::clamp(1.0f - c.left / c.whole, 0.0f, 1.0f) * len;
            Transform* ut = w.get<Transform>(u);
            float left = gone;
            for (size_t i = 1; i < c.path.size(); ++i) {
                const Vec2 a = c.path[i - 1], b = c.path[i];
                const float seg = std::hypot(b.x - a.x, b.z - a.z);
                if (left <= seg || i + 1 == c.path.size()) {
                    const float k = std::clamp(left / std::max(seg, 1e-4f), 0.0f, 1.0f);
                    ut->x = a.x + (b.x - a.x) * k;
                    ut->z = a.z + (b.z - a.z) * k;
                    break;
                }
                left -= seg;
            }
            constexpr float HOP_SPAN = 0.22f, HOP_OVER = 0.25f;
            float y = c.startHeight;
            for (const Climbing::Hop& h : c.hops) {
                if (gone >= h.at + HOP_SPAN) {
                    y = h.to;
                    continue;
                }
                if (gone > h.at - HOP_SPAN) {
                    const float a = (gone - (h.at - HOP_SPAN)) / (2.0f * HOP_SPAN);
                    const float d = h.to - h.from, over = std::max(d, 0.0f) * 0.5f + HOP_OVER;
                    y = h.from + d * a + over * 4.0f * a * (1.0f - a);
                }
                break;
            }
            int xs[4], zs[4];
            stairsTiles(w, c.ladder, xs, zs);
            ut->y = t.heightAt(xs[0] + 0.5f, zs[0] + 0.5f) + y;
        }
    }
    // A unit off a ladder or stairs: at their foot on their floor, or at their head on the floor above
    // (stairs: the tile beside the bottom step it was heading for, or else the one nearest it)
    auto land = [&](const BuildingMap& m, ObjectId u, ObjectId ladder, bool down, const Vec2* off) {
        const Transform lt = *w.get<Transform>(ladder);
        const int ls = storeyOf(w, ladder);
        Transform* ut = w.get<Transform>(u);
        if (down) {
            const Vec2 foot = !isStairs(w, ladder) ? climbFoot(w, ladder) : off ? *off : nearestLanding(w, m, ladder, Vec2{ ut->x, ut->z });
            ut->x = foot.x;
            ut->z = foot.z;
            if (ls == 0) {
                w.remove<Upstairs>(u);
                ut->y = t.standHeight(ut->x, ut->z);
            } else {
                w.add(u, Upstairs{ mapGet(m.foundation, tileKey(tileOf(lt.x), tileOf(lt.z))), ls });
                ut->y = t.heightAt(ut->x, ut->z) + storeyBaseHeight(ls);
            }
        } else {
            Vec2 spot{ lt.x, lt.z };
            ladderTop(w, m, ladder, spot);
            ut->x = spot.x;
            ut->z = spot.z;
            ut->y = t.heightAt(spot.x, spot.z) + storeyBaseHeight(ls + 1);
            w.add(u, Upstairs{ mapGet(m.foundation, tileKey(tileOf(spot.x), tileOf(spot.z))), ls + 1 });
        }
    };
    if (!done.empty()) {
        const BuildingMap m = buildingMap(w);
        for (ObjectId u : done) {
            const Climbing c = *w.get<Climbing>(u);
            w.remove<Climbing>(u);
            w.remove<IdleSpot>(u);
            if (!w.has<Route>(u)) w.remove<SoloMove>(u);
            land(m, u, c.ladder, c.down, c.down && !c.path.empty() ? &c.path.back() : nullptr);
            queueSoundFrom(w, Sfx::Occupy, c.ladder);
        }
    }
    // Units stopped partway up or down stairs, as by a new order: they step off at the nearer end, and the walk they were
    // just given is planned again from there
    {
        StairWalkers& sw = w.resource<StairWalkers>();
        std::vector<std::pair<ObjectId, ObjectId>> stopped;
        for (const auto& [u, stairs] : sw.units) {
            if (!w.has<Climbing>(u) && w.has<Transform>(u) && w.has<Transform>(stairs) && !w.has<Carried>(u) &&
                std::find(done.begin(), done.end(), u) == done.end()) {
                stopped.push_back({ u, stairs });
            }
        }
        sw.units.clear();
        for (ObjectId u : w.view<Climbing>()) {
            const ObjectId l = w.get<Climbing>(u)->ladder;
            if (isStairs(w, l)) sw.units.push_back({ u, l });
        }
        if (!stopped.empty()) {
            const BuildingMap m = buildingMap(w);
            for (const auto& [u, stairs] : stopped) {
                const Transform* ut = w.get<Transform>(u);
                const Vec2 foot = nearestLanding(w, m, stairs, Vec2{ ut->x, ut->z });
                Vec2 top = foot;
                ladderTop(w, m, stairs, top);
                const bool nearTop = std::hypot(ut->x - top.x, ut->z - top.z) < std::hypot(ut->x - foot.x, ut->z - foot.z);
                // The walk it was given: where to, and on which floor (by its route if it had one)
                bool walk = false;
                int storey = -1;
                Vec2 goal{ 0.0f, 0.0f };
                if (const Route* r = w.get<Route>(u); r && !r->legs.empty()) {
                    walk = true;
                    goal = r->goal;
                    storey = r->legs.back().storey;
                } else if (const SoloMove* so = w.get<SoloMove>(u); so && !so->waypoints.empty()) {
                    walk = true;
                    goal = so->waypoints.back();
                }
                land(m, u, stairs, !nearTop, &foot);
                if (walk) walkSoloTo(w, t, u, goal, storey);
            }
        }
    }
    // Upstairs with no floor under it any more: it comes down to the ground
    const std::vector<ObjectId> ups = w.view<Upstairs, Transform>();
    if (ups.empty()) return;
    const BuildingMap m = buildingMap(w);
    for (ObjectId u : ups) {
        if (w.has<Climbing>(u)) continue;
        Transform* ut = w.get<Transform>(u);
        const int level = w.get<Upstairs>(u)->level;
        if (mapFloorSurface(w, m, tileOf(ut->x), tileOf(ut->z), level, -1)) {
            if (!w.has<Occupying>(u) && !w.has<Carried>(u)) ut->y = t.heightAt(ut->x, ut->z) + storeyBaseHeight(level);
            continue;
        }
        w.remove<Upstairs>(u);
        w.remove<Route>(u);
        ut->y = t.standHeight(ut->x, ut->z);
    }
}

void planSoloMove(World& w, Terrain& t, ObjectId e, Vec2 target) {
    SoloMove* so = w.get<SoloMove>(e);
    if (!so) so = &w.add(e, SoloMove{});
    const Transform* tr = w.get<Transform>(e);
    if (!tr) return;
    if (const Upstairs* up = w.get<Upstairs>(e)) {
        const BuildingMap m = buildingMap(w);
        std::vector<Vec2> path = floorPath(w, m, up->level, Vec2{ tr->x, tr->z }, target);
        if (path.empty()) path = { nearestFloorSpot(w, up->foundation, target, up->level) };
        so->waypoints = std::move(path);
        so->next = 0;
        return;
    }
    so->waypoints = t.findPath({ tr->x, tr->z }, target);
    so->next = 0;
}

void walkSoloTo(World& w, Terrain& t, ObjectId e, Vec2 target, int storey) {
    w.remove<Route>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!tr) return;
    const int here = storeyOf(w, e);
    if (storey < 0) {
        storey = 0;
        if (here > 0) {
            const BuildingMap m = buildingMap(w);
            if (floorWalkable(w, m, tileOf(target.x), tileOf(target.z), here)) storey = here;
        }
    }
    w.add(e, SoloMove{});
    if (storey == here) {
        if (here == 0) {
            planSoloMove(w, t, e, target);
            return;
        }
        const BuildingMap m = buildingMap(w);
        std::vector<Vec2> path = floorPath(w, m, here, Vec2{ tr->x, tr->z }, target);
        if (!path.empty()) {
            w.get<SoloMove>(e)->waypoints = std::move(path);
            return;
        }
    }
    Route r;
    if (planRoute(w, t, e, target, storey, r)) {
        const Vec2 first = r.legs.front().to;
        r.planned = true;
        w.add(e, std::move(r));
        planSoloMove(w, t, e, first);
        return;
    }
    planSoloMove(w, t, e, target);
}

namespace {
// A unit chasing a foe on another floor: how soon it looks for a way there again. Not saved.
struct FloorChase {
    float retryIn = 0.0f;
};
}

void routeSystem(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<Route>()) {
        if (w.has<Climbing>(u)) continue;
        Route& r = *w.get<Route>(u);
        SoloMove* so = w.get<SoloMove>(u);
        if (!so || r.leg >= r.legs.size() || outCold(w, u) || w.has<Carried>(u)) {
            w.remove<Route>(u);
            continue;
        }
        const RouteLeg leg = r.legs[r.leg];
        if (storeyOf(w, u) != leg.storey) {
            w.remove<Route>(u);
            continue;
        }
        if (!r.planned) {
            planSoloMove(w, t, u, leg.to);
            r.planned = true;
            continue;
        }
        const bool arrived = so->next >= so->waypoints.size();
        if (leg.ladder != NO_OBJECT) {
            const Transform* ut = w.get<Transform>(u);
            const float near = ut ? std::hypot(ut->x - leg.to.x, ut->z - leg.to.z) : 1e9f;
            if (!arrived && (near > 0.9f || !atClimbSpot(w, u, leg.ladder))) continue;
            const Vec2 goal = r.goal;
            if (startClimb(w, t, u, leg.ladder, &leg.off)) {
                ++r.leg;
                r.planned = false;
                so->waypoints = { goal };
                so->next = 0;
            } else {
                w.remove<Route>(u);
                so->next = so->waypoints.size();
            }
            continue;
        }
        if (arrived) {
            w.remove<Route>(u);
            if (w.has<FloorChase>(u)) w.remove<SoloMove>(u);
        }
    }
    // Units fighting a foe on another floor go there by way of the ladders
    for (ObjectId u : w.view<Combat, Transform>()) {
        const Combat* c = w.get<Combat>(u);
        ObjectId foe = c->target;
        if (const AttackOrder* ao = w.get<AttackOrder>(u)) foe = ao->target;
        FloorChase* fc = w.get<FloorChase>(u);
        if (foe == NO_OBJECT || !w.has<Entity>(foe) || storeyOf(w, foe) == storeyOf(w, u) || outCold(w, u) || w.has<Carried>(u) ||
            w.has<Occupying>(u)) {
            if (fc && !w.has<Route>(u)) w.remove<FloorChase>(u);
            continue;
        }
        if (w.has<Route>(u) || w.has<Climbing>(u)) continue;
        if (!fc) fc = &w.add(u, FloorChase{});
        fc->retryIn -= dt;
        if (fc->retryIn > 0.0f) continue;
        fc->retryIn = 2.0f;
        const Transform* ft = w.get<Transform>(foe);
        walkSoloTo(w, t, u, Vec2{ ft->x, ft->z }, storeyOf(w, foe));
        if (!w.has<Route>(u)) w.remove<SoloMove>(u);
    }
}

void foundationLost(World& w, ObjectId foundation) {
    const Foundation* f = w.get<Foundation>(foundation);
    if (!f) return;
    const Foundation keep = *f;
    std::vector<ObjectId> pieces;
    std::vector<std::pair<ObjectId, int>> drop;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (e == foundation) continue;
        const Transform* tr = w.get<Transform>(e);
        if (!keep.contains(tileOf(tr->x), tileOf(tr->z))) continue;
        if (propInfo(w.get<Prop>(e)->kind).onFoundation) pieces.push_back(e);
        else if (const int s = storeyOf(w, e); s > 0) drop.push_back({ e, s });
    }
    // Things lying on its upper floors come down with them
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (const int s = storeyOf(w, e); s > 0 && keep.contains(tileOf(tr->x), tileOf(tr->z))) drop.push_back({ e, s });
    }
    for (ObjectId e : pieces) {
        Transform at = *w.get<Transform>(e);
        // The logs brought to a piece upstairs fall to the ground, the building gone from under them
        if (const int s = storeyOf(w, e); s > 0) at.y -= propInfo(w.get<Prop>(e)->kind).upperFloor ? floorSlabHeight(s) : storeyBaseHeight(s);
        int logs = 0;
        if (const Blueprint* bp = w.get<Blueprint>(e); bp && bp->progress <= 0.0) logs = bp->supplied;
        const int material = materialItem(materialOf(w, e));
        releasePropLinks(w, e);
        w.destroy(e);
        if (logs > 0) spawnGroundItem(w, at, material, logs);
    }
    for (const auto& [e, s] : drop) {
        if (Transform* tr = w.get<Transform>(e)) tr->y -= storeyBaseHeight(s);
        w.remove<Storey>(e);
    }
    buildingTilesChanged(w, keep.x0, keep.z0, keep.x0 + keep.w - 1, keep.z0 + keep.h - 1);
}

void settleBuildingsAfterLoad(World& w, Terrain& t) {
    // Foundations from before they were laid tile by tile: one foundation tile for each tile of the rectangle
    for (ObjectId e : w.view<Foundation>()) {
        const Foundation f = *w.get<Foundation>(e);
        if (f.w * f.h <= 1) continue;
        const int owner = ownerOf(w, e);
        const BuildMaterial material = materialOf(w, e);
        const Blueprint* bp = w.get<Blueprint>(e);
        const bool planned = bp != nullptr;
        const int supplied = bp && bp->progress <= 0.0 ? bp->supplied : 0;
        const Transform at = *w.get<Transform>(e);
        for (int z = f.z0; z < f.z0 + f.h; ++z) {
            for (int x = f.x0; x < f.x0 + f.w; ++x) {
                const ObjectId n = owner >= 0 ? placeBuiltProp(w, t, PropKind::Foundation, x + 0.5f, z + 0.5f, 0.0f, owner)
                                              : makeProp(w, PropKind::Foundation, Transform{ x + 0.5f, t.heightAt(x + 0.5f, z + 0.5f), z + 0.5f });
                w.add(n, Foundation{ x, z, 1, 1, {} });
                if (planned) w.add(n, Blueprint{});
                if (material != BuildMaterial::Wood) w.add(n, Material{ material });
            }
        }
        if (supplied > 0) spawnGroundItem(w, at, materialItem(material), supplied);
        releasePropLinks(w, e);
        w.destroy(e);
    }
    // The floor everything above the ground stands on, from its height
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (info.airship || w.has<Foundation>(e)) continue;
        Transform* tr = w.get<Transform>(e);
        const float ground = t.heightAt(tr->x, tr->z);
        const int level = storeyAtHeight(tr->y - ground);
        if (level > 0) {
            w.add(e, Storey{ level });
            // Set to its floor's height as this game spaces floors (older saves stacked them a slab short)
            tr->y = ground + (info.upperFloor ? floorSlabHeight(level) : storeyBaseHeight(level));
        } else {
            w.remove<Storey>(e);
        }
    }
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        Transform* tr = w.get<Transform>(e);
        const float ground = t.heightAt(tr->x, tr->z);
        const int level = storeyAtHeight(tr->y - ground);
        if (level > 0) {
            w.add(e, Storey{ level });
            tr->y = ground + storeyBaseHeight(level);
        }
    }
    for (ObjectId u : w.view<Upstairs, Transform>()) {
        const Transform* tr = w.get<Transform>(u);
        Upstairs& up = *w.get<Upstairs>(u);
        up.level = std::max(1, storeyAtHeight(tr->y - t.heightAt(tr->x, tr->z)));
        up.foundation = foundationAt(w, tileOf(tr->x), tileOf(tr->z));
    }
    // A walk up or down stairs isn't saved, so a unit saved partway is loaded standing on a step, which is no floor it can
    // walk off: it is handed to the stairs' stopped-partway check, which sets it down at the nearer end on the next step
    // of the game. Standing on the top step from the floor above is all right.
    std::vector<ObjectId> stairs;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (isStairs(w, e) && !w.has<Blueprint>(e)) stairs.push_back(e);
    }
    if (!stairs.empty()) {
        StairWalkers& sw = w.resource<StairWalkers>();
        for (ObjectId u : w.view<Entity, Transform>()) {
            if (w.has<Occupying>(u) || w.has<Carried>(u) || w.has<Climbing>(u)) continue;
            const Transform* ut = w.get<Transform>(u);
            const int ux = tileOf(ut->x), uz = tileOf(ut->z);
            const float above = ut->y - t.heightAt(ut->x, ut->z);
            const Upstairs* up = w.get<Upstairs>(u);
            for (ObjectId s : stairs) {
                const int ls = storeyOf(w, s);
                if (above < storeyBaseHeight(ls) - 0.5f || above > storeyBaseHeight(ls + 1) + 0.5f) continue;
                int xs[4], zs[4];
                stairsTiles(w, s, xs, zs);
                int step = -1;
                for (int i = 0; i < 3; ++i) {
                    if (xs[i] == ux && zs[i] == uz) step = i;
                }
                if (step < 0 || (step == 2 && up && up->level == ls + 1)) continue;
                sw.units.push_back({ u, s });
                break;
            }
        }
    }
    w.resource<BuildingsChanged>().dirty = true;
}

bool startBuildStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    const Transform* ut = w.get<Transform>(u);
    Inventory* inv = w.get<Inventory>(u);
    if (!ut || w.has<Mob>(u)) return false;
    const int faction = factionOf(w, u);
    // Auto-Construct: build the nearest blueprint of its faction that has everything it needs; failing that, bring the
    // nearest one that still wants materials what it wants, carried or fetched from a store of its faction
    // Corners first: a wall or door with walls or doors (planned or built) beside it both across and along. Finished
    // before the walls either side of it, it never shuts a unit into its tile, as only a unit on a piece's own tile holds
    // it up. Nearest first among the corners, then among the rest.
    const BuildingMap pieces = buildingMap(w);
    auto corner = [&](ObjectId e) {
        const PropInfo* info = infoOf(w, e);
        if (!info || !(info->wallRule || info->door)) return false;
        const Transform* tr = w.get<Transform>(e);
        const int x = tileOf(tr->x), z = tileOf(tr->z), lv = storeyOf(w, e);
        auto wallAt = [&](int ax, int az) { return isWallOrDoor(w, mapGet(pieces.piece, levelKey(ax, az, lv))); };
        return (wallAt(x - 1, z) || wallAt(x + 1, z)) && (wallAt(x, z - 1) || wallAt(x, z + 1));
    };
    std::vector<std::pair<float, ObjectId>> wanted, rest;
    for (ObjectId e : w.view<Blueprint, Prop, Transform>()) {
        if (ownerOf(w, e) != faction) continue;
        const Vec2 at = approachOf(w, u, e);
        const float floors = (float)std::abs(workStorey(w, e) - storeyOf(w, u)) * 20.0f;
        const float d2 = (at.x - ut->x) * (at.x - ut->x) + (at.z - ut->z) * (at.z - ut->z) + floors * floors;
        (corner(e) ? wanted : rest).push_back({ d2, e });
    }
    std::sort(wanted.begin(), wanted.end());
    std::sort(rest.begin(), rest.end());
    wanted.insert(wanted.end(), rest.begin(), rest.end());
    // Blueprints on another floor than the unit's are left out when no ladders or stairs lead there (a few checks at most)
    {
        int checks = 0;
        std::vector<std::pair<float, ObjectId>> reachable;
        for (const auto& pe : wanted) {
            const int ws = workStorey(w, pe.second), us = storeyOf(w, u);
            if (ws == 0 && us == 0) {
                reachable.push_back(pe);
                continue;
            }
            if (gapTo(w, u, pe.second) <= BLUEPRINT_REACH) {
                reachable.push_back(pe);
                continue;
            }
            if (checks >= 12) continue;
            ++checks;
            Route r;
            if (planRoute(w, t, u, approachOf(w, u, pe.second), ws, r)) reachable.push_back(pe);
        }
        wanted.swap(reachable);
    }
    BuildJob job;
    bool hungry = false;
    for (const auto& [d2, e] : wanted) {
        const Blueprint* bp = w.get<Blueprint>(e);
        if (bp->supplied < materialsNeeded(w, e) || bp->retryIn > 0.0f) continue;
        job.phase = BuildJob::Phase::Build;
        job.target = e;
        break;
    }
    if (job.target == NO_OBJECT) {
        const std::vector<Claim> claims = claimsOf(w);
        for (const auto& [d2, e] : wanted) {
            if (w.get<Blueprint>(e)->supplied >= materialsNeeded(w, e)) continue;
            hungry = true;
            const int item = materialItem(materialOf(w, e));
            if (inv && carriedCount(*inv, item) > 0) {
                job.phase = BuildJob::Phase::Deliver;
                job.target = e;
                job.item = item;
                break;
            }
            ObjectId source = NO_OBJECT;
            float sourceD2 = 0.0f;
            for (ObjectId s : w.view<Storage, Prop, Transform>()) {
                const PropInfo& info = propInfo(w.get<Prop>(s)->kind);
                if (info.claimsLand || info.stall || w.has<Blueprint>(s) || lockShutFor(w, claims, s, u)) continue;
                const Transform* st = w.get<Transform>(s);
                if (ownerOf(w, s) != faction && !claimAt(claims, st->x, st->z, faction)) continue;
                const Storage* sto = w.get<Storage>(s);
                if (countInSlots(sto->slots.data(), (int)sto->slots.size(), item) <= 0) continue;
                const float sd2 = (st->x - ut->x) * (st->x - ut->x) + (st->z - ut->z) * (st->z - ut->z);
                if (source == NO_OBJECT || sd2 < sourceD2) {
                    source = s;
                    sourceD2 = sd2;
                }
            }
            if (source == NO_OBJECT) continue;
            job.phase = BuildJob::Phase::Fetch;
            job.target = e;
            job.source = source;
            job.item = item;
            break;
        }
    }
    if (job.target == NO_OBJECT) {
        r.why = hungry ? BehaviorWait::NoLogs : BehaviorWait::NoBlueprint;
        return false;
    }
    const ObjectId target = job.target;
    r.why = BehaviorWait::None;
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    b.busy = true;
    b.step = r;
    b.stepProp = target;
    const ObjectId first = job.phase == BuildJob::Phase::Fetch ? job.source : target;
    const float reach = job.phase == BuildJob::Phase::Fetch ? PROP_REACH : BLUEPRINT_REACH;
    const float gap = job.phase == BuildJob::Phase::Fetch ? flatGap(w, u, first) : gapTo(w, u, first);
    const int storey = job.phase == BuildJob::Phase::Fetch ? storeyOf(w, first) : workStorey(w, first);
    if (gap > reach) walkSoloTo(w, t, u, approachOf(w, u, first), storey);
    w.add(u, job);
    return true;
}

void buildJobSystem(World& w, Terrain& t, float dt) {
    for (ObjectId e : w.view<Blueprint>()) {
        Blueprint* bp = w.get<Blueprint>(e);
        bp->retryIn = std::max(0.0f, bp->retryIn - dt);
    }
    for (ObjectId u : w.view<BuildJob, Transform>()) {
        BuildJob& job = *w.get<BuildJob>(u);
        const int log = job.item;
        Behaviors* b = w.get<Behaviors>(u);
        auto end = [&](float cooldown, bool keepWalk = false) {
            w.remove<BuildJob>(u);
            if (!keepWalk) {
                w.remove<SoloMove>(u);
                w.remove<Route>(u);
            }
            if (!b) return;
            const BehaviorKind kind = b->step.kind;
            b->busy = false;
            if (BehaviorRule* r = ruleOf(*b, kind)) r->cooldown = std::max(r->cooldown, cooldown);
        };
        if (!b || outCold(w, u) || w.has<Carried>(u)) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        if (!w.has<Blueprint>(job.target)) {
            end(0.0f);
            continue;
        }
        const ObjectId goal = job.phase == BuildJob::Phase::Fetch ? job.source : job.target;
        const float reach = job.phase == BuildJob::Phase::Fetch ? PROP_REACH + 0.3f : BLUEPRINT_REACH;
        const float gap = job.phase == BuildJob::Phase::Fetch ? flatGap(w, u, goal) : gapTo(w, u, goal);
        if (gap > reach) {
            if (!walking(w, u)) {
                job.stuck += dt;
                const int storey = job.phase == BuildJob::Phase::Fetch ? storeyOf(w, goal) : workStorey(w, goal);
                if (job.stuck > BUILD_GIVE_UP_SECONDS) end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                else if (job.stuck > 1.0f && job.stuck - dt <= 1.0f) walkSoloTo(w, t, u, approachOf(w, u, goal), storey);
            }
            continue;
        }
        if (w.has<Climbing>(u)) continue;
        job.stuck = 0.0f;
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
        Inventory* inv = w.get<Inventory>(u);
        switch (job.phase) {
        case BuildJob::Phase::Fetch: {
            Storage* s = w.get<Storage>(job.source);
            const Blueprint* bp = w.get<Blueprint>(job.target);
            if (!s || !inv) {
                end(BEHAVIOR_RETRY_SECONDS);
                break;
            }
            const ItemDef* def = findItem(log);
            const float each = def ? std::max(0.01f, def->weight) : 1.0f;
            const float spare = ENCUMBERED_SHARE * carryLimit(statsOf(w, u)) - carriedWeightOf(w, u);
            const int byWeight = std::max(0, (int)std::floor(spare / each));
            const int want = std::min({ materialsNeeded(w, job.target) - bp->supplied, byWeight,
                                        slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, log) });
            const int took = takeFromSlots(s->slots.data(), (int)s->slots.size(), log, std::max(0, want));
            if (took <= 0) {
                end(BEHAVIOR_RETRY_SECONDS);
                break;
            }
            addToInventory(*inv, log, took);
            queueItemMove(w, job.source, u, log, took);
            queueSoundFrom(w, Sfx::ItemMove, job.source);
            job.phase = BuildJob::Phase::Deliver;
            if (gapTo(w, u, job.target) > BLUEPRINT_REACH) walkSoloTo(w, t, u, approachOf(w, u, job.target), workStorey(w, job.target));
            break;
        }
        case BuildJob::Phase::Deliver: {
            const int carried = inv ? carriedCount(*inv, log) : 0;
            const int took = supplyBlueprint(w, job.target, carried);
            if (took > 0) {
                takeFromSlots(inv->slots.data(), INVENTORY_SLOTS, log, took);
                queueItemMove(w, u, job.target, log, took);
                queueSoundFrom(w, Sfx::ItemMove, job.target);
            }
            end(0.0f);
            break;
        }
        case BuildJob::Phase::Build: {
            Blueprint* bp = w.get<Blueprint>(job.target);
            if (bp->supplied < materialsNeeded(w, job.target) || bp->retryIn > 0.0f) {
                end(0.0f);
                break;
            }
            // The materials brought to it are spent the moment building begins
            if (bp->progress <= 0.0) queueSoundFrom(w, Sfx::ItemMove, job.target);
            bp->progress += (double)dt * carpentryPace(w, u) / buildSecondsNeeded(w, job.target);
            if (bp->progress < 1.0) break;
            bp->progress = 1.0;
            // A unit in the way (the builder itself, too, when it stands beside it): it is asked to move, and this one
            // goes on to something else meanwhile, keeping any walk away it was just given
            const bool built = finishBlueprint(w, t, job.target);
            end(built ? 0.0f : BLOCKED_RETRY_SECONDS, !built);
            break;
        }
        }
    }
}

void buildingSystem(World& w, Terrain& t, float dt) {
    buildJobSystem(w, t, dt);
    climbSystem(w, t, dt);
    routeSystem(w, t, dt);
    doorSystem(w, dt);
    if (w.resource<BuildingsChanged>().dirty) rebuildBlocking(w, t);
    else refreshBuildingTiles(w, t);
}
