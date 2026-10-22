#include "navmesh.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace {

// Obstacle edges are shrunk by this much when testing a point, so a unit standing right on
// the clearance line isn't rejected by float rounding
constexpr float EDGE_EPS = 1e-3f;

float dist2(Vec2 a, Vec2 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

float dist(Vec2 a, Vec2 b) { return std::sqrt(dist2(a, b)); }

// Positive when c is to the left of the direction a->b (x and z read as a standard 2D plane)
float side(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

// True if (x, z) is inside the box, shrunk inwards by eps
bool insideBox(const NavBox& b, float x, float z, float eps) {
    return x > b.x0 + eps && x < b.x1 - eps && z > b.z0 + eps && z < b.z1 - eps;
}

float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// A portal is the edge shared by two neighbouring quads, with its ends ordered as seen
// while walking through it: left end first, right end second.
struct Portal {
    Vec2 left, right;
};

} // namespace

void Navmesh::build(NavBox domain, const std::vector<NavBox>& obstacles, float clearance) {
    clearance_ = clearance;

    // Agent centres may go anywhere in the map, but must stay clearance away from its edge
    domain_ = { domain.x0 + clearance, domain.z0 + clearance,
                domain.x1 - clearance, domain.z1 - clearance };

    // Grow each obstacle by the clearance (a square, so corners are kept conservative),
    // then clip it to the domain
    blocked_.clear();
    for (const NavBox& o : obstacles) {
        NavBox g{ o.x0 - clearance, o.z0 - clearance, o.x1 + clearance, o.z1 + clearance };
        g.x0 = std::max(g.x0, domain_.x0);
        g.z0 = std::max(g.z0, domain_.z0);
        g.x1 = std::min(g.x1, domain_.x1);
        g.z1 = std::min(g.z1, domain_.z1);
        if (g.x0 < g.x1 && g.z0 < g.z1) blocked_.push_back(g);
    }

    // Quad edges: the domain edges plus every grown obstacle edge. Each quad between two
    // neighbouring edges is either entirely free or entirely blocked.
    xs_ = { domain_.x0, domain_.x1 };
    zs_ = { domain_.z0, domain_.z1 };
    for (const NavBox& b : blocked_) {
        xs_.push_back(b.x0);
        xs_.push_back(b.x1);
        zs_.push_back(b.z0);
        zs_.push_back(b.z1);
    }
    auto sortUnique = [](std::vector<float>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    sortUnique(xs_);
    sortUnique(zs_);

    quadsX_ = (int)xs_.size() - 1;
    quadsZ_ = (int)zs_.size() - 1;
    free_.assign((size_t)quadsX_ * quadsZ_, 0);
    for (int j = 0; j < quadsZ_; ++j) {
        for (int i = 0; i < quadsX_; ++i) {
            float cx = 0.5f * (xs_[i] + xs_[i + 1]);
            float cz = 0.5f * (zs_[j] + zs_[j + 1]);
            bool blocked = false;
            for (const NavBox& b : blocked_) {
                if (insideBox(b, cx, cz, 0.0f)) { blocked = true; break; }
            }
            free_[(size_t)j * quadsX_ + i] = blocked ? 0 : 1;
        }
    }
}

bool Navmesh::isFree(float x, float z) const {
    if (x < domain_.x0 || x > domain_.x1 || z < domain_.z0 || z > domain_.z1) return false;
    for (const NavBox& b : blocked_) {
        if (insideBox(b, x, z, EDGE_EPS)) return false;
    }
    return true;
}

Vec2 Navmesh::snap(Vec2 p) const {
    if (isFree(p.x, p.z)) return p;

    // The nearest free point lies on an obstacle side or the domain edge. Test the nearest
    // point on each of those sides and keep the closest one that is free.
    Vec2 best{ domain_.x0, domain_.z0 };
    float bestD = std::numeric_limits<float>::infinity();
    auto consider = [&](float x, float z) {
        if (!isFree(x, z)) return;
        float d = dist2(p, Vec2{ x, z });
        if (d < bestD) { bestD = d; best = { x, z }; }
    };

    for (const NavBox& b : blocked_) {
        float cx = clampf(p.x, b.x0, b.x1);
        float cz = clampf(p.z, b.z0, b.z1);
        consider(b.x0, cz);
        consider(b.x1, cz);
        consider(cx, b.z0);
        consider(cx, b.z1);
    }
    float dx = clampf(p.x, domain_.x0, domain_.x1);
    float dz = clampf(p.z, domain_.z0, domain_.z1);
    consider(domain_.x0, dz);
    consider(domain_.x1, dz);
    consider(dx, domain_.z0);
    consider(dx, domain_.z1);
    return best;
}

int Navmesh::quadAt(Vec2 p) const {
    for (int j = 0; j < quadsZ_; ++j) {
        if (p.z < zs_[j] - EDGE_EPS || p.z > zs_[j + 1] + EDGE_EPS) continue;
        for (int i = 0; i < quadsX_; ++i) {
            if (!free_[(size_t)j * quadsX_ + i]) continue;
            if (p.x < xs_[i] - EDGE_EPS || p.x > xs_[i + 1] + EDGE_EPS) continue;
            return j * quadsX_ + i;
        }
    }
    return -1;
}

std::vector<Vec2> Navmesh::findPath(Vec2 start, Vec2 goal) const {
    Vec2 s = snap(start);
    Vec2 g = snap(goal);
    int from = quadAt(s);
    int to = quadAt(g);
    if (from < 0 || to < 0) return {};

    if (from == to) {
        if (dist2(s, g) < 1e-8f) return {};
        return { g };
    }

    auto center = [&](int id) {
        int i = id % quadsX_, j = id / quadsX_;
        return Vec2{ 0.5f * (xs_[i] + xs_[i + 1]), 0.5f * (zs_[j] + zs_[j + 1]) };
    };

    // A* over quads. Cost is the distance between quad centres; the heuristic is the
    // straight distance to the goal.
    int count = quadsX_ * quadsZ_;
    std::vector<float> gScore(count, std::numeric_limits<float>::infinity());
    std::vector<int> parent(count, -1);
    std::vector<uint8_t> closed(count, 0);
    using Node = std::pair<float, int>;   // (f score, quad id)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    gScore[from] = 0.0f;
    open.push({ dist(center(from), g), from });
    bool found = false;
    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == to) { found = true; break; }

        int i = cur % quadsX_, j = cur / quadsX_;
        const int neighbours[4][2] = { { i - 1, j }, { i + 1, j }, { i, j - 1 }, { i, j + 1 } };
        for (const auto& nb : neighbours) {
            int ni = nb[0], nj = nb[1];
            if (ni < 0 || nj < 0 || ni >= quadsX_ || nj >= quadsZ_) continue;
            int nk = nj * quadsX_ + ni;
            if (!free_[nk] || closed[nk]) continue;

            float g2 = gScore[cur] + dist(center(cur), center(nk));
            if (g2 < gScore[nk]) {
                gScore[nk] = g2;
                parent[nk] = cur;
                open.push({ g2 + dist(center(nk), g), nk });
            }
        }
    }
    if (!found) return {};

    // Quads from start to goal, in order
    std::vector<int> seq;
    for (int k = to; k != -1; k = parent[k]) seq.push_back(k);
    std::reverse(seq.begin(), seq.end());

    // Portals between consecutive quads, with the start and goal as the first and last
    std::vector<Portal> portals;
    portals.push_back({ s, s });
    for (size_t k = 0; k + 1 < seq.size(); ++k) {
        int a = seq[k], b = seq[k + 1];
        int ai = a % quadsX_, aj = a / quadsX_;
        int bi = b % quadsX_, bj = b / quadsX_;

        // The shared edge is a vertical line when the quads sit side by side, else horizontal
        Vec2 p, q;
        if (bi != ai) {
            float x = bi > ai ? xs_[ai + 1] : xs_[ai];
            p = { x, zs_[aj] };
            q = { x, zs_[aj + 1] };
        } else {
            float z = bj > aj ? zs_[aj + 1] : zs_[aj];
            p = { xs_[ai], z };
            q = { xs_[ai + 1], z };
        }

        // Left is the side where side(ca, cb, point) is positive, seen walking from a to b
        Vec2 ca = center(a), cb = center(b);
        if (side(ca, cb, p) >= 0.0f) portals.push_back({ p, q });
        else                         portals.push_back({ q, p });
    }
    portals.push_back({ g, g });

    // Funnel algorithm: pull the path tight through the portals. The apex is the current
    // corner; the left and right boundaries are the two rays that bound what it can see.
    std::vector<Vec2> path;
    Vec2 apex = s, left = s, right = s;
    size_t apexI = 0, leftI = 0, rightI = 0;
    for (size_t i = 1; i < portals.size(); ++i) {
        Vec2 pl = portals[i].left, pr = portals[i].right;

        // Tighten the right boundary
        if (side(apex, right, pr) >= 0.0f) {
            if (side(apex, left, pr) <= 0.0f) {
                right = pr;
                rightI = i;
            } else {
                // The new right point went past the left boundary, so the left corner is a turn
                path.push_back(left);
                apex = left;
                apexI = leftI;
                left = right = apex;
                leftI = rightI = apexI;
                i = apexI;
                continue;
            }
        }

        // Tighten the left boundary
        if (side(apex, left, pl) <= 0.0f) {
            if (side(apex, right, pl) >= 0.0f) {
                left = pl;
                leftI = i;
            } else {
                // The new left point went past the right boundary, so the right corner is a turn
                path.push_back(right);
                apex = right;
                apexI = rightI;
                left = right = apex;
                leftI = rightI = apexI;
                i = apexI;
                continue;
            }
        }
    }
    path.push_back(g);

    // Drop repeats, and the start itself
    std::vector<Vec2> out;
    Vec2 prev = s;
    for (Vec2 p : path) {
        if (dist2(p, prev) > 1e-10f) {
            out.push_back(p);
            prev = p;
        }
    }
    return out;
}
