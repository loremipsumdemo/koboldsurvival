#include "terrain.h"
#include <algorithm>
#include <cmath>

Terrain::Terrain(int size) {
    load(flatMap(size));
}

void Terrain::load(const MapData& m) {
    size_ = m.size;
    const size_t vs = (size_t)size_ + 1, tiles = (size_t)size_ * size_;
    heights_.assign(vs * vs, 0.0f);
    std::copy_n(m.heights.begin(), std::min(m.heights.size(), heights_.size()), heights_.begin());
    walkable_.assign(tiles, 1);
    std::copy_n(m.walkable.begin(), std::min(m.walkable.size(), tiles), walkable_.begin());
    biome_.assign(tiles, BIOME_NONE);
    std::copy_n(m.biome.begin(), std::min(m.biome.size(), tiles), biome_.begin());
    rebuildNavmesh();
}

MapData Terrain::toMapData() const {
    MapData m;
    m.size = size_;
    m.heights = heights_;
    m.walkable = walkable_;
    m.biome = biome_;
    return m;
}

float Terrain::heightAt(float x, float z) const {
    const int vs = vertSize();
    x = std::clamp(x, 0.0f, (float)size_ - 0.001f);
    z = std::clamp(z, 0.0f, (float)size_ - 0.001f);
    int x0 = (int)x, z0 = (int)z;
    float fx = x - x0, fz = z - z0;

    auto h = [&](int i, int j) { return heights_[(size_t)j * vs + i]; };
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
    walkable_[(size_t)y * size_ + x] = w;
    rebuildNavmesh();
}

void Terrain::rebuildNavmesh() {
    // Each run of blocked tiles along a row becomes one obstacle box
    std::vector<NavBox> blocked;
    for (int y = 0; y < size_; ++y) {
        int x = 0;
        while (x < size_) {
            if (walkable_[(size_t)y * size_ + x]) { ++x; continue; }
            int start = x;
            while (x < size_ && !walkable_[(size_t)y * size_ + x]) ++x;
            blocked.push_back({ (float)start, (float)y, (float)x, (float)y + 1.0f });
        }
    }

    // Units keep this far from any blocked tile, so their centres never sit inside a wall
    constexpr float AGENT_CLEARANCE = 0.3f;
    navmesh_.build({ 0.0f, 0.0f, (float)size_, (float)size_ }, blocked, AGENT_CLEARANCE);
}
