#include "build.h"
#include "building.h"
#include "prop_index.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float SAMPLE_STEP = 0.125f;

template <typename Pred>
bool anyFootSample(const Footprint& f, float cx, float cz, float yaw, Pred pred) {
    const float reach = footprintReach(f);
    const int n = (int)std::ceil(2.0f * reach / SAMPLE_STEP);
    for (int i = 0; i < n; ++i) {
        const float dx = -reach + (i + 0.5f) * SAMPLE_STEP;
        for (int j = 0; j < n; ++j) {
            const float dz = -reach + (j + 0.5f) * SAMPLE_STEP;
            if (!insideFootprint(f, dx, dz, yaw)) continue;
            if (pred(cx + dx, cz + dz)) return true;
        }
    }
    return false;
}

template <typename Pred>
bool allFootSamples(const Footprint& f, float cx, float cz, float yaw, Pred pred) {
    return !anyFootSample(f, cx, cz, yaw, [&](float x, float z) { return !pred(x, z); });
}

bool footprintsOverlap(const Footprint& fa, float ax, float az, float ayaw,
                       const Footprint& fb, float bx, float bz, float byaw) {
    const float reach = footprintReach(fa) + footprintReach(fb);
    if (distSq(ax, az, bx, bz) > reach * reach) return false;
    return anyFootSample(fa, ax, az, ayaw, [&](float x, float z) {
        return insideFootprint(fb, x - bx, z - bz, byaw);
    });
}

float snapExtent(const Footprint& f) {
    return f.radius > 0.0f ? f.radius : std::max(f.halfW, f.halfD);
}

float snapCoord(float g, float halfExtent) {
    const int twice = (int)std::lround(2.0f * halfExtent);
    return (twice % 2 != 0) ? std::floor(g) + 0.5f : std::round(g);
}
}

float wallDecorOut(const PropInfo& info) { return 0.15f + info.mountDepth * 0.5f + 0.005f; }

ObjectId wallTileAt(World& w, int tx, int tz, int storey) {
    const BuildingMap& m = cachedBuildingMap(w);
    const auto it = m.piece.find(levelKey(tx, tz, storey));
    if (it == m.piece.end()) return NO_OBJECT;
    const Prop* p = w.get<Prop>(it->second);
    return p && propInfo(p->kind).wallRule ? it->second : NO_OBJECT;
}

bool wallSideBare(World& w, int tx, int tz, int storey, int side) {
    const BuildingMap& m = cachedBuildingMap(w);
    const auto it = m.piece.find(levelKey(tx + SIDE_DX[side & 3], tz + SIDE_DZ[side & 3], storey));
    if (it == m.piece.end()) return true;
    const Prop* p = w.get<Prop>(it->second);
    return !p || !(propInfo(p->kind).wallRule || propInfo(p->kind).door);
}

void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z) {
    if (propInfo(kind).wallDecor) {
        // The middle of the tile under the pointer: onPropSpawned moves it out to the side it faces
        x = std::floor(gx) + 0.5f;
        z = std::floor(gz) + 0.5f;
        return;
    }
    const float h = snapExtent(propInfo(kind).foot);
    x = snapCoord(gx, h);
    z = snapCoord(gz, h);
}

BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z, int storey, int builder,
                          const std::vector<Claim>* given, float yaw, const std::vector<ObjectId>* nearby) {
    const Footprint& f = propInfo(kind).foot;

    if (propInfo(kind).wallDecor) {
        // Hung on a wall: a finished wall of the same floor on this tile, and no other prop of the kind on the same side of it
        const int tx = tileOf(x), tz = tileOf(z);
        const ObjectId wall = wallTileAt(w, tx, tz, storey);
        if (wall == NO_OBJECT || w.has<Blueprint>(wall)) return BuildCheck::NoWall;
        if (!wallSideBare(w, tx, tz, storey, dirOfYaw(yaw))) return BuildCheck::WallBetween;
        if (const Claim* held = claimAt(given ? *given : claimsOf(w), x, z); held && held->owner != builder) return BuildCheck::EnemySealed;
        std::vector<ObjectId> found;
        if (!nearby) {
            propsNear(w, x, z, 1.5f, found);
            nearby = &found;
        }
        for (ObjectId e : *nearby) {
            const Prop* p = w.get<Prop>(e);
            const Transform* tr = w.get<Transform>(e);
            if (!p || !tr || !propInfo(p->kind).wallDecor || tileOf(tr->x) != tx || tileOf(tr->z) != tz) continue;
            if (storeyOf(w, e) == storey && dirOfYaw(p->yaw) == dirOfYaw(yaw)) return BuildCheck::Blocked;
        }
        return BuildCheck::Ok;
    }

    // On an upper floor, every bit of it must stand on a finished floor of yours on that level
    auto standable = [&](float px, float pz) {
        const int tx = (int)std::floor(px), tz = (int)std::floor(pz);
        if (storey > 0) return floorSurfaceAt(w, tx, tz, storey, builder);
        return t.walkable(tx, tz);
    };
    // A Perimeter piece may span gaps in the ground (water): one tile of it on ground is enough
    const bool ground = propInfo(kind).perimeter && storey == 0 ? anyFootSample(f, x, z, yaw, standable)
                                                                : allFootSamples(f, x, z, yaw, standable);
    if (!ground) return storey > 0 ? BuildCheck::NoFloor : BuildCheck::Unwalkable;
    if (storey > 0 && propInfo(kind).claimsLand) return BuildCheck::NoFloor;
    // An outdoor prop: on the ground, off every building's foundation
    if (propInfo(kind).exterior) {
        if (storey > 0) return BuildCheck::Indoors;
        const bool outdoors = allFootSamples(f, x, z, yaw, [&](float px, float pz) {
            return foundationAt(w, (int)std::floor(px), (int)std::floor(pz)) == NO_OBJECT;
        });
        if (!outdoors) return BuildCheck::Indoors;
    }
    // A Perimeter Wall or Gate: not on or directly beside a foundation
    if (propInfo(kind).perimeter) {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        propTileRect(propInfo(kind), x, z, yaw, x0, z0, x1, z1);
        if (touchesFoundation(w, x0, z0, x1, z1)) return BuildCheck::BesideFoundation;
    }

    std::vector<Claim> own;
    if (!given) own = claimsOf(w);
    const std::vector<Claim>& claims = given ? *given : own;

    if (const Claim* held = claimAt(claims, x, z); held && held->owner != builder) return BuildCheck::EnemySealed;
    // A Bedroll: only by a Campfire, or in your Altar's claim
    if (propInfo(kind).needsCamp && !campSheltered(w, claims, x, z, builder)) return BuildCheck::NoCamp;

    if (propInfo(kind).claimsLand) {
        for (const Claim& c : claims) {
            if (c.owner == builder && !c.camp) return BuildCheck::AltarTaken;
            // A camp's land gives way to an Altar's: the Altar may stand over it
            if (!c.camp && claimsOverlap(x, z, CLAIM_RADIUS, c.x, c.z, c.radius)) return BuildCheck::ClaimOverlap;
        }
    }
    // A Campfire's claim may not share land with another camp's, or with an Altar that is not the builder's own
    if (kind == PropKind::Campfire) {
        for (const Claim& c : claims) {
            if (!c.camp && c.owner == builder) continue;
            if (claimsOverlap(x, z, CAMP_CLAIM_RADIUS, c.x, c.z, c.radius)) return BuildCheck::ClaimOverlap;
        }
    }

    // Only the props within reach of this one's footprint can overlap it (prop_index.h)
    std::vector<ObjectId> found;
    if (!nearby) {
        propsNear(w, x, z, footprintReach(f) + propIndex(w).widestReach, found);
        nearby = &found;
    }
    for (ObjectId e : *nearby) {
        const Prop* p = w.get<Prop>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!p || !tr) continue;
        const PropInfo& other = propInfo(p->kind);
        // Floors (a foundation's tiles, upper floors) are stood on, not in the way; things on other floors don't count
        if (other.foundation || other.upperFloor || storeyOf(w, e) != storey) continue;
        if (footprintsOverlap(f, x, z, yaw, other.foot, tr->x, tr->z, p->yaw)) return BuildCheck::Blocked;
    }
    return BuildCheck::Ok;
}

bool buildAnywhere(const BuildPick& pick) {
    return pick.tool == BuildPick::Tool::Destroy || pick.adjusting() || pick.areaTool() || (pick.placing() && propInfo(pick.kind).buildAnywhere);
}

bool selectionInClaim(World& w) {
    return selectionInClaim(w, claimsOf(w));
}

bool selectionInClaim(World& w, const std::vector<Claim>& claims) {
    for (ObjectId u : w.view<Entity, Selected, Commandable, Transform>()) {
        const Transform* tr = w.get<Transform>(u);
        if (claimAt(claims, tr->x, tr->z, FACTION_PLAYER)) return true;
    }
    return false;
}

