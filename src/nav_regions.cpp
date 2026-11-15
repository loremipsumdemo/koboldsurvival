#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include "terrain.h"

// Which open tiles can reach which, counting the tiles buildings close (Terrain::setBlocked), kept up to date a piece at a time.
//
// The map is cut into square clusters of CL tiles. Inside a cluster its open tiles are labelled into local regions (4-connected: a diagonal
// step needs both its sides open, so it never joins two regions that are not already joined). Between neighboring clusters, the pairs of
// regions that touch across their shared edge are listed. A region of one cluster joined to a region of another by such a pair is one
// piece of ground, which is worked out over the regions (a few per cluster), never over the tiles.
//
// When a tile opens or closes, only its cluster's tiles are labelled again, only the edges along that cluster's sides are listed again, and
// the pieces are worked out again only if the regions or their edges came out different. Everything else is left as it was. So a wall built
// in a corner of the map costs a few hundred tiles of work, not a pass over the map; and what a path search needs to know (is the goal
// reachable at all, so that it need not search the whole world for a way that is not there) is answered from the regions.

struct Terrain::Nav {
    static constexpr int CL = 16;
    struct Edge {
        uint8_t a, b, dir;   // a region of this cluster, the region it meets in the next cluster along x (dir 0) or along z (dir 1)
        bool operator==(const Edge& o) const { return a == o.a && b == o.b && dir == o.dir; }
    };

    int size = 0, cw = 0;
    std::vector<uint8_t> label;                 // by tile: 0 closed, else its local region (1 up to count of its cluster)
    std::vector<uint8_t> count;                 // by cluster: how many regions it has
    std::vector<uint8_t> dirty;                 // by cluster: its tiles changed since it was labelled
    std::vector<int> dirtyList;
    std::vector<std::vector<Edge>> edges;       // by cluster
    std::vector<int> offset;                    // by cluster: where its regions begin in comp
    std::vector<int> comp;                      // by region (offset[cluster] + local - 1): its piece of ground
    bool piecesStale = true;
    std::unordered_set<long long> stash;        // the closed tiles as they stood when they were all cleared (Terrain::clearBlocked)
    bool stashed = false;

    explicit Nav(int mapSize) : size(mapSize) {
        cw = (size + CL - 1) / CL;
        label.assign((size_t)size * size, 0);
        count.assign((size_t)cw * cw, 0);
        dirty.assign((size_t)cw * cw, 1);
        edges.assign((size_t)cw * cw, {});
        offset.assign((size_t)cw * cw + 1, 0);
        dirtyList.reserve((size_t)cw * cw);
        for (int c = 0; c < cw * cw; ++c) dirtyList.push_back(c);
    }

    void mark(int x, int z) {
        if (x < 0 || z < 0 || x >= size || z >= size) return;
        const int c = (z / CL) * cw + x / CL;
        if (dirty[(size_t)c]) return;
        dirty[(size_t)c] = 1;
        dirtyList.push_back(c);
    }

    // Labels one cluster's open tiles into regions
    void relabel(const Terrain& t, int c) {
        const int x0 = (c % cw) * CL, z0 = (c / cw) * CL;
        const int x1 = std::min(size, x0 + CL), z1 = std::min(size, z0 + CL);
        for (int z = z0; z < z1; ++z) {
            for (int x = x0; x < x1; ++x) label[(size_t)z * size + x] = 0;
        }
        int next = 0;
        std::array<int, CL * CL> stack{};
        for (int z = z0; z < z1; ++z) {
            for (int x = x0; x < x1; ++x) {
                if (label[(size_t)z * size + x] || !t.walkable(x, z)) continue;
                ++next;
                const uint8_t id = (uint8_t)std::min(next, 255);
                int top = 0;
                label[(size_t)z * size + x] = id;
                stack[(size_t)top++] = (z - z0) * CL + (x - x0);
                while (top > 0) {
                    const int at = stack[(size_t)--top];
                    const int ax = x0 + at % CL, az = z0 + at / CL;
                    static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (const auto& d : D) {
                        const int nx = ax + d[0], nz = az + d[1];
                        if (nx < x0 || nz < z0 || nx >= x1 || nz >= z1) continue;
                        if (label[(size_t)nz * size + nx] || !t.walkable(nx, nz)) continue;
                        label[(size_t)nz * size + nx] = id;
                        stack[(size_t)top++] = (nz - z0) * CL + (nx - x0);
                    }
                }
            }
        }
        const uint8_t now = (uint8_t)std::min(next, 255);
        if (count[(size_t)c] != now) piecesStale = true;
        count[(size_t)c] = now;
    }

