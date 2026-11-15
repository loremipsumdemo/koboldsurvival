#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
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
//
// The regions are also a coarse map for long walks (hierarchical path finding): a search over the regions, from the middle of one to the
// middle of the next, finds the corridor of regions a walk goes through, and the tile search is then kept to that corridor and the
// regions beside it (so the walk can still cut corners). A walk across the map so searches a few hundred regions and a strip of tiles
// along the way, rather than every tile around it.

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
    // The middle of each region (the average of its tiles), by cluster and local region; worked out as a cluster is labelled
    std::vector<std::vector<std::array<float, 2>>> centers;
    std::vector<int> clusterOf;                 // by region: its cluster (made with the pieces)
    // The region search's own numbers, kept between searches, each region marked with the search that last touched it
    std::vector<float> cost;
    std::vector<int> from;
    std::vector<uint32_t> seen, done, allow;
    uint32_t search = 0, allowed = 0;

    explicit Nav(int mapSize) : size(mapSize) {
        cw = (size + CL - 1) / CL;
        label.assign((size_t)size * size, 0);
        count.assign((size_t)cw * cw, 0);
        dirty.assign((size_t)cw * cw, 1);
        edges.assign((size_t)cw * cw, {});
        centers.assign((size_t)cw * cw, {});
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
        // Each region's middle
        std::vector<std::array<float, 2>>& mids = centers[(size_t)c];
        mids.assign(now, { 0.0f, 0.0f });
        std::vector<int> tiles(now, 0);
        for (int z = z0; z < z1; ++z) {
            for (int x = x0; x < x1; ++x) {
                const uint8_t l = label[(size_t)z * size + x];
                if (!l) continue;
                mids[(size_t)l - 1][0] += (float)x + 0.5f;
                mids[(size_t)l - 1][1] += (float)z + 0.5f;
                ++tiles[(size_t)l - 1];
            }
        }
        for (size_t i = 0; i < mids.size(); ++i) {
            if (tiles[i] > 0) {
                mids[i][0] /= (float)tiles[i];
                mids[i][1] /= (float)tiles[i];
            }
        }
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
        clusterOf.resize((size_t)total);
        for (int c = 0; c < cw * cw; ++c) {
            for (int i = offset[(size_t)c]; i < offset[(size_t)c + 1]; ++i) clusterOf[(size_t)i] = c;
        }
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

    // The region a tile is in (an index over every cluster's regions), or -1 for a closed tile
    int regionAt(int x, int z) const {
        if (x < 0 || z < 0 || x >= size || z >= size) return -1;
        const uint8_t l = label[(size_t)z * size + x];
        if (!l) return -1;
        const int c = (z / CL) * cw + x / CL;
        if (l > count[(size_t)c]) return -1;
        return offset[(size_t)c] + l - 1;
    }

    // The regions beside region r (one may come more than once)
    template <typename Fn>
    void eachNeighbor(int r, Fn fn) const {
        const int c = clusterOf[(size_t)r];
        const int l = r - offset[(size_t)c] + 1;
        auto other = [&](int n, int ln) {
            if (ln >= 1 && ln <= count[(size_t)n]) fn(offset[(size_t)n] + ln - 1);
        };
        for (const Edge& e : edges[(size_t)c]) {
            if (e.a == l) other(e.dir == 0 ? c + 1 : c + cw, e.b);
        }
        if (c % cw > 0) {
            for (const Edge& e : edges[(size_t)(c - 1)]) {
                if (e.dir == 0 && e.b == l) other(c - 1, e.a);
            }
        }
        if (c / cw > 0) {
            for (const Edge& e : edges[(size_t)(c - cw)]) {
                if (e.dir == 1 && e.b == l) other(c - cw, e.a);
            }
        }
    }

    std::array<float, 2> centerOf(int r) const {
        const int c = clusterOf[(size_t)r];
        const size_t i = (size_t)(r - offset[(size_t)c]);
        return i < centers[(size_t)c].size() ? centers[(size_t)c][i] : std::array<float, 2>{ 0.0f, 0.0f };
    }

    // The regions a walk from one tile to the other goes through, found over the regions (A* from middle to middle), and the regions
    // beside them, marked for allowedAt. False if there is no way.
    bool corridor(int sx, int sz, int gx, int gz) {
        const int a = regionAt(sx, sz), b = regionAt(gx, gz);
        if (a < 0 || b < 0 || comp[(size_t)a] != comp[(size_t)b]) return false;
        const size_t total = comp.size();
        if (cost.size() != total) {
            cost.assign(total, 0.0f);
            from.assign(total, -1);
            seen.assign(total, 0);
            done.assign(total, 0);
            allow.assign(total, 0);
            search = allowed = 0;
        }
        if (++search == 0) {
            std::fill(seen.begin(), seen.end(), 0u);
            std::fill(done.begin(), done.end(), 0u);
            search = 1;
        }
        const std::array<float, 2> goal = centerOf(b);
        auto guess = [&](int r) {
            const std::array<float, 2> m = centerOf(r);
            return std::hypot(m[0] - goal[0], m[1] - goal[1]);
        };
        using Entry = std::pair<float, int>;
        static std::vector<Entry> open;
        open.clear();
        const std::greater<Entry> later;
        cost[(size_t)a] = 0.0f;
        from[(size_t)a] = -1;
        seen[(size_t)a] = search;
        open.push_back({ guess(a), a });
        bool reached = false;
        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), later);
            const int r = open.back().second;
            open.pop_back();
            if (done[(size_t)r] == search) continue;
            done[(size_t)r] = search;
            if (r == b) {
                reached = true;
                break;
            }
            const std::array<float, 2> here = centerOf(r);
            eachNeighbor(r, [&](int n) {
                if (done[(size_t)n] == search) return;
                const std::array<float, 2> m = centerOf(n);
                const float g = cost[(size_t)r] + std::hypot(m[0] - here[0], m[1] - here[1]);
                if (seen[(size_t)n] == search && g >= cost[(size_t)n]) return;
                seen[(size_t)n] = search;
                cost[(size_t)n] = g;
                from[(size_t)n] = r;
                open.push_back({ g + guess(n), n });
                std::push_heap(open.begin(), open.end(), later);
            });
        }
        if (!reached) return false;
        if (++allowed == 0) {
            std::fill(allow.begin(), allow.end(), 0u);
            allowed = 1;
        }
        for (int r = b; r >= 0; r = from[(size_t)r]) {
            allow[(size_t)r] = allowed;
            eachNeighbor(r, [&](int n) { allow[(size_t)n] = allowed; });
            if (r == a) break;
        }
        return true;
    }

    bool allowedAt(int x, int z) const {
        const int r = regionAt(x, z);
        return r >= 0 && (size_t)r < allow.size() && allow[(size_t)r] == allowed;
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

bool Terrain::navCorridor(int sx, int sz, int gx, int gz) const {
    if (size_ > MOST_SIZE_WITH_NAV) return false;
    prepareNav();
    return nav_ && nav_->corridor(sx, sz, gx, gz);
}

bool Terrain::navAllowed(int x, int z) const {
    return nav_ && nav_->allowedAt(x, z);
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
