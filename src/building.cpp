#include "building.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include "airship.h"
#include "experience.h"
#include "factions.h"
#include "hostility.h"
#include "inventory.h"
#include "items.h"
#include "systems_internal.h"

using namespace systems_detail;

namespace {
int tileOf(float v) { return (int)std::floor(v); }

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

// Takes up to count of item out of the slots: how many it took
int takeItems(ItemStack* slots, int slotCount, int item, int count) {
    int took = 0;
    for (int i = 0; i < slotCount && took < count; ++i) {
        ItemStack& s = slots[i];
        if (s.item != item || s.count <= 0) continue;
        const int n = std::min(s.count, count - took);
        s.count -= n;
        took += n;
        if (s.count <= 0) s = ItemStack{};
    }
    return took;
}

int countItems(const ItemStack* slots, int slotCount, int item) {
    int n = 0;
    for (int i = 0; i < slotCount; ++i) n += slots[i].item == item ? slots[i].count : 0;
    return n;
}

// How far u is from the piece: from a foundation's rectangle, or the edge of a piece's tile
float gapTo(World& w, ObjectId u, ObjectId prop) {
    const Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    if (!ut || !pt) return 1e9f;
    if (const Foundation* f = w.get<Foundation>(prop)) {
        const float cx = std::clamp(ut->x, (float)f->x0, (float)(f->x0 + f->w));
        const float cz = std::clamp(ut->z, (float)f->z0, (float)(f->z0 + f->h));
        return std::hypot(ut->x - cx, ut->z - cz);
    }
    return std::max(0.0f, std::hypot(ut->x - pt->x, ut->z - pt->z) - 0.5f);
}

// Where to walk to reach the piece: a foundation's nearest point, or the piece itself
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
    const SoloMove* so = w.get<SoloMove>(u);
    return so && so->next < so->waypoints.size();
}

BehaviorRule* ruleOf(Behaviors& b, BehaviorKind kind) {
    for (BehaviorRule& r : b.rules) {
        if (r.kind == kind) return &r;
    }
    return nullptr;
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
    return i.foundation || i.wallRule || i.door;
}

BuildMaterial materialOf(World& w, ObjectId prop) {
    const Material* m = w.get<Material>(prop);
    return m ? m->kind : BuildMaterial::Wood;
}

int materialItem(BuildMaterial m) {
    static const int iron = itemTagged(ITEM_TAG_IRON_INGOT), crystal = itemTagged(ITEM_TAG_DRAGON_CRYSTAL);
    switch (m) {
    case BuildMaterial::Iron: return iron;
    case BuildMaterial::Crystal: return crystal;
    default: return woodLogItem();
    }
}

double materialToughness(BuildMaterial m) {
    return m == BuildMaterial::Crystal ? 3.0 : m == BuildMaterial::Iron ? 2.0 : 1.0;
}

const char* materialNameKey(BuildMaterial m) {
    return m == BuildMaterial::Crystal ? "UILABEL_MATERIAL_CRYSTAL" : m == BuildMaterial::Iron ? "UILABEL_MATERIAL_IRON" : "UILABEL_MATERIAL_WOOD";
}

bool isBuildingPiece(PropKind kind) {
    const PropInfo& i = propInfo(kind);
    return i.foundation || i.onFoundation;
}

int materialsNeeded(World& w, ObjectId prop) {
    const PropInfo* info = infoOf(w, prop);
    if (!info || info->materials <= 0.0) return 0;
    return std::max(1, (int)std::ceil(info->materials - 1e-9));
}

double buildSecondsNeeded(World& w, ObjectId prop) {
    const PropInfo* info = infoOf(w, prop);
    if (!info) return 1.0;
    double tiles = 1.0;
    if (const Foundation* f = w.get<Foundation>(prop)) tiles = (double)f->w * (double)f->h;
    return std::max(0.5, info->buildSeconds * tiles);
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
}

