#pragma once
#include <cstdint>
#include <vector>
#include "grid.h"
#include "map.h"
#include "navmesh.h"

// Terrain is a resource, not entities: a million tiles as ECS entities would
// be far too heavy. It holds the ground heights of the loaded map, the tile
// walkability and ground types that the map is drawn with, and the navmesh that all
// movement uses. Its size is the size of the loaded map.
class Terrain {
public:
    explicit Terrain(int size = DEFAULT_MAP_SIZE);

    // Replaces the whole map: heights, walkability and ground types
    void load(const MapData& m);
    // The map as it stands, for saving
    MapData toMapData() const;

    // Tiles per side, and corners per side
    int size() const { return size_; }
    int vertSize() const { return size_ + 1; }

    // Corner heights, (size+1)^2, in world units
    const std::vector<float>& heights() const { return heights_; }

    // Bilinear height sample at a world position
    float heightAt(float x, float z) const;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < size_ && y < size_; }
    bool walkable(int x, int y) const { return inBounds(x, y) && walkable_[(size_t)y * size_ + x]; }
    // Blocking a tile rebuilds the navmesh. Doors use this.
    void setWalkable(int x, int y, bool w);

    // Ground type of a tile, or BIOME_NONE
    uint8_t biome(int x, int y) const { return inBounds(x, y) ? biome_[(size_t)y * size_ + x] : BIOME_NONE; }

    // Navmesh queries, in world units
    bool isFree(float x, float z) const { return navmesh_.isFree(x, z); }
    Vec2 snap(Vec2 p) const { return navmesh_.snap(p); }

    // Smooth path from start to goal, not including start. Empty if unreachable or already there.
    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const { return navmesh_.findPath(start, goal); }

private:
    void rebuildNavmesh();

    int size_ = DEFAULT_MAP_SIZE;
    std::vector<float> heights_;
    std::vector<uint8_t> walkable_;
    std::vector<uint8_t> biome_;
    Navmesh navmesh_;
};
