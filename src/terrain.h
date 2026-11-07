#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "chunk.h"
#include "chunk_source.h"
#include "geometry.h"
#include "grid.h"
#include "world_format.h"

constexpr float AGENT_CLEARANCE = 0.3f;

// How many chunks of ground Terrain keeps in memory for the simulation. Asking for another drops the one unused longest,
// and it comes back from the source when next needed, so the map can be far larger than this.
constexpr size_t MAX_GROUND_CHUNKS = 1024;

// Terrain holds the ground of the level being played: heights, walkability and ground types, in chunks of CHUNK_TILES
// tiles, which it asks its ChunkSource for. It starts as a blank flat map.
// Two sets of chunks are held. For drawing, the ones around the camera and the units (stream()). For the simulation, the
// ground of whichever chunks were last asked about, so units, paths and sight work the same everywhere, whatever is drawn.
class Terrain {
public:
    explicit Terrain(int size = DEFAULT_MAP_SIZE);
    explicit Terrain(std::shared_ptr<const ChunkSource> source);

    // On failure, the terrain is left as it was
    bool openWorld(const std::string& folder, std::string& err);
    void open(std::shared_ptr<const ChunkSource> source);

    int size() const { return size_; }
    const std::shared_ptr<const ChunkSource>& source() const { return source_; }
    int chunksPerSide() const { return cps_; }
    size_t groundChunksHeld() const { return groundHeld_.size(); }
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
    // The piece of open ground a tile is in, numbered from 1; 0 for closed ground. On a map too big to work them out, all
    // open ground is piece 1.
    int areaAt(int x, int y) const { return walkable(x, y) ? areaOf(x, y) : 0; }
    // How many tiles each piece has, by its number. Empty on a map too big to work them out.
    std::vector<int> areaSizes() const;
    Vec2 snap(Vec2 p) const;

    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const;
    bool lineClear(Vec2 a, Vec2 b) const;
    bool sightClear(float ax, float ay, float az, float bx, float by, float bz) const;

private:
    struct GroundChunk {
        std::vector<float> heights;
        std::vector<uint8_t> walk;
        uint64_t lastUsed = 0;
    };
    const GroundChunk& groundOf(int cx, int cz) const;
    void findAreas();
    int areaOf(int x, int y) const;

    int size_ = 0;
    int cps_ = 0;
    std::vector<std::unique_ptr<ChunkData>> chunks_;
    std::vector<int> drawn_;
    mutable std::vector<std::unique_ptr<GroundChunk>> ground_;
    mutable std::vector<int> groundHeld_;
    mutable uint64_t groundClock_ = 0;
    std::vector<int> groundArea_;
    bool streamed_ = false;
    std::shared_ptr<const ChunkSource> source_;
    uint32_t nextVersion_ = 1;
    WorldSurface surface_;
    uint32_t surfaceVersion_ = 0;
    float heightScale_ = 1.0f;
    float floorY_ = -1.0f;
};
