#pragma once
#include <cstdint>
#include <vector>
#include "grid.h"
#include "navmesh.h"

// Terrain is a resource, not entities: a million tiles as ECS entities would
// be far too heavy. It holds the ground heights of the loaded map, the tile
// walkability mask that the building is drawn on, and the navmesh that all
// movement uses. It starts flat until a map is loaded.
class Terrain {
public:
    Terrain();

    // Replaces the ground with these corner heights (VERT_SIZE x VERT_SIZE). Walkability resets.
    void load(const std::vector<float>& heights);

    // Corner heights, VERT_SIZE x VERT_SIZE, in world units
    const std::vector<float>& heights() const { return heights_; }

    // Sets one corner's height. The navmesh only depends on walkability, so it isn't rebuilt.
    void setVertexHeight(int x, int z, float h);

    // Map editor brush: adds amount (may be negative) at the centre (hx, hz), falling off with
    // the square of distance to zero at the radius. Corners stay within +/- HEIGHT_SCALE.
    void paint(float hx, float hz, float radius, float amount);

    // Bilinear height sample at a world position
    float heightAt(float x, float z) const;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < MAP_SIZE && y < MAP_SIZE; }
    bool walkable(int x, int y) const { return inBounds(x, y) && walkable_[y * MAP_SIZE + x]; }

    // Blocking a tile rebuilds the navmesh
    void setWalkable(int x, int y, bool w);

    // Navmesh queries, in world units
    bool isFree(float x, float z) const { return navmesh_.isFree(x, z); }
    Vec2 snap(Vec2 p) const { return navmesh_.snap(p); }

    // Smooth path from start to goal, not including start. Empty if unreachable or already there.
    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const { return navmesh_.findPath(start, goal); }

private:
    void rebuildNavmesh();

    std::vector<float> heights_;
    std::vector<uint8_t> walkable_;
    Navmesh navmesh_;
};
