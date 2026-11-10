#include "spawn_cluster.h"
#include <algorithm>
#include <cmath>
#include "props.h"

namespace {
constexpr float TWO_PI_F = 6.2831853f;

bool openGround(const Terrain& t, float x, float z) {
    if (x < 0.0f || z < 0.0f || x >= (float)t.size() || z >= (float)t.size()) return false;
    return t.walkable((int)std::floor(x), (int)std::floor(z));
}

// Clear of every prop already there, by its footprint's reach
bool clearOfProps(World& w, float x, float z, float spacing) {
    for (ObjectId p : w.view<Prop, Transform>()) {
        const Transform* pt = w.get<Transform>(p);
        const float reach = footprintReach(propInfo(w.get<Prop>(p)->kind).foot) + spacing * 0.5f;
        const float dx = pt->x - x, dz = pt->z - z;
        if (dx * dx + dz * dz < reach * reach) return false;
    }
    return true;
}
}

std::vector<Vec2> clusterSpots(World& w, const Terrain& t, Rng& rng, float x, float z, int count, float spacing, bool props) {
    std::vector<Vec2> out;
    count = std::clamp(count, 1, SPAWN_CLUSTER_MAX);
    // One is placed where it was asked for, whatever is there, as a single spawn always was
    out.push_back(Vec2{ x, z });
    if (count == 1) return out;
    spacing = std::max(0.3f, spacing);
    auto fits = [&](float px, float pz) {
        if (!openGround(t, px, pz)) return false;
        for (const Vec2& o : out) {
            if ((o.x - px) * (o.x - px) + (o.z - pz) * (o.z - pz) < spacing * spacing) return false;
        }
        return !props || clearOfProps(w, px, pz, spacing);
    };
    // Scattered in a disc big enough for them, which grows when it fills up
    float radius = std::max(1.5f, spacing * std::sqrt((float)count) * 1.2f);
    int misses = 0;
    while ((int)out.size() < count && radius < 64.0f) {
        const float a = rng.unit() * TWO_PI_F, d = std::sqrt(rng.unit()) * radius;
        const float px = x + std::cos(a) * d, pz = z + std::sin(a) * d;
        if (fits(px, pz)) {
            out.push_back(Vec2{ px, pz });
            misses = 0;
        } else if (++misses > 40) {
            radius *= 1.5f;
            misses = 0;
        }
    }
    return out;
}
