#pragma once
#include <cstdint>
#include <vector>
#include "grid.h"

// Terrain is a resource, not entities: a million tiles as ECS entities would
// be far too heavy. It holds height and walkability data plus pathfinding.
class Terrain {
public:
    void generate(unsigned seed);

    // Corner heights, VERT_SIZE x VERT_SIZE, each in [-1, 1]
    const std::vector<float>& heights() const { return heights_; }

    // Bilinear height sample at a world position
    float heightAt(float x, float z) const;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < MAP_SIZE && y < MAP_SIZE; }
    bool walkable(int x, int y) const { return inBounds(x, y) && walkable_[y * MAP_SIZE + x]; }
    void setWalkable(int x, int y, bool w) { if (inBounds(x, y)) walkable_[y * MAP_SIZE + x] = w; }

    // Closest walkable tile to g, searching outward (returns g if nothing is found)
    GridPos nearestWalkable(GridPos g) const;

    // 8-way A* from start to goal. Returns the tiles after start, or empty if unreachable.
    std::vector<GridPos> findPath(GridPos start, GridPos goal);

private:
    std::vector<float> heights_;
    std::vector<uint8_t> walkable_;

    // A* scratch space, reused between searches. stamp_/closed_ hold a
    // generation number so nothing has to be cleared between searches.
    std::vector<float> gScore_;
    std::vector<int> parent_;
    std::vector<uint32_t> stamp_, closed_;
    uint32_t gen_ = 0;
};
