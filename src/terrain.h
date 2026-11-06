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

constexpr float AGENT_CLEARANCE = 0.3f;

// Terrain holds the ground of the level being played: heights, walkability and ground types,
// in chunks of CHUNK_TILES tiles. It starts as a blank flat map. openWorld opens a world folder, whose chunks are read in
// as they are needed around the camera and the units, and dropped again when they are far away (stream()).
// Loaded chunks are for drawing. Heights and walkability are also held for the whole level from the moment it opens,
// so units, paths and sight work the same everywhere, whatever is loaded.
class Terrain {
public:
    explicit Terrain(int size = DEFAULT_MAP_SIZE);

    bool openWorld(const std::string& folder, std::string& err);

    int size() const { return size_; }
    const std::string& folder() const { return folder_; }
    const WorldInfo& worldInfo() const { return info_; }
    int chunksPerSide() const { return cps_; }
    const ChunkData* chunkAt(int cx, int cz) const;

    int stream(const std::vector<Vec2>& focuses, int radius, int maxLoads);
    bool streamed() const { return streamed_; }

    float heightAt(float x, float z) const;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < size_ && y < size_; }
    bool walkable(int x, int y) const;
    bool tileGround(int x, int y, bool& walkable, float& height) const;
    const WorldSurface& surface() const { return surface_; }
    uint32_t surfaceVersion() const { return surfaceVersion_; }
    float heightScale() const { return heightScale_; }
    float floorY() const { return floorY_; }

    bool isFree(float x, float z) const;
    Vec2 snap(Vec2 p) const;

    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const;
    bool lineClear(Vec2 a, Vec2 b) const;
    bool sightClear(float ax, float ay, float az, float bx, float by, float bz) const;

private:
    void load(const MapData& m);
    void keepGround(const ChunkData& c);
    void findAreas();
    int areaOf(int x, int y) const;

    int size_ = 0;
    int cps_ = 0;
    std::vector<std::unique_ptr<ChunkData>> chunks_;
    std::vector<float> groundHeights_;
    std::vector<uint8_t> groundWalk_;
    std::vector<int> groundArea_;
    bool streamed_ = false;
    std::string folder_;
    WorldInfo info_;
    uint32_t nextVersion_ = 1;
    WorldSurface surface_;
    uint32_t surfaceVersion_ = 0;
    float heightScale_ = 1.0f;
    float floorY_ = -1.0f;
};
