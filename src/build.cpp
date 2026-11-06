#include "build.h"
#include <algorithm>
#include <cmath>

namespace {

// Spacing of the sample points across a footprint, in tiles: finer than a grid square, so a thin overlap is still found
constexpr float SAMPLE_STEP = 0.125f;

// Calls pred at each sample point inside the footprint centred at (cx, cz); true as soon as one returns true
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

// True when pred holds at every sample point inside the footprint
template <typename Pred>
bool allFootSamples(const Footprint& f, float cx, float cz, float yaw, Pred pred) {
    return !anyFootSample(f, cx, cz, yaw, [&](float x, float z) { return !pred(x, z); });
}

// Whether two footprints, each with its centre and turn, share any ground
bool footprintsOverlap(const Footprint& fa, float ax, float az, float ayaw,
                       const Footprint& fb, float bx, float bz, float byaw) {
    const float reach = footprintReach(fa) + footprintReach(fb);
    const float dx = ax - bx, dz = az - bz;
    if (dx * dx + dz * dz > reach * reach) return false;   // too far apart to touch: no samples needed
    return anyFootSample(fa, ax, az, ayaw, [&](float x, float z) {
        return insideFootprint(fb, x - bx, z - bz, byaw);
    });
}

// The extent that sets the snapping: the radius of a circle, or the larger half-side of a rectangle
float snapExtent(const Footprint& f) {
    return f.radius > 0.0f ? f.radius : std::max(f.halfW, f.halfD);
}

// Odd widths (in tiles) centre on a tile middle; even widths centre on a tile edge
float snapCoord(float g, float halfExtent) {
    const int twice = (int)std::lround(2.0f * halfExtent);
    return (twice % 2 != 0) ? std::floor(g) + 0.5f : std::round(g);
}

}  // namespace

void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z) {
    const float h = snapExtent(propInfo(kind).foot);
    x = snapCoord(gx, h);
    z = snapCoord(gz, h);
}

BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw) {
    const PropInfo& info = propInfo(kind);
    const Footprint& f = info.foot;
    const int builder = FACTION_PLAYER;

    // Every point of its shape must be ground a unit can stand on
    const bool ground = allFootSamples(f, x, z, yaw, [&](float px, float pz) {
        return t.walkable((int)std::floor(px), (int)std::floor(pz));
    });
    if (!ground) return BuildCheck::Unwalkable;

    const std::vector<Claim> claims = claimsOf(w);

    // Sealed ground belongs to the faction whose vault seals it: nobody else builds there
    const int sealer = sealingFactionAt(claims, x, z);
    if (sealer >= 0 && sealer != builder) return BuildCheck::EnemySealed;

    if (kind == PropKind::Flag) {
        for (const Claim& c : claims) {
            if (claimsOverlap(x, z, c.x, c.z)) return BuildCheck::ClaimOverlap;
        }
    }

    if (kind == PropKind::FoundationSmall || kind == PropKind::FoundationLarge || kind == PropKind::FoundationLong ||
        kind == PropKind::FoundationCircle) {
        // A foundation lies wholly inside one of the builder's claim circles
        bool inClaim = false;
        for (const Claim& c : claims) {
            if (c.owner != builder) continue;
            const float r = CLAIM_RADIUS;
            if (allFootSamples(f, x, z, yaw, [&](float px, float pz) {
                    const float dx = px - c.x, dz = pz - c.z;
                    return dx * dx + dz * dz <= r * r;
                })) {
                inClaim = true;
                break;
            }
        }
        if (!inClaim) return BuildCheck::NoClaim;
    }

    // A vault lies wholly on a foundation, checked against that foundation's real shape
    if (info.onFoundation) {
        bool onOne = false;
        for (Entity e : w.view<Prop, Transform>()) {
            const Prop* p = w.get<Prop>(e);
            const PropInfo& oi = propInfo(p->kind);
            if (!oi.foot.foundation) continue;
            const Transform* tr = w.get<Transform>(e);
            if (allFootSamples(f, x, z, yaw, [&](float px, float pz) {
                    return insideFootprint(oi.foot, px - tr->x, pz - tr->z, p->yaw);
                })) {
                onOne = true;
                break;
            }
        }
        if (!onOne) return BuildCheck::NoFoundation;

        // The vault goes in the builder's claim, which must have no vault yet, within the faction's cap
        const Claim* c = claimAt(claims, x, z, builder);
        if (!c) return BuildCheck::NoClaim;
        if (c->vault != NULL_ENTITY) return BuildCheck::VaultTaken;
        int standing = 0;
        for (const Claim& other : claims) {
            if (other.vault != NULL_ENTITY && w.get<Owner>(other.vault)->faction == builder) ++standing;
        }
        if (standing >= VAULT_CAP) return BuildCheck::VaultCap;
    }

    // Nothing else may share its ground. A foundation can be shared only by a built prop that sits on one.
    for (Entity e : w.view<Prop, Transform>()) {
        const Prop* p = w.get<Prop>(e);
        const PropInfo& oi = propInfo(p->kind);
        if (oi.foot.foundation && info.canSitOnFoundation) continue;
        const Transform* tr = w.get<Transform>(e);
        if (footprintsOverlap(f, x, z, yaw, oi.foot, tr->x, tr->z, p->yaw)) return BuildCheck::Blocked;
    }
    return BuildCheck::Ok;
}

