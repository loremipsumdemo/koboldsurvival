#include "build.h"
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

BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z) {
    const Footprint& f = propInfo(kind).foot;
    const int builder = FACTION_PLAYER;
    const float yaw = 0.0f;

    const bool ground = allFootSamples(f, x, z, yaw, [&](float px, float pz) {
        return t.walkable((int)std::floor(px), (int)std::floor(pz));
    });
    if (!ground) return BuildCheck::Unwalkable;

    const std::vector<Claim> claims = claimsOf(w);

    if (const Claim* held = claimAt(claims, x, z); held && held->owner != builder) return BuildCheck::EnemySealed;

    if (kind == PropKind::Altar) {
        for (const Claim& c : claims) {
            if (c.owner == builder) return BuildCheck::AltarTaken;
            if (claimsOverlap(x, z, c.x, c.z)) return BuildCheck::ClaimOverlap;
        }
    }

    for (Entity e : w.view<Prop, Transform>()) {
        const Prop* p = w.get<Prop>(e);
        const Transform* tr = w.get<Transform>(e);
        if (footprintsOverlap(f, x, z, yaw, propInfo(p->kind).foot, tr->x, tr->z, p->yaw)) return BuildCheck::Blocked;
    }
    return BuildCheck::Ok;
}

bool buildAnywhere(const BuildPick& pick) {
    return pick.tool == BuildPick::Tool::Destroy || pick.places(PropKind::Bedroll) || pick.places(PropKind::Campfire) ||
           pick.places(PropKind::Altar);
}

bool selectionInClaim(World& w) {
    const std::vector<Claim> claims = claimsOf(w);
    for (Entity u : w.view<Unit, Selected, Commandable, Transform>()) {
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
    case BuildCheck::Ok: break;
    }
    return "";
}

Entity makeProp(World& w, PropKind kind, const Transform& at, float yaw) {
    const PropInfo& info = propInfo(kind);
    const Entity e = w.create();
    w.add(e, at);
    w.add(e, Prop{ kind, yaw });
    if (hasStorage(info)) w.add(e, Storage{});
    if (info.use == PropUse::PassiveCrafting) {
        w.add(e, Output{});
        w.add(e, Fuel{});
        w.add(e, PropWork{});
    }
    if (info.lockLevel > 0) w.add(e, Lock{ true, info.lockLevel });
    if (kind == PropKind::Altar) w.add(e, Health{});
    return e;
}

Entity placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner) {
    const PropInfo& info = propInfo(kind);
    const Entity e = makeProp(w, kind, Transform{ x, t.heightAt(x, z), z }, yaw);
    if (!info.built) {
        claimOwnership(w, e);
        return e;
    }

    w.add(e, Owner{ owner });

    if (hasStorage(info) && kind != PropKind::Altar && claimAt(claimsOf(w), x, z, owner)) {
        w.add(e, Lock{ true, BUILT_LOCK_LEVEL, owner });
    }
    claimOwnership(w, e);
    return e;
}
