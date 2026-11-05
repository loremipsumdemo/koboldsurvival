#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "chunk.h"
#include "geometry.h"
#include "grid.h"
#include "map_file.h"
#include "world_format.h"

// A unit keeps this far from any blocked tile, so its center never sits against a wall
constexpr float AGENT_CLEARANCE = 0.3f;

// Terrain holds the ground of the level being played: heights, walkability and ground types,
// in chunks of CHUNK_TILES tiles. A level is one of two kinds:
//   - a whole map in memory (a premade map, or an older saved map), with every chunk loaded, or
//   - an open world folder, where chunks are read in as they are needed around the camera and
//     the units, and dropped again when they are far away (stream()).
// Outside the loaded chunks the ground counts as sea level and as blocked for movement.
class Terrain {
public:
    explicit Terrain(int size = DEFAULT_MAP_SIZE);

    // Replaces the level with a whole map in memory. Every chunk is loaded.
    void load(const MapData& m);
    // Opens a world folder. Nothing is read until stream() is called.
    bool openWorld(const std::string& folder, std::string& err);

    // Tiles per side of the whole level, and chunks per side
    int size() const { return size_; }
    // The folder and header of a streamed world (the folder is empty for a whole map in memory)
    const std::string& folder() const { return folder_; }
    const WorldInfo& worldInfo() const { return info_; }
    int chunksPerSide() const { return cps_; }
    // The chunk at (cx, cz), or nullptr if it is not loaded
    const ChunkData* chunkAt(int cx, int cz) const;

    // Loads the chunks within `radius` chunks of each focus point (world units), nearest first,
    // at most maxLoads of them (0 for no limit). Chunks more than radius + 1 chunks from every
    // focus are unloaded. Returns the number of chunks loaded plus unloaded. Does nothing for
    // a whole map in memory.
    int stream(const std::vector<Vec2>& focuses, int radius, int maxLoads);
    bool streamed() const { return streamed_; }

    // Corner heights in world units, bilinear within a tile. Sea level is 0.
    float heightAt(float x, float z) const;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < size_ && y < size_; }
    // A tile is walkable when its chunk is loaded and the tile is not blocked
    bool walkable(int x, int y) const;
    // Walkability and center height of a tile, loaded or not. For a streamed world an unloaded chunk
    // is read from the folder for this call only, so nothing extra stays in memory. False if the tile
    // is outside the level or its chunk can't be read.
    bool tileGround(int x, int y, bool& walkable, float& height) const;
    // The look and surface tiles the level is drawn with. A whole map in memory uses the default.
    const WorldSurface& surface() const { return surface_; }
    // Changes when the surface changes (a new level), so the renderer knows to upload it again
    uint32_t surfaceVersion() const { return surfaceVersion_; }
    // Heights are held in world units. The file's height scale is applied when chunks are read, so
    // this is 1 for every level; it is kept for the shaders that take a depth scale.
    float heightScale() const { return heightScale_; }
    // Height of the seabed beyond the map, in world units
    float floorY() const { return floorY_; }
    // Ground type of a tile, or Biome::None
    Biome biome(int x, int y) const;

    // Clearance queries, in world units
    bool isFree(float x, float z) const;
    Vec2 snap(Vec2 p) const;

    // Smooth path from start to goal, not including start. If the goal can't be reached, the path
    // goes to the nearest reachable point. Empty if the unit is already there or can't move.
    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const;

private:
    // The chunk holding tile (x, y), or nullptr
    const ChunkData* tileChunk(int x, int y) const;

    int size_ = 0;
    int cps_ = 0;
    std::vector<std::unique_ptr<ChunkData>> chunks_;   // chunk id = cz * cps_ + cx
    bool streamed_ = false;
    std::string folder_;
    WorldInfo info_;
    uint32_t nextVersion_ = 1;
    WorldSurface surface_;
    uint32_t surfaceVersion_ = 0;
    float heightScale_ = 1.0f;
    float floorY_ = -1.0f;
};
