#include "terrain.h"
#include <algorithm>
#include <cmath>

Terrain::Terrain() {
    heights_.assign((size_t)VERT_SIZE * VERT_SIZE, 0.0f);
    walkable_.assign((size_t)MAP_SIZE * MAP_SIZE, 1);
    rebuildNavmesh();
}

void Terrain::load(const std::vector<float>& heights) {
    heights_.assign((size_t)VERT_SIZE * VERT_SIZE, 0.0f);
    std::copy_n(heights.begin(), std::min(heights.size(), heights_.size()), heights_.begin());
    walkable_.assign((size_t)MAP_SIZE * MAP_SIZE, 1);
    rebuildNavmesh();
}

void Terrain::setVertexHeight(int x, int z, float h) {
    if (x < 0 || z < 0 || x >= VERT_SIZE || z >= VERT_SIZE) return;
    heights_[(size_t)z * VERT_SIZE + x] = std::clamp(h, -HEIGHT_SCALE, HEIGHT_SCALE);
}

void Terrain::paint(float hx, float hz, float radius, float amount) {
    if (radius <= 0.0f) return;
    const int x0 = std::max(0, (int)std::floor(hx - radius));
    const int x1 = std::min(VERT_SIZE - 1, (int)std::ceil(hx + radius));
    const int z0 = std::max(0, (int)std::floor(hz - radius));
    const int z1 = std::min(VERT_SIZE - 1, (int)std::ceil(hz + radius));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = x - hx, dz = z - hz;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d >= radius) continue;
            const float falloff = 1.0f - d / radius;
            setVertexHeight(x, z, heights_[(size_t)z * VERT_SIZE + x] + amount * falloff * falloff);
        }
    }
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

void Terrain::setWalkable(int x, int y, bool w) {
    if (!inBounds(x, y)) return;
    walkable_[y * MAP_SIZE + x] = w;
    rebuildNavmesh();
}

void Terrain::rebuildNavmesh() {
    // Each run of blocked tiles along a row becomes one obstacle box
    std::vector<NavBox> blocked;
    for (int y = 0; y < MAP_SIZE; ++y) {
        int x = 0;
        while (x < MAP_SIZE) {
            if (walkable_[y * MAP_SIZE + x]) { ++x; continue; }
            int start = x;
            while (x < MAP_SIZE && !walkable_[y * MAP_SIZE + x]) ++x;
            blocked.push_back({ (float)start, (float)y, (float)x, (float)y + 1.0f });
        }
    }

    // Units keep this far from any blocked tile, so their centres never sit inside a wall
    constexpr float AGENT_CLEARANCE = 0.3f;
    navmesh_.build({ 0.0f, 0.0f, (float)MAP_SIZE, (float)MAP_SIZE }, blocked, AGENT_CLEARANCE);
}