ObjectId foundationAt(World& w, int x, int z) {
    for (ObjectId e : w.view<Foundation>()) {
        if (w.get<Foundation>(e)->contains(x, z)) return e;
    }
    return NO_OBJECT;
}

ObjectId groundPieceAt(World& w, int x, int z) {
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (!info.onFoundation || info.upperFloor) continue;
        const Transform* tr = w.get<Transform>(e);
        if (tileOf(tr->x) == x && tileOf(tr->z) == z) return e;
    }
    return NO_OBJECT;
}

namespace {
ObjectId floorPieceAt(World& w, int x, int z, bool finishedOnly) {
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (!propInfo(w.get<Prop>(e)->kind).upperFloor || (finishedOnly && w.has<Blueprint>(e))) continue;
        const Transform* tr = w.get<Transform>(e);
        if (tileOf(tr->x) == x && tileOf(tr->z) == z) return e;
    }
    return NO_OBJECT;
}
}

ObjectId upperFloorAt(World& w, int x, int z) { return floorPieceAt(w, x, z, true); }

PieceCheck checkFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner) {
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    const int fw = x1 - x0 + 1, fh = z1 - z0 + 1;
    if (fw < FOUNDATION_MIN || fh < FOUNDATION_MIN) return PieceCheck::TooSmall;
    if (fw > FOUNDATION_MAX || fh > FOUNDATION_MAX) return PieceCheck::TooBig;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (!t.walkable(x, z)) return PieceCheck::Unwalkable;
        }
    }
    // Its middle in the builder's claim, and none of it in anyone else's
    const std::vector<Claim> claims = claimsOf(w);
    const float cx = (x0 + x1 + 1) * 0.5f, cz = (z0 + z1 + 1) * 0.5f;
    if (!claimAt(claims, cx, cz, owner)) return PieceCheck::OutsideClaim;
    for (const float px : { (float)x0, (float)x1 + 1.0f }) {
        for (const float pz : { (float)z0, (float)z1 + 1.0f }) {
            const Claim* c = claimAt(claims, px, pz);
            if (c && c->owner != owner) return PieceCheck::OutsideClaim;
        }
    }
    for (ObjectId e : w.view<Foundation>()) {
        const Foundation* f = w.get<Foundation>(e);
        if (x0 < f->x0 + f->w && f->x0 <= x1 && z0 < f->z0 + f->h && f->z0 <= z1) return PieceCheck::Blocked;
    }
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (propInfo(w.get<Prop>(e)->kind).airship) continue;
        const Transform* tr = w.get<Transform>(e);
        const int px = tileOf(tr->x), pz = tileOf(tr->z);
        if (px >= x0 && px <= x1 && pz >= z0 && pz <= z1) return PieceCheck::Blocked;
    }
    return PieceCheck::Ok;
}

ObjectId placeFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner, BuildMaterial material) {
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    const float cx = (x0 + x1 + 1) * 0.5f, cz = (z0 + z1 + 1) * 0.5f;
    const ObjectId e = placeBuiltProp(w, t, PropKind::Foundation, cx, cz, 0.0f, owner);
    w.add(e, Foundation{ x0, z0, x1 - x0 + 1, z1 - z0 + 1, {} });
    w.add(e, Blueprint{});
    if (material != BuildMaterial::Wood) w.add(e, Material{ material });
    return e;
}

PieceCheck checkPiece(World& w, PropKind kind, int x, int z, int owner) {
    const PropInfo& info = propInfo(kind);
    const ObjectId f = foundationAt(w, x, z);
    if (f == NO_OBJECT || ownerOf(w, f) != owner) return PieceCheck::NeedsFoundation;
    if (w.has<Blueprint>(f)) return PieceCheck::FoundationUnfinished;
    if ((info.upperFloor || info.ladder) && !edgesClosed(w, f)) return PieceCheck::EdgesOpen;
    if (info.upperFloor) {
        if (floorPieceAt(w, x, z, false) != NO_OBJECT) return PieceCheck::Taken;
        const ObjectId under = groundPieceAt(w, x, z);
        if (under != NO_OBJECT && propInfo(w.get<Prop>(under)->kind).ladder) return PieceCheck::Taken;
        return PieceCheck::Ok;
    }
    if (groundPieceAt(w, x, z) != NO_OBJECT) return PieceCheck::Taken;
    if (info.ladder && floorPieceAt(w, x, z, false) != NO_OBJECT) return PieceCheck::Taken;
    return PieceCheck::Ok;
}

