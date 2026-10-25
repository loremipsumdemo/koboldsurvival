#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>
#include "terrain.h"

// Paths are searched over the tile grid and then smoothed into straight segments. The center of a
// walkable tile is always clear of obstacles, so every grid path can be walked.

namespace {

constexpr float SQRT2 = 1.41421356f;
// A search gives up after this many tiles and takes the closest one. Most searches on the test worlds
// take a few thousand tiles; the longest detour needed about 220,000.
constexpr int MAX_SEARCH_TILES = 400000;
constexpr float LINE_SAMPLE = 0.125f;      // spacing of the clearance checks along a straight line

struct Step { int dx, dz; float cost; };
constexpr Step STEPS[8] = {
    { 1, 0, 1.0f }, { -1, 0, 1.0f }, { 0, 1, 1.0f }, { 0, -1, 1.0f },
    { 1, 1, SQRT2 }, { 1, -1, SQRT2 }, { -1, 1, SQRT2 }, { -1, -1, SQRT2 },
};

using TileKey = uint64_t;
TileKey keyOf(int x, int z) { return ((uint64_t)(uint32_t)x << 32) | (uint32_t)z; }
int xOf(TileKey k) { return (int)(uint32_t)(k >> 32); }
int zOf(TileKey k) { return (int)(uint32_t)(k & 0xFFFFFFFFu); }

// Shortest distance on the grid when diagonal steps are allowed
float octile(int ax, int az, int bx, int bz) {
    const float dx = (float)std::abs(ax - bx), dz = (float)std::abs(az - bz);
    return std::max(dx, dz) + (SQRT2 - 1.0f) * std::min(dx, dz);
}

// True if the straight segment from a to b stays clear of every obstacle. Both ends are free.
bool clearLine(const Terrain& t, Vec2 a, Vec2 b) {
    const float dx = b.x - a.x, dz = b.z - a.z;
    const int n = std::max(1, (int)std::ceil(std::sqrt(dx * dx + dz * dz) / LINE_SAMPLE));
    for (int i = 1; i < n; ++i) {
        const float s = (float)i / (float)n;
        if (!t.isFree(a.x + dx * s, a.z + dz * s)) return false;
    }
    return t.isFree(b.x, b.z);
}

// Tile path from one tile to another, as tile coordinates, start first. If the goal can't be
// reached, the path ends at the reached tile nearest the goal.
std::vector<std::pair<int, int>> tilePath(const Terrain& t, int sx, int sz, int gx, int gz) {
    struct Node { float g; TileKey parent; bool closed; };
    std::unordered_map<TileKey, Node> nodes;
    using Entry = std::pair<float, TileKey>;   // f score, tile
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    const TileKey startKey = keyOf(sx, sz), goalKey = keyOf(gx, gz);
    nodes[startKey] = Node{ 0.0f, startKey, false };
    open.push({ octile(sx, sz, gx, gz), startKey });
    TileKey closest = startKey;
    float closestH = octile(sx, sz, gx, gz);
    bool reached = false;

    int expanded = 0;
    while (!open.empty() && expanded < MAX_SEARCH_TILES) {
        const TileKey k = open.top().second;
        open.pop();
        Node& n = nodes[k];
        if (n.closed) continue;
        n.closed = true;
        ++expanded;
        if (k == goalKey) { reached = true; break; }

        const int x = xOf(k), z = zOf(k);
        const float h = octile(x, z, gx, gz);
        if (h < closestH) { closestH = h; closest = k; }

        for (const Step& s : STEPS) {
            const int nx = x + s.dx, nz = z + s.dz;
            if (!t.walkable(nx, nz)) continue;
            // No cutting corners: a diagonal step needs both tiles beside it to be walkable
            if (s.dx != 0 && s.dz != 0 && (!t.walkable(x + s.dx, z) || !t.walkable(x, z + s.dz))) continue;

            const TileKey nk = keyOf(nx, nz);
            const float g = n.g + s.cost;
            auto it = nodes.find(nk);
            if (it == nodes.end()) {
                nodes.emplace(nk, Node{ g, k, false });
                open.push({ g + octile(nx, nz, gx, gz), nk });
            } else if (!it->second.closed && g < it->second.g) {
                it->second.g = g;
                it->second.parent = k;
                open.push({ g + octile(nx, nz, gx, gz), nk });
            }
        }
    }

    const TileKey end = reached ? goalKey : closest;
    std::vector<std::pair<int, int>> path;
    for (TileKey k = end;; k = nodes[k].parent) {
        path.push_back({ xOf(k), zOf(k) });
        if (k == startKey) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Drops the points a straight line can skip: from each point, go as far along the path as the
// line stays clear. The first and last points are kept.
std::vector<Vec2> smooth(const Terrain& t, const std::vector<Vec2>& pts) {
    std::vector<Vec2> out;
    size_t anchor = 0;
    while (anchor + 1 < pts.size()) {
        size_t far = anchor + 1;
        for (size_t j = anchor + 2; j < pts.size() && clearLine(t, pts[anchor], pts[j]); ++j) far = j;
        out.push_back(pts[far]);
        anchor = far;
    }
    return out;
}

} // namespace

std::vector<Vec2> Terrain::findPath(Vec2 start, Vec2 goal) const {
    if (!isFree(start.x, start.z)) start = snap(start);
    if (!isFree(start.x, start.z)) return {};
    goal = snap(goal);

    const int sx = (int)std::floor(start.x), sz = (int)std::floor(start.z);
    const int gx = (int)std::floor(goal.x), gz = (int)std::floor(goal.z);
    std::vector<std::pair<int, int>> tiles = tilePath(*this, sx, sz, gx, gz);
    const bool reached = !tiles.empty() && tiles.back().first == gx && tiles.back().second == gz;

    // The unit starts where it stands, so the start tile's center is not a waypoint
    if (!tiles.empty() && tiles.front().first == sx && tiles.front().second == sz) tiles.erase(tiles.begin());
    if (tiles.empty() && !reached) return {};

    std::vector<Vec2> pts;
    pts.push_back(start);
    for (const auto& tile : tiles) pts.push_back({ tile.first + 0.5f, tile.second + 0.5f });
    if (reached) pts.push_back(goal);

    std::vector<Vec2> out = smooth(*this, pts);
    if (out.size() == 1 && std::hypot(out[0].x - start.x, out[0].z - start.z) < 0.05f) return {};
    return out;
}
