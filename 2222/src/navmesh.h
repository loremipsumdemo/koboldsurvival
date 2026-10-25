#pragma once
#include <cstdint>
#include <vector>

// A point on the ground, in world x and z
struct Vec2 {
    float x = 0.0f, z = 0.0f;
};

// An axis-aligned box on the ground, in world units
struct NavBox {
    float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
};

// Navigation mesh for free-form movement. Blocked boxes are grown by the agent clearance,
// and the free space is cut into convex quads along the grown edges. Paths are A* over
// those quads, then smoothed with the funnel algorithm into straight segments, so units
// walk in any direction and turn at obstacle corners rather than at tile centres.
class Navmesh {
public:
    // domain: the area agents may occupy. obstacles: boxes they must stay clear of.
    // clearance: how far an agent's centre must stay from every obstacle.
    void build(NavBox domain, const std::vector<NavBox>& obstacles, float clearance);

    // True if an agent centred at (x, z) is clear of every obstacle and the domain edge
    bool isFree(float x, float z) const;

    // Nearest free point to p, or p itself if it is already free
    Vec2 snap(Vec2 p) const;

    // Smooth path from start to goal, not including start. The goal is snapped to the mesh
    // first. Returns empty if the goal can't be reached or the agent is already there.
    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const;

private:
    int quadAt(Vec2 p) const;   // a free quad containing p, or -1

    NavBox domain_;
    float clearance_ = 0.0f;
    std::vector<NavBox> blocked_;    // obstacles grown by the clearance, clipped to the domain
    std::vector<float> xs_, zs_;     // every quad edge, sorted; quads sit between neighbours
    std::vector<uint8_t> free_;      // one flag per quad, row-major, quadsX_ per row
    int quadsX_ = 0, quadsZ_ = 0;
};
