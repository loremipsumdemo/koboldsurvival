#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include "terrain.h"

namespace {
constexpr float SQRT2 = 1.41421356f;
constexpr int MAX_SEARCH_TILES = 400000;
constexpr int UNREACHABLE_SEARCH_TILES = 20000;
// The distance estimate is nudged up a little, so a search on open ground runs straight at the goal rather than
// opening every tile of equal cost. A path can come out a tenth of a percent longer than the shortest; smoothing
// straightens it anyway.
constexpr float HEURISTIC_WEIGHT = 1.001f;
constexpr float SIGHT_EYE_HEIGHT = 1.0f;
constexpr float SIGHT_SAMPLE = 0.5f;
constexpr float LINE_SAMPLE = 0.125f;

struct Step { int dx, dz; float cost; };
constexpr Step STEPS[8] = {
    { 1, 0, 1.0f }, { -1, 0, 1.0f }, { 0, 1, 1.0f }, { 0, -1, 1.0f },
    { 1, 1, SQRT2 }, { 1, -1, SQRT2 }, { -1, 1, SQRT2 }, { -1, -1, SQRT2 },
};

float octile(int ax, int az, int bx, int bz) {
    const float dx = (float)std::abs(ax - bx), dz = (float)std::abs(az - bz);
    return std::max(dx, dz) + (SQRT2 - 1.0f) * std::min(dx, dz);
}

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
// In chunk-sized pages, made as a search reaches them, so a search on a huge map costs only the ground it covers.
constexpr int NODE_PAGE_TILES = 64;
constexpr size_t MOST_NODE_PAGES = 256;
struct NodePage {
    Node nodes[NODE_PAGE_TILES * NODE_PAGE_TILES];
};
std::vector<std::unique_ptr<NodePage>> gPages;
size_t gPagesHeld = 0;
int gPagesSize = 0;
uint32_t gSearch = 0;

Node& nodeAt(int x, int z) {
    const int pagesAcross = (gPagesSize + NODE_PAGE_TILES - 1) / NODE_PAGE_TILES;
    std::unique_ptr<NodePage>& page = gPages[(size_t)(z / NODE_PAGE_TILES) * pagesAcross + x / NODE_PAGE_TILES];
    if (!page) {
        page = std::make_unique<NodePage>();
        ++gPagesHeld;
    }
    return page->nodes[(z % NODE_PAGE_TILES) * NODE_PAGE_TILES + x % NODE_PAGE_TILES];
}
using Entry = std::pair<float, int>;
std::vector<Entry> gOpen;

std::vector<std::pair<int, int>> tilePath(const Terrain& t, int sx, int sz, int gx, int gz, int maxTiles) {
    const int size = t.size();
    if (gPagesSize != size || gPagesHeld > MOST_NODE_PAGES) {
        const size_t pagesAcross = (size_t)(size + NODE_PAGE_TILES - 1) / NODE_PAGE_TILES;
        gPages.clear();
        gPages.resize(pagesAcross * pagesAcross);
        gPagesHeld = 0;
        gPagesSize = size;
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
    const int goalKey = t.inBounds(gx, gz) ? gz * size + gx : -1;
    nodeAt(sx, sz) = Node{ 0.0f, startKey, gSearch, false };
    push(HEURISTIC_WEIGHT * octile(sx, sz, gx, gz), startKey);
    int closest = startKey;
    float closestH = octile(sx, sz, gx, gz);
    bool reached = false;

    int expanded = 0;
    while (!gOpen.empty() && expanded < maxTiles) {
        const int k = gOpen.front().second;
        std::pop_heap(gOpen.begin(), gOpen.end(), later);
        gOpen.pop_back();
        const int x = k % size, z = k / size;
        Node& n = nodeAt(x, z);
        if (n.closed) continue;
        n.closed = true;
        ++expanded;
        if (k == goalKey) { reached = true; break; }

        const float h = octile(x, z, gx, gz);
        if (h < closestH) { closestH = h; closest = k; }

        for (const Step& s : STEPS) {
            const int nx = x + s.dx, nz = z + s.dz;
            if (!t.walkable(nx, nz)) continue;
            if (s.dx != 0 && s.dz != 0 && (!t.walkable(x + s.dx, z) || !t.walkable(x, z + s.dz))) continue;

            const int nk = nz * size + nx;
            const float g = n.g + s.cost;
            Node& o = nodeAt(nx, nz);
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
    for (int k = reached ? goalKey : closest;; k = nodeAt(k % size, k / size).parent) {
        path.push_back({ k % size, k / size });
        if (k == startKey) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Vec2> smooth(const Terrain& t, const std::vector<Vec2>& pts) {
    std::vector<Vec2> out;
    size_t anchor = 0;
    while (anchor + 1 < pts.size()) {
        const size_t last = pts.size() - 1;
        size_t far = anchor + 1;
        size_t blocked = last + 1;
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
}

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
