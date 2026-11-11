#include "build.h"
#include "building.h"
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
    const float dx = ax - bx, dz = az - bz;
    if (dx * dx + dz * dz > reach * reach) return false;
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

void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z) {
    const float h = snapExtent(propInfo(kind).foot);
    x = snapCoord(gx, h);
    z = snapCoord(gz, h);
}

BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z, int storey) {
    const Footprint& f = propInfo(kind).foot;
    const int builder = FACTION_PLAYER;
    const float yaw = 0.0f;

    // On an upper floor, every bit of it must stand on a finished floor of yours on that level
    const bool ground = allFootSamples(f, x, z, yaw, [&](float px, float pz) {
        const int tx = (int)std::floor(px), tz = (int)std::floor(pz);
        if (storey > 0) return floorSurfaceAt(w, tx, tz, storey, builder);
        return t.walkable(tx, tz);
    });
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

    const std::vector<Claim> claims = claimsOf(w);

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

    for (ObjectId e : w.view<Prop, Transform>()) {
        const Prop* p = w.get<Prop>(e);
        const PropInfo& other = propInfo(p->kind);
        // Floors (a foundation's tiles, upper floors) are stood on, not in the way; things on other floors don't count
        if (other.foundation || other.upperFloor || storeyOf(w, e) != storey) continue;
        const Transform* tr = w.get<Transform>(e);
        if (footprintsOverlap(f, x, z, yaw, other.foot, tr->x, tr->z, p->yaw)) return BuildCheck::Blocked;
    }
    return BuildCheck::Ok;
}

bool buildAnywhere(const BuildPick& pick) {
    return pick.tool == BuildPick::Tool::Destroy || pick.adjusting() || (pick.placing() && propInfo(pick.kind).buildAnywhere);
}

bool selectionInClaim(World& w) {
    const std::vector<Claim> claims = claimsOf(w);
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
    case BuildCheck::Ok: break;
    }
    return "";
}

void onPropSpawned(World& w, ObjectId e) {
    Prop* p = w.get<Prop>(e);
    const Transform* at = w.get<Transform>(e);
    if (!p || !at) return;
    const PropInfo& info = propInfo(p->kind);
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
    if (info.lockLevel > 0) w.add(e, Lock{ true, info.lockLevel });
    if (propInfo(kind).claimsLand) w.add(e, Health{});
    onPropSpawned(w, e);
    return e;
}

ObjectId placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner, int storey) {
    const PropInfo& info = propInfo(kind);
    const bool piece = info.foundation || info.onFoundation;
    float y = piece ? t.heightAt(x, z) : t.standHeight(x, z);
    if (storey > 0) y = t.heightAt(x, z) + storeyBaseHeight(storey);
    const ObjectId e = makeProp(w, kind, Transform{ x, y, z }, yaw);
    if (storey > 0) w.add(e, Storey{ storey });
    // One that needs a camp is its placer's, so it is known whose camp it must stay in
    if (info.needsCamp) w.add(e, Owner{ owner });
    if (!info.built) {
        claimOwnership(w, e);
        return e;
    }

    w.add(e, Owner{ owner });

    if (hasStorage(info) && !info.claimsLand && claimAt(claimsOf(w), x, z, owner)) {
        w.add(e, Lock{ true, BUILT_LOCK_LEVEL, owner });
    }
    claimOwnership(w, e);
    return e;
}