bool edgesClosed(World& w, ObjectId foundation) {
    const Foundation* f = w.get<Foundation>(foundation);
    if (!f) return false;
    for (int z = f->z0; z < f->z0 + f->h; ++z) {
        for (int x = f->x0; x < f->x0 + f->w; ++x) {
            if (!f->onEdge(x, z)) continue;
            const ObjectId p = groundPieceAt(w, x, z);
            const PropInfo* info = p != NO_OBJECT ? infoOf(w, p) : nullptr;
            if (!info || (!info->wallRule && !info->door)) return false;
        }
    }
    return true;
}

void collapseCheck(World& w) {
    for (ObjectId fe : w.view<Foundation>()) {
        const Foundation f = *w.get<Foundation>(fe);
        std::vector<ObjectId> floors;
        int standing = 0;
        for (ObjectId e : w.view<Prop, Transform>()) {
            const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
            const Transform* tr = w.get<Transform>(e);
            if (!f.contains(tileOf(tr->x), tileOf(tr->z))) continue;
            if (info.upperFloor) floors.push_back(e);
            else if ((info.wallRule || info.door) && !w.has<Blueprint>(e)) ++standing;
        }
        if (standing > 0 || floors.empty()) continue;
        bool finishedFloor = false;
        for (ObjectId e : floors) finishedFloor = finishedFloor || !w.has<Blueprint>(e);
        if (!finishedFloor) continue;
        for (ObjectId e : floors) {
            releasePropLinks(w, e);
            w.destroy(e);
        }
        queueLog(w, lang("MISC_FLOORS_COLLAPSE"), LogKind::Seen);
    }
}