    // The pairs of regions that meet across the cluster's sides on the +x and +z
    std::vector<Edge> listEdges(int c) const {
        std::vector<Edge> out;
        const int cx = c % cw, cz = c / cw;
        const int x0 = cx * CL, z0 = cz * CL;
        auto add = [&](uint8_t a, uint8_t b, uint8_t dir) {
            const Edge e{ a, b, dir };
            for (const Edge& o : out) {
                if (o == e) return;
            }
            out.push_back(e);
        };
        if (cx + 1 < cw) {
            const int x = std::min(size, x0 + CL) - 1;
            for (int z = z0; z < std::min(size, z0 + CL); ++z) {
                const uint8_t a = label[(size_t)z * size + x], b = label[(size_t)z * size + x + 1];
                if (a && b) add(a, b, 0);
            }
        }
        if (cz + 1 < cw) {
            const int z = std::min(size, z0 + CL) - 1;
            for (int x = x0; x < std::min(size, x0 + CL); ++x) {
                const uint8_t a = label[(size_t)z * size + x], b = label[(size_t)(z + 1) * size + x];
                if (a && b) add(a, b, 1);
            }
        }
        return out;
    }

    int find(int i) {
        while (comp[(size_t)i] != i) {
            comp[(size_t)i] = comp[(size_t)comp[(size_t)i]];
            i = comp[(size_t)i];
        }
        return i;
    }

    // Brings the regions, their edges and the pieces of ground up to date, touching only what has changed
    void flush(const Terrain& t) {
        if (dirtyList.empty() && !piecesStale) return;
        std::vector<int> relink;
        for (int c : dirtyList) {
            relabel(t, c);
            dirty[(size_t)c] = 0;
            relink.push_back(c);
            if (c % cw > 0) relink.push_back(c - 1);   // the cluster before it lists the edges across their shared side
            if (c / cw > 0) relink.push_back(c - cw);
        }
        dirtyList.clear();
        std::sort(relink.begin(), relink.end());
        relink.erase(std::unique(relink.begin(), relink.end()), relink.end());
        for (int c : relink) {
            std::vector<Edge> now = listEdges(c);
            if (now != edges[(size_t)c]) {
                edges[(size_t)c] = std::move(now);
                piecesStale = true;
            }
        }
        if (!piecesStale) return;
        // The pieces of ground: the regions joined across their clusters' sides, over the regions only
        int total = 0;
        for (int c = 0; c < cw * cw; ++c) {
            offset[(size_t)c] = total;
            total += count[(size_t)c];
        }
        offset[(size_t)cw * cw] = total;
        comp.resize((size_t)total);
        for (int i = 0; i < total; ++i) comp[(size_t)i] = i;
        for (int c = 0; c < cw * cw; ++c) {
            for (const Edge& e : edges[(size_t)c]) {
                const int n = e.dir == 0 ? c + 1 : c + cw;
                if (e.a > count[(size_t)c] || e.b > count[(size_t)n]) continue;
                const int ra = find(offset[(size_t)c] + e.a - 1), rb = find(offset[(size_t)n] + e.b - 1);
                if (ra != rb) comp[(size_t)ra] = rb;
            }
        }
        for (int i = 0; i < total; ++i) comp[(size_t)i] = find(i);
        piecesStale = false;
    }

    int pieceAt(int x, int z) const {
        if (x < 0 || z < 0 || x >= size || z >= size) return 0;
        const uint8_t l = label[(size_t)z * size + x];
        if (!l) return 0;
        const int c = (z / CL) * cw + x / CL;
        if (l > count[(size_t)c]) return 0;
        return comp[(size_t)(offset[(size_t)c] + l - 1)] + 1;
    }
};

namespace {
constexpr int MOST_SIZE_WITH_NAV = 2048;   // as for Terrain's areas: a bigger map does not track it
}

void Terrain::prepareNav() const {
    if (size_ <= 0 || size_ > MOST_SIZE_WITH_NAV) return;
    if (!nav_) nav_ = std::make_shared<Nav>(size_);
    nav_->flush(*this);
}

bool Terrain::connected(int ax, int az, int bx, int bz) const {
    if (size_ > MOST_SIZE_WITH_NAV) return areaOf(ax, az) == areaOf(bx, bz);
    prepareNav();
    if (!nav_) return true;
    const int a = nav_->pieceAt(ax, az), b = nav_->pieceAt(bx, bz);
    return a != 0 && a == b;
}

int Terrain::navPieceAt(int x, int z) const {
    if (size_ > MOST_SIZE_WITH_NAV) return areaAt(x, z);
    prepareNav();
    return nav_ ? nav_->pieceAt(x, z) : areaAt(x, z);
}

void Terrain::navTileChanged(int x, int z) {
    if (nav_) nav_->mark(x, z);
}

void Terrain::navStashBlocked() {
    if (!nav_ || nav_->stashed) return;
    nav_->stash = blocked_;
    nav_->stashed = true;
}

void Terrain::navSettleBlocked() {
    if (!nav_ || !nav_->stashed) return;
    // Tiles newly closed were marked as they were closed; the ones that were closed and are not any more are marked now
    for (const long long key : nav_->stash) {
        if (!blocked_.count(key)) nav_->mark((int)(key % 1000003LL), (int)(key / 1000003LL));
    }
    nav_->stash.clear();
    nav_->stashed = false;
}