const char* buildRefusalKey(BuildCheck c) {
    switch (c) {
    case BuildCheck::Blocked: return "MISC_BUILD_BLOCKED";
    case BuildCheck::Unwalkable: return "MISC_BUILD_UNWALKABLE";
    case BuildCheck::EnemySealed: return "MISC_BUILD_SEALED";
    case BuildCheck::ClaimOverlap: return "MISC_BUILD_CLAIM_OVERLAP";
    case BuildCheck::AltarTaken: return "MISC_BUILD_ALTAR_TAKEN";
    case BuildCheck::NoCamp: return "MISC_BUILD_NEEDS_CAMP";
    case BuildCheck::NoFloor: return "MISC_BUILD_NO_FLOOR";
    case BuildCheck::Indoors: return "MISC_BUILD_OUTDOORS_ONLY";
    case BuildCheck::BesideFoundation: return "MISC_BUILD_NEAR_FOUNDATION";
    case BuildCheck::NoWall: return "MISC_BUILD_NEEDS_WALL";
    case BuildCheck::WallBetween: return "MISC_BUILD_WALL_BETWEEN";
    case BuildCheck::Ok: break;
    }
    return "";
}

void onPropSpawned(World& w, ObjectId e) {
    Prop* p = w.get<Prop>(e);
    Transform* at = w.get<Transform>(e);
    if (!p || !at) return;
    const PropInfo& info = propInfo(p->kind);
    if (info.wallDecor) {
        // Out to the side of its tile that it faces, against the wall's face (the same wherever it is placed from)
        p->yaw = (float)dirOfYaw(p->yaw) * 1.5707963f;
        at->x = std::floor(at->x) + 0.5f + std::sin(p->yaw) * wallDecorOut(info);
        at->z = std::floor(at->z) + 0.5f + std::cos(p->yaw) * wallDecorOut(info);
    }
    if (info.randomYaw) {
        // A way of its own to face, the same each time for the spot it stands on
        std::uint32_t h = (std::uint32_t)(std::int32_t)std::floor(at->x * 4.0f) * 73856093u ^
                          (std::uint32_t)(std::int32_t)std::floor(at->z * 4.0f) * 19349663u ^ ((std::uint32_t)idx(p->kind) + 1u) * 83492791u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        p->yaw = (float)(h % 3600u) / 3600.0f * 6.2831853f;
    }
}

ObjectId makeProp(World& w, PropKind kind, const Transform& at, float yaw) {
    const PropInfo& info = propInfo(kind);
    const ObjectId e = w.create();
    w.add(e, at);
    w.add(e, Prop{ kind, yaw });
    if (hasStorage(info)) {
        Storage st;
        st.slots.assign((size_t)std::max(1, info.slots), ItemStack{});
        st.capacity = info.capacity;
        w.add(e, std::move(st));
    }
    if (info.stall) w.add(e, StallStock{});
    if (info.use == PropUse::PassiveCrafting) {
        w.add(e, Output{});
        w.add(e, Fuel{});
        w.add(e, PropWork{});
    }
    // An Advanced Crafting prop: what it makes waits in its output to be carried out
    if (info.advanced) {
        w.add(e, Output{});
        w.add(e, PropWork{});
    }
    // A Perimeter Gate's lock is its shut state: it stands open until shut
    if (info.lockLevel > 0) w.add(e, Lock{ !(info.perimeter && !info.solid), info.lockLevel });
    if (propInfo(kind).claimsLand) w.add(e, Health{});
    else if (info.health > 0.0) w.add(e, Health{ info.health, info.health });
    onPropSpawned(w, e);
    claimMakerAdded(w, e);
    return e;
}

ObjectId placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner, int storey) {
    const PropInfo& info = propInfo(kind);
    const bool piece = info.foundation || info.onFoundation;
    float y = piece ? t.heightAt(x, z) : t.standHeight(x, z);
    if (storey > 0) y = t.heightAt(x, z) + storeyBaseHeight(storey);
    // A wall decoration hangs from the wall it is on: its base is the wall's
    if (info.wallDecor) {
        if (const ObjectId wall = wallTileAt(w, tileOf(x), tileOf(z), storey); wall != NO_OBJECT) y = w.get<Transform>(wall)->y;
    }
    const ObjectId e = makeProp(w, kind, Transform{ x, y, z }, yaw);
    if (info.perimeter) notePerimeterGround(w, t, e);
    if (storey > 0) w.add(e, Storey{ storey });
    // One that needs a camp is its placer's, so it is known whose camp it must stay in
    if (info.needsCamp) w.add(e, Owner{ owner });
    if (!info.built) {
        claimOwnership(w, e);
        return e;
    }

    w.add(e, Owner{ owner });
    if (Lock* gate = w.get<Lock>(e); gate && info.perimeter && !info.solid) gate->faction = owner;

    if (hasStorage(info) && !info.claimsLand && claimAt(claimsOf(w), x, z, owner)) {
        w.add(e, Lock{ true, BUILT_LOCK_LEVEL, owner });
    }
    claimOwnership(w, e);
    return e;
}