ObjectId placePiece(World& w, const Terrain& t, PropKind kind, int x, int z, int owner, BuildMaterial material) {
    const PropInfo& info = propInfo(kind);
    float yaw = 0.0f;
    if (info.door) {
        // Across the doorway: along the edge it is on
        const ObjectId fe = foundationAt(w, x, z);
        const Foundation* f = fe != NO_OBJECT ? w.get<Foundation>(fe) : nullptr;
        if (f && (x == f->x0 || x == f->x0 + f->w - 1)) yaw = 1.5707963f;
    }
    const ObjectId e = placeBuiltProp(w, t, kind, x + 0.5f, z + 0.5f, yaw, owner);
    if (info.upperFloor) {
        if (Transform* tr = w.get<Transform>(e)) tr->y += FLOOR_HEIGHT;
    }
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
    // Units standing where it goes are asked to step off it, and it waits
    std::vector<ObjectId> inTheWay;
    if (!info->upperFloor) {
        for (ObjectId u : w.view<Entity, Transform>()) {
            if (w.has<Corpse>(u) || outCold(w, u) || w.has<Carried>(u) || w.has<Upstairs>(u) || w.has<Occupying>(u)) continue;
            const Transform* ut = w.get<Transform>(u);
            const float r = w.get<Entity>(u)->radius * 0.8f;
            if (ut->x + r > x0 && ut->x - r < x1 + 1 && ut->z + r > z0 && ut->z - r < z1 + 1) inTheWay.push_back(u);
        }
    }
    if (!inTheWay.empty()) {
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
                        if ((x >= x0 && x <= x1 && z >= z0 && z <= z1) || !t.walkable(x, z)) continue;
                        leaveSquad(w, u);
                        w.remove<IdleSpot>(u);
                        walkSolo(w, t, u, Vec2{ x + 0.5f, z + 0.5f });
                        moved = true;
                    }
                }
            }
        }
        if (Blueprint* bp = w.get<Blueprint>(prop)) bp->retryIn = BLOCKED_RETRY_SECONDS;
        return false;
    }
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
    auto key = [](int x, int z) { return (long long)z * 1000003LL + (long long)x; };
    std::unordered_set<long long> changed;
    int x0 = tiles.front().first, x1 = x0, z0 = tiles.front().second, z1 = z0;
    for (const auto& [x, z] : tiles) {
        changed.insert(key(x, z));
        t.setBlocked(x, z, false);
        t.setDeck(x, z, false);
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        z0 = std::min(z0, z); z1 = std::max(z1, z);
    }
    // Only the changed tiles are worked out again: a finished wall closes its tile, a finished foundation decks its own
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (w.has<Blueprint>(e)) continue;
        if (const Foundation* f = w.get<Foundation>(e)) {
            if (f->x0 > x1 || f->x0 + f->w - 1 < x0 || f->z0 > z1 || f->z0 + f->h - 1 < z0) continue;
            for (const auto& [x, z] : tiles) {
                if (f->contains(x, z)) t.setDeck(x, z, true);
            }
            continue;
        }
        if (!propInfo(w.get<Prop>(e)->kind).wallRule) continue;
        const Transform* tr = w.get<Transform>(e);
        if (changed.count(key(tileOf(tr->x), tileOf(tr->z)))) t.setBlocked(tileOf(tr->x), tileOf(tr->z), true);
    }
    // Walks that pass near a changed tile are planned again, around a new wall or through a gap left by a lost one
    for (ObjectId u : w.view<SoloMove, Transform>()) {
        if (w.has<Upstairs>(u)) continue;
        SoloMove* so = w.get<SoloMove>(u);
        const Transform* ut = w.get<Transform>(u);
        if (so->next >= so->waypoints.size() || !routeNear(Vec2{ ut->x, ut->z }, so->waypoints, so->next, tiles)) continue;
        planSolo(w, t, u, so->waypoints.back());
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
        if (changed.count(key(tileOf(ut->x), tileOf(ut->z)))) ut->y = t.standHeight(ut->x, ut->z);
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
    const int x = tileOf(tr->x), z = tileOf(tr->z);
    auto joins = [&](int nx, int nz) {
        const ObjectId p = groundPieceAt(w, nx, nz);
        const PropInfo* info = p != NO_OBJECT ? infoOf(w, p) : nullptr;
        return info && (info->wallRule || info->door);
    };
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

bool isUpstairs(World& w, ObjectId u) { return w.has<Upstairs>(u); }

Vec2 nearestFloorSpot(World& w, ObjectId foundation, Vec2 target) {
    const Foundation* f = w.get<Foundation>(foundation);
    Vec2 best = target;
    float bestD2 = -1.0f;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (!propInfo(w.get<Prop>(e)->kind).upperFloor || w.has<Blueprint>(e)) continue;
        const Transform* tr = w.get<Transform>(e);
        if (f && !f->contains(tileOf(tr->x), tileOf(tr->z))) continue;
        const float d2 = (tr->x - target.x) * (tr->x - target.x) + (tr->z - target.z) * (tr->z - target.z);
        if (bestD2 < 0.0f || d2 < bestD2) {
            bestD2 = d2;
            best = Vec2{ tr->x, tr->z };
        }
    }
    return best;
}

bool canClimbUp(World& w, ObjectId ladder) {
    const PropInfo* info = infoOf(w, ladder);
    const Transform* tr = w.get<Transform>(ladder);
    if (!info || !info->ladder || !tr || w.has<Blueprint>(ladder)) return false;
    const int x = tileOf(tr->x), z = tileOf(tr->z);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (upperFloorAt(w, x + dx, z + dz) != NO_OBJECT) return true;
        }
    }
    return false;
}