bool buildAnywhere(const BuildPick& pick) {
    return pick.tool == BuildPick::Tool::Destroy || pick.places(PropKind::Bedroll) || pick.places(PropKind::Campfire);
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
    case BuildCheck::NoFoundation: return "MISC_BUILD_NO_FOUNDATION";
    case BuildCheck::NoClaim: return "MISC_BUILD_NO_CLAIM";
    case BuildCheck::EnemySealed: return "MISC_BUILD_SEALED";
    case BuildCheck::ClaimOverlap: return "MISC_BUILD_CLAIM_OVERLAP";
    case BuildCheck::VaultTaken: return "MISC_BUILD_VAULT_TAKEN";
    case BuildCheck::VaultCap: return "MISC_BUILD_VAULT_CAP";
    case BuildCheck::Ok: break;
    }
    return "";
}

// The parts a built prop starts with, as a placed one of its kind would have them (world_setup.cpp, a level's props).
// A built storage has nothing in it; a passive crafting prop (the Vault) starts cold, with an empty output.
static void addBuiltPropParts(World& w, Entity e, PropKind kind) {
    const PropInfo& info = propInfo(kind);
    if (hasStorage(info)) w.add(e, Storage{});
    if (info.use == PropUse::PassiveCrafting) {
        w.add(e, Output{});
        w.add(e, Fuel{});
        w.add(e, PropWork{});
    }
    if (info.lockLevel > 0) w.add(e, Lock{ true, info.lockLevel });
}

Entity restoreBuiltProp(World& w, PropKind kind, float x, float y, float z, float yaw, int owner) {
    const PropInfo& info = propInfo(kind);
    const Entity e = w.create();
    w.add(e, Transform{ x, y, z });
    w.add(e, Prop{ kind, -1, yaw });
    addBuiltPropParts(w, e, kind);
    if (owner >= 0) w.add(e, Owner{ owner });
    if (info.built) {
        if (kind == PropKind::Flag) w.add(e, Health{});
        if (info.foot.foundation) w.add(e, Wear{});
    }
    return e;
}

Entity placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner) {
    const PropInfo& info = propInfo(kind);
    const Entity e = w.create();
    w.add(e, Transform{ x, t.heightAt(x, z), z });
    w.add(e, Prop{ kind, -1, yaw });
    addBuiltPropParts(w, e, kind);
    if (!info.built) {
        claimOwnership(w, e);
        return e;
    }

    w.add(e, Owner{ owner });
    if (kind == PropKind::Flag) w.add(e, Health{});
    if (info.foot.foundation) w.add(e, Wear{});

    // A prop that sits on a foundation is held by it: the foundation containing its centre
    if (info.canSitOnFoundation) {
        for (Entity f : w.view<Prop, Transform>()) {
            const Prop* fp = w.get<Prop>(f);
            if (!propInfo(fp->kind).foot.foundation) continue;
            const Transform* ft = w.get<Transform>(f);
            if (insideFootprint(propInfo(fp->kind).foot, x - ft->x, z - ft->z, fp->yaw)) {
                w.add(e, OnBase{ f });
                break;
            }
        }
    }

    // Inside a claim of its faction, a prop with contents is locked to that faction. Inside any claim, the claim's
    // faction owns it (claimOwnership).
    if (hasStorage(info) && claimAt(claimsOf(w), x, z, owner)) {
        w.add(e, Lock{ true, BUILT_LOCK_LEVEL, owner });
    }
    claimOwnership(w, e);
    return e;
}
