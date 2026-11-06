#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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
// A goal in another walkable area can't be reached however long the search, so it only looks this far for the place
// nearest it. The search heads for the goal first, so this is enough to reach the near shore.
constexpr int UNREACHABLE_SEARCH_TILES = 20000;
// The distance estimate is nudged up a little, so a search on open ground runs straight at the goal rather than
// opening every tile of equal cost. A path can come out a tenth of a percent longer than the shortest; smoothing
// straightens it anyway.
constexpr float HEURISTIC_WEIGHT = 1.001f;
constexpr float SIGHT_EYE_HEIGHT = 1.0f;   // how far above its feet a unit sees from
constexpr float SIGHT_SAMPLE = 0.5f;       // spacing of the ground checks along a line of sight
constexpr float LINE_SAMPLE = 0.125f;      // spacing of the clearance checks along a straight line

struct Step { int dx, dz; float cost; };
constexpr Step STEPS[8] = {
    { 1, 0, 1.0f }, { -1, 0, 1.0f }, { 0, 1, 1.0f }, { 0, -1, 1.0f },
    { 1, 1, SQRT2 }, { 1, -1, SQRT2 }, { -1, 1, SQRT2 }, { -1, -1, SQRT2 },
};

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

// What a search knows of each tile. These stay between searches, each tile marked with the search that last touched it,
// so a search costs only the tiles it opens and nothing is allocated once they have grown. Paths are only searched for on
// the main thread.
struct Node {
    float g = 0.0f;
    int parent = -1;
    uint32_t search = 0;
    bool closed = false;
};
std::vector<Node> gNodes;
uint32_t gSearch = 0;
using Entry = std::pair<float, int>;   // f score, tile
std::vector<Entry> gOpen;              // a heap, smallest f first

// Tile path from one tile to another, as tile coordinates, start first. If the goal can't be
// reached, the path ends at the reached tile nearest the goal.
std::vector<std::pair<int, int>> tilePath(const Terrain& t, int sx, int sz, int gx, int gz, int maxTiles) {
    const int size = t.size();
    if (gNodes.size() != (size_t)size * size) {
        gNodes.assign((size_t)size * size, Node{});
        gSearch = 0;
    }
    ++gSearch;
    gOpen.clear();
    const std::greater<Entry> later;
    auto push = [&](float f, int k) {
        gOpen.push_back({ f, k });
        std::push_heap(gOpen.begin(), gOpen.end(), later);
    };

    const int startKey = sz * size + sx;
    // The goal may be off the level, when it could not be snapped anywhere: then no tile matches it
    const int goalKey = t.inBounds(gx, gz) ? gz * size + gx : -1;
    gNodes[(size_t)startKey] = Node{ 0.0f, startKey, gSearch, false };
    push(HEURISTIC_WEIGHT * octile(sx, sz, gx, gz), startKey);
    int closest = startKey;
    float closestH = octile(sx, sz, gx, gz);
    bool reached = false;

    int expanded = 0;
    while (!gOpen.empty() && expanded < maxTiles) {
        const int k = gOpen.front().second;
        std::pop_heap(gOpen.begin(), gOpen.end(), later);
        gOpen.pop_back();
        Node& n = gNodes[(size_t)k];
        if (n.closed) continue;
        n.closed = true;
        ++expanded;
        if (k == goalKey) { reached = true; break; }

        const int x = k % size, z = k / size;
        const float h = octile(x, z, gx, gz);
        if (h < closestH) { closestH = h; closest = k; }

        for (const Step& s : STEPS) {
            const int nx = x + s.dx, nz = z + s.dz;
            if (!t.walkable(nx, nz)) continue;
            // No cutting corners: a diagonal step needs both tiles beside it to be walkable
            if (s.dx != 0 && s.dz != 0 && (!t.walkable(x + s.dx, z) || !t.walkable(x, z + s.dz))) continue;

            const int nk = nz * size + nx;
            const float g = n.g + s.cost;
            Node& o = gNodes[(size_t)nk];
            if (o.search != gSearch) {
                o = Node{ g, k, gSearch, false };
                push(g + HEURISTIC_WEIGHT * octile(nx, nz, gx, gz), nk);
            } else if (!o.closed && g < o.g) {
                o.g = g;
                o.parent = k;
                push(g + HEURISTIC_WEIGHT * octile(nx, nz, gx, gz), nk);
            }
        }
    }

    std::vector<std::pair<int, int>> path;
    for (int k = reached ? goalKey : closest;; k = gNodes[(size_t)k].parent) {
        path.push_back({ k % size, k / size });
        if (k == startKey) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Drops the points a straight line can skip: from each point, go as far along the path as the
// line stays clear. The first and last points are kept. How far is found by doubling the stride until a line is blocked
// and then halving back, since testing every point in turn costs far more than the search on a long path.
std::vector<Vec2> smooth(const Terrain& t, const std::vector<Vec2>& pts) {
    std::vector<Vec2> out;
    size_t anchor = 0;
    while (anchor + 1 < pts.size()) {
        const size_t last = pts.size() - 1;
        size_t far = anchor + 1;      // the path itself gets this far
        size_t blocked = last + 1;    // the nearest point known to be out of a straight line, if any
        for (size_t stride = 1; far < last;) {
            const size_t next = std::min(far + stride, last);
            if (!clearLine(t, pts[anchor], pts[next])) {
                blocked = next;
                break;
            }
            far = next;
            stride *= 2;
        }
        while (blocked <= last && blocked - far > 1) {
            const size_t mid = far + (blocked - far) / 2;
            if (clearLine(t, pts[anchor], pts[mid])) far = mid;
            else blocked = mid;
        }
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
    if (!walkable(sx, sz)) return {};
    const int maxTiles = areaOf(sx, sz) == areaOf(gx, gz) ? MAX_SEARCH_TILES : UNREACHABLE_SEARCH_TILES;
    std::vector<std::pair<int, int>> tiles = tilePath(*this, sx, sz, gx, gz, maxTiles);
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

bool Terrain::lineClear(Vec2 a, Vec2 b) const {
    return clearLine(*this, a, b);
}

bool Terrain::sightClear(float ax, float ay, float az, float bx, float by, float bz) const {
    const float dx = bx - ax, dz = bz - az;
    const int n = (int)std::ceil(std::sqrt(dx * dx + dz * dz) / SIGHT_SAMPLE);
    for (int i = 1; i < n; ++i) {
        const float s = (float)i / (float)n;
        const float eye = ay + (by - ay) * s + SIGHT_EYE_HEIGHT;
        if (heightAt(ax + dx * s, az + dz * s) > eye) return false;
    }
    return true;
}