bool startClimb(World& w, ObjectId u, ObjectId ladder) {
    const Transform* lt = w.get<Transform>(ladder);
    const Transform* ut = w.get<Transform>(u);
    if (!lt || !ut || w.has<Climbing>(u) || outCold(w, u)) return false;
    if (const Upstairs* up = w.get<Upstairs>(u)) {
        if (up->foundation != foundationAt(w, tileOf(lt->x), tileOf(lt->z))) return false;
        if (std::hypot(ut->x - lt->x, ut->z - lt->z) > PROP_REACH + 0.1f) return false;
        w.remove<SoloMove>(u);
        w.add(u, Climbing{ ladder, CLIMB_SECONDS, true });
        return true;
    }
    if (!canClimbUp(w, ladder) || flatGap(w, u, ladder) > PROP_REACH) return false;
    if (w.has<Carrying>(u)) putDown(w, u);
    leaveSquad(w, u);
    w.remove<SoloMove>(u);
    w.add(u, Climbing{ ladder, CLIMB_SECONDS, false });
    return true;
}

void moveUpstairs(World& w, ObjectId u, Vec2 target) {
    const Upstairs* up = w.get<Upstairs>(u);
    if (!up) return;
    const Vec2 spot = nearestFloorSpot(w, up->foundation, target);
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    w.add(u, SoloMove{ { spot }, 0 });
}

void climbSystem(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<Climbing, Transform>()) {
        Climbing& c = *w.get<Climbing>(u);
        const Transform* lt = w.get<Transform>(c.ladder);
        if (!lt || outCold(w, u) || w.has<Carried>(u)) {
            w.remove<Climbing>(u);
            continue;
        }
        c.left -= dt;
        if (c.left > 0.0f) continue;
        const bool down = c.down;
        const ObjectId ladder = c.ladder;
        w.remove<Climbing>(u);
        Transform* ut = w.get<Transform>(u);
        w.remove<IdleSpot>(u);
        w.remove<SoloMove>(u);
        if (down) {
            w.remove<Upstairs>(u);
            ut->x = lt->x;
            ut->z = lt->z;
            ut->y = t.standHeight(ut->x, ut->z);
        } else {
            const ObjectId f = foundationAt(w, tileOf(lt->x), tileOf(lt->z));
            const Vec2 spot = nearestFloorSpot(w, f, Vec2{ lt->x, lt->z });
            ut->x = spot.x;
            ut->z = spot.z;
            ut->y = t.heightAt(spot.x, spot.z) + FLOOR_TOP;
            w.add(u, Upstairs{ f });
        }
        queueSoundFrom(w, Sfx::Occupy, ladder);
    }
    // Upstairs with no floor under it any more: it comes down
    for (ObjectId u : w.view<Upstairs, Transform>()) {
        Transform* ut = w.get<Transform>(u);
        const Upstairs* up = w.get<Upstairs>(u);
        if (w.has<Foundation>(up->foundation) && upperFloorAt(w, tileOf(ut->x), tileOf(ut->z)) != NO_OBJECT) continue;
        w.remove<Upstairs>(u);
        ut->y = t.standHeight(ut->x, ut->z);
    }
}

