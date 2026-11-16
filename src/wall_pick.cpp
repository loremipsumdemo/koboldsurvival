#include "wall_pick.h"
#include <algorithm>
#include <cmath>
#include "build.h"
#include "building.h"
#include "geometry.h"
#include "selection.h"

namespace {
bool pointerRay(const Camera& cam, float mx, float my, int W, int H, float o[3], float d[3]) {
    const Mat4 vp = cam.viewProj((float)W / (float)H);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return false;
    const float ndcX = 2.0f * mx / (float)W - 1.0f, ndcY = 1.0f - 2.0f * my / (float)H;
    auto unproject = [&](float ndcZ, float out[3]) {
        const float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4];
        for (int i = 0; i < 4; ++i) r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1] + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
        out[0] = r[0] / r[3];
        out[1] = r[1] / r[3];
        out[2] = r[2] / r[3];
    };
    float f[3];
    unproject(-1.0f, o);
    unproject(1.0f, f);
    float len = 0.0f;
    for (int i = 0; i < 3; ++i) {
        d[i] = f[i] - o[i];
        len += d[i] * d[i];
    }
    len = std::sqrt(len);
    if (len < 1e-6f) return false;
    for (int i = 0; i < 3; ++i) d[i] /= len;
    return true;
}
}

std::optional<WallSidePick> pickWallSide(World& w, const Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                                         const std::function<bool(int, int, int)>& cutAway) {
    const BuildingMap& m = cachedBuildingMap(w);
    if (m.piece.empty()) return std::nullopt;
    float o[3], d[3];
    if (!pointerRay(cam, mx, my, W, H, o, d)) return std::nullopt;

    // How far to look: to where the ray reaches the ground (a wall stands above it), else a good way
    float reach = 300.0f;
    if (const auto g = pickGround(cam, t, mx, my, W, H)) {
        const float gy = t.heightAt(g->first, g->second);
        const float dx = g->first - o[0], dy = gy - o[1], dz = g->second - o[2];
        reach = std::sqrt(dx * dx + dy * dy + dz * dz) + 1.0f;
    }

    constexpr float STEP = 0.1f;
    for (float s = 0.0f; s < reach; s += STEP) {
        const float x = o[0] + d[0] * s, y = o[1] + d[1] * s, z = o[2] + d[2] * s;
        const int tx = tileOf(x), tz = tileOf(z);
        const float rel = y - t.heightAt((float)tx + 0.5f, (float)tz + 0.5f);
        if (rel < 0.0f) continue;
        const int storey = std::min(STOREY_MAX, (int)std::floor(rel / FLOOR_TOP));
        if (rel - storeyBaseHeight(storey) > FLOOR_HEIGHT) continue;   // in the slab between two floors
        const ObjectId wall = wallTileAt(w, tx, tz, storey);
        if (wall == NO_OBJECT || cutAway(tx, tz, storey)) continue;

        // Where the ray comes into the wall's tile, and by which face of it
        const float baseY = w.get<Transform>(wall)->y;
        const float lo[3] = { (float)tx, baseY, (float)tz }, hi[3] = { (float)tx + 1.0f, baseY + FLOOR_HEIGHT, (float)tz + 1.0f };
        float t0 = 0.0f;
        int face = -1;   // 0 x, 1 y, 2 z
        for (int a = 0; a < 3; ++a) {
            if (std::fabs(d[a]) < 1e-9f) continue;
            const float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
            const float near = std::min(ta, tb);
            if (near > t0) {
                t0 = near;
                face = a;
            }
        }
        WallSidePick pick;
        pick.tx = tx;
        pick.tz = tz;
        pick.storey = storey;
        if (face == 0) pick.side = d[0] > 0.0f ? 3 : 1;
        else if (face == 2) pick.side = d[2] > 0.0f ? 2 : 0;
        else {
            // Down onto the top (or the ray began inside the tile): the open side that looks most toward the camera
            float best = 1e9f;
            int bestSide = 0;
            for (int side = 0; side < 4; ++side) {
                const float facing = (float)SIDE_DX[side] * d[0] + (float)SIDE_DZ[side] * d[2];   // negative: the side looks at the camera
                const float score = facing + (wallSideBare(w, tx, tz, storey, side) ? 0.0f : 4.0f);
                if (score < best) {
                    best = score;
                    bestSide = side;
                }
            }
            pick.side = bestSide;
        }
        return pick;
    }
    return std::nullopt;
}
