#include "terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <queue>
#include <random>

namespace {

// Classic 2D gradient (Perlin) noise with a seeded permutation table.
struct Perlin {
    int p[512];

    explicit Perlin(unsigned seed) {
        std::vector<int> perm(256);
        std::iota(perm.begin(), perm.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(perm.begin(), perm.end(), rng);
        for (int i = 0; i < 512; ++i) p[i] = perm[i & 255];
    }

    static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    static float lerp(float a, float b, float t) { return a + t * (b - a); }

    static float grad(int h, float x, float y) {
        switch (h & 7) {
        case 0: return  x + y;
        case 1: return -x + y;
        case 2: return  x - y;
        case 3: return -x - y;
        case 4: return  x;
        case 5: return -x;
        case 6: return  y;
        default: return -y;
        }
    }

    // Output is roughly in [-0.71, 0.71]
    float noise(float x, float y) const {
        float fx = std::floor(x), fy = std::floor(y);
        int xi = (int)fx & 255, yi = (int)fy & 255;
        float xf = x - fx, yf = y - fy;
        float u = fade(xf), v = fade(yf);

        int aa = p[p[xi] + yi];
        int ab = p[p[xi] + yi + 1];
        int ba = p[p[xi + 1] + yi];
        int bb = p[p[xi + 1] + yi + 1];

        float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
        float x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
        return lerp(x1, x2, v);
    }
};

} // namespace

void Terrain::generate(unsigned seed) {
    Perlin perlin(seed);

    heights_.assign((size_t)VERT_SIZE * VERT_SIZE, 0.0f);
    for (int z = 0; z < VERT_SIZE; ++z) {
        for (int x = 0; x < VERT_SIZE; ++x) {
            // Two octaves, then scaled so the result stays within [-1, 1]
            float fx = x * 0.12f, fz = z * 0.12f;   // features roughly 8 tiles across
            float n = perlin.noise(fx, fz)
                    + 0.5f * perlin.noise(fx * 2.1f + 17.3f, fz * 2.1f - 4.7f);
            heights_[(size_t)z * VERT_SIZE + x] = std::clamp(n / 1.05f, -1.0f, 1.0f) * HEIGHT_SCALE;
        }
    }

    size_t cells = (size_t)MAP_SIZE * MAP_SIZE;
    walkable_.assign(cells, 1);
    gScore_.assign(cells, 0.0f);
    parent_.assign(cells, -1);
    stamp_.assign(cells, 0u);
    closed_.assign(cells, 0u);
    gen_ = 0;
}

float Terrain::heightAt(float x, float z) const {
    x = std::clamp(x, 0.0f, (float)MAP_SIZE - 0.001f);
    z = std::clamp(z, 0.0f, (float)MAP_SIZE - 0.001f);
    int x0 = (int)x, z0 = (int)z;
    float fx = x - x0, fz = z - z0;

    auto h = [&](int i, int j) { return heights_[(size_t)j * VERT_SIZE + i]; };
    float ha = h(x0, z0);          // corner a: (0,0)
    float hb = h(x0 + 1, z0);      // corner b: (1,0)
    float hc = h(x0, z0 + 1);      // corner c: (0,1)
    float hd = h(x0 + 1, z0 + 1);  // corner d: (1,1)

    // The mesh splits each tile along the a-d diagonal into two planar triangles.
    // Sampling with the same split keeps units exactly on the drawn surface.
    if (fz >= fx) return ha + fx * (hd - hc) + fz * (hc - ha);   // triangle a, c, d
    return ha + fx * (hb - ha) + fz * (hd - hb);                 // triangle a, d, b
}

GridPos Terrain::nearestWalkable(GridPos g) const {
    if (walkable(g.x, g.y)) return g;
    for (int r = 1; r <= 15; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (walkable(g.x + dx, g.y + dy)) return { g.x + dx, g.y + dy };
            }
        }
    }
    return g;
}

std::vector<GridPos> Terrain::findPath(GridPos start, GridPos goal) {
    if (start == goal || !walkable(goal.x, goal.y)) return {};

    // New generation for this search, so no per-search clearing is needed
    if (++gen_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        std::fill(closed_.begin(), closed_.end(), 0u);
        gen_ = 1;
    }

    auto idx = [](int x, int y) { return y * MAP_SIZE + x; };
    auto heuristic = [&](int x, int y) {
        int dx = std::abs(x - goal.x), dy = std::abs(y - goal.y);
        return (float)std::max(dx, dy) + 0.4142f * (float)std::min(dx, dy);   // octile
    };

    using Node = std::pair<float, int>;   // (f score, cell index)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    int s = idx(start.x, start.y);
    int goalIdx = idx(goal.x, goal.y);
    stamp_[s] = gen_;
    gScore_[s] = 0.0f;
    parent_[s] = -1;
    open.push({ heuristic(start.x, start.y), s });

    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (closed_[cur] == gen_) continue;   // stale heap entry
        closed_[cur] = gen_;
        if (cur == goalIdx) break;

        int cx = cur % MAP_SIZE, cy = cur / MAP_SIZE;
        for (int k = 0; k < 8; ++k) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (!walkable(nx, ny)) continue;

            bool diag = DX[k] != 0 && DY[k] != 0;
            // No cutting corners past blocked tiles (keeps units out of building corners)
            if (diag && (!walkable(cx + DX[k], cy) || !walkable(cx, cy + DY[k]))) continue;

            int nk = idx(nx, ny);
            if (closed_[nk] == gen_) continue;

            float g2 = gScore_[cur] + (diag ? 1.4142f : 1.0f);
            if (stamp_[nk] != gen_ || g2 < gScore_[nk]) {
                stamp_[nk] = gen_;
                gScore_[nk] = g2;
                parent_[nk] = cur;
                open.push({ g2 + heuristic(nx, ny), nk });
            }
        }
    }

    if (closed_[goalIdx] != gen_) return {};

    std::vector<GridPos> path;
    for (int k = goalIdx; k != s; k = parent_[k]) {
        path.push_back({ k % MAP_SIZE, k / MAP_SIZE });
    }
    std::reverse(path.begin(), path.end());
    return path;
}