void foundationLost(World& w, ObjectId foundation) {
    const Foundation* f = w.get<Foundation>(foundation);
    if (!f) return;
    const Foundation keep = *f;
    std::vector<ObjectId> pieces;
    for (ObjectId e : w.view<Prop, Transform>()) {
        if (e == foundation || !propInfo(w.get<Prop>(e)->kind).onFoundation) continue;
        const Transform* tr = w.get<Transform>(e);
        if (keep.contains(tileOf(tr->x), tileOf(tr->z))) pieces.push_back(e);
    }
    for (ObjectId e : pieces) {
        const Transform at = *w.get<Transform>(e);
        int logs = 0;
        if (const Blueprint* bp = w.get<Blueprint>(e); bp && bp->progress <= 0.0) logs = bp->supplied;
        const int material = materialItem(materialOf(w, e));
        releasePropLinks(w, e);
        w.destroy(e);
        if (logs > 0) spawnGroundItem(w, Transform{ at.x, at.y, at.z }, material, logs);
    }
    for (ObjectId u : w.view<Upstairs>()) {
        if (w.get<Upstairs>(u)->foundation == foundation) w.remove<Upstairs>(u);
    }
    buildingTilesChanged(w, keep.x0, keep.z0, keep.x0 + keep.w - 1, keep.z0 + keep.h - 1);
}

bool startBuildStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    const Transform* ut = w.get<Transform>(u);
    Inventory* inv = w.get<Inventory>(u);
    if (!ut || w.has<Mob>(u) || w.has<Upstairs>(u)) return false;
    const int faction = factionOf(w, u);
    // Auto-Construct: build the nearest blueprint of its faction that has everything it needs; failing that, bring the
    // nearest one that still wants materials what it wants, carried or fetched from a store of its faction
    std::vector<std::pair<float, ObjectId>> wanted;
    for (ObjectId e : w.view<Blueprint, Prop, Transform>()) {
        if (ownerOf(w, e) != faction) continue;
        const Vec2 at = approachOf(w, u, e);
        wanted.push_back({ (at.x - ut->x) * (at.x - ut->x) + (at.z - ut->z) * (at.z - ut->z), e });
    }
    std::sort(wanted.begin(), wanted.end());
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
                if (info.claimsLand || w.has<Blueprint>(s) || lockShutFor(w, claims, s, u)) continue;
                const Transform* st = w.get<Transform>(s);
                if (ownerOf(w, s) != faction && !claimAt(claims, st->x, st->z, faction)) continue;
                if (countItems(w.get<Storage>(s)->slots.data(), STORAGE_SLOTS, item) <= 0) continue;
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
    if (gap > reach) walkSolo(w, t, u, approachOf(w, u, first));
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
        auto end = [&](float cooldown) {
            w.remove<BuildJob>(u);
            w.remove<SoloMove>(u);
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
                if (job.stuck > BUILD_GIVE_UP_SECONDS) end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                else if (job.stuck > 1.0f && job.stuck - dt <= 1.0f) walkSolo(w, t, u, approachOf(w, u, goal));
            }
            continue;
        }
        job.stuck = 0.0f;
        w.remove<SoloMove>(u);
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
            const int took = takeItems(s->slots.data(), STORAGE_SLOTS, log, std::max(0, want));
            if (took <= 0) {
                end(BEHAVIOR_RETRY_SECONDS);
                break;
            }
            addToInventory(*inv, log, took);
            queueSoundFrom(w, Sfx::ItemMove, job.source);
            job.phase = BuildJob::Phase::Deliver;
            if (gapTo(w, u, job.target) > BLUEPRINT_REACH) walkSolo(w, t, u, approachOf(w, u, job.target));
            break;
        }
        case BuildJob::Phase::Deliver: {
            const int carried = inv ? carriedCount(*inv, log) : 0;
            const int took = supplyBlueprint(w, job.target, carried);
            if (took > 0) {
                takeItems(inv->slots.data(), INVENTORY_SLOTS, log, took);
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
            // A unit in the way: it is asked to move, and this one goes on to something else meanwhile
            end(finishBlueprint(w, t, job.target) ? 0.0f : BLOCKED_RETRY_SECONDS);
            break;
        }
        }
    }
}

void buildingSystem(World& w, Terrain& t, float dt) {
    buildJobSystem(w, t, dt);
    climbSystem(w, t, dt);
    doorSystem(w, dt);
    if (w.resource<BuildingsChanged>().dirty) rebuildBlocking(w, t);
    else refreshBuildingTiles(w, t);
}
