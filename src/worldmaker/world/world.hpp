#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/math.hpp"
#include "core/tunables.hpp"

namespace wm {


// Terrain is built in square chunks of this many tiles per side.
constexpr int kChunkTiles = 64;
constexpr int kMinWorldTiles = 64;
constexpr int kMaxWorldTiles = 65536;         // largest world size

// Largest size allowed.
inline int MaxWorldTiles() { return kMaxWorldTiles; }

// A new random seed in 1..999999, short enough to read in a file name.
uint32_t RandomSeed();

// Ground types. The first seven keep their old values so saved worlds still load.
// Water is no longer generated; old files may hold it, shown as seabed sand.
enum class Biome : uint8_t {
    Sand = 1,        // seabed sand
    Grass = 2,       // loam, under grass
    Forest = 3,      // forest floor
    Desert = 4,      // desert sand
    Rock = 5,        // bedrock
    Snow = 6,        // ice
    BeachSand = 7,
    Silt = 8,        // deep silt, under the sea
    FrozenClay = 9,
    Clay = 10,
    Gravel = 11,
    AridSoil = 12,
    Scree = 13,
    Floodplain = 14,
    CliffRock = 15,
};

// Terrain settings, one group per stage. The defaults give a typical world.
struct GenParams {
    // Continents and mountains
    float waterShare = 0.42f;    // share of the map under the sea, 0.05 to 0.95
    float relief = 1.0f;         // mountain height, 0.25 to 2
    float rangeCover = 0.5f;     // how much of the land carries mountain ranges, 0 to 1
    float plainShare = 0.0f;     // share of the land pulled down to plains, 0 to 1
    float plainLevel = 0.06f;    // plains height above the sea (normalized)
    // Erosion
    float erosion = 0.6f;       // strength of water erosion, 0 to 1
    int   passes = 24;          // water erosion passes, 8 to 60
    float thermal = 0.3f;       // slope slumping, 0 to 1
    // Climate
    bool  rankedClimate = true; // true: even spread of climates; false: real values
    float warmth = 0.0f;        // -0.3 to 0.3
    float wetness = 0.0f;       // -0.3 to 0.3
    // Ground and coast
    float lowlands = 0.3f;      // pushes land toward the low ground, 0 to 1
    bool  rugged = false;       // rock detail: false soft, true rugged
    float detail = 0.7f;        // fine rock detail, 0 to 1
    float steep = 0.3f;         // how much detail follows steep slopes, 0 to 1
    float beach = 5.0f;         // beach width in tiles, 0 to 20
    float shelf = 10.0f;        // offshore shelf width in tiles, 0 to 30
    // Vegetation
    float cover = 0.7f;         // plant cover, 0 to 1
};

struct GenSettings {
    uint32_t seed = 1;
    int size = 4096;            // tiles per side, a multiple of kChunkTiles
    float heightScale = 500.0f; // world units for a normalized height of 1
    GenParams p;
};

// A generated world. One tile is one world unit. Tile (0, 0) is at
// world (-size/2, -size/2), so the world is centerd on the origin.
struct World {
    int size = 0;
    uint32_t seed = 1;
    float seaLevel = 0.0f;          // world units
    std::vector<float> height;      // size*size, world units, row-major by z
    std::vector<uint8_t> biome;     // Biome values
    std::vector<float> veg;         // 0 to 1 vegetation density
};

// Builds worlds in stages and keeps the results of the stages whose settings have
// not changed, so changing a later setting does not redo the terrain shape or the
// erosion. Only one build may run at a time. Progress goes 0 to 100 into progress.
class WorldGen {
public:
    WorldGen();
    ~WorldGen();
    WorldGen(const WorldGen&) = delete;
    WorldGen& operator=(const WorldGen&) = delete;

    // say(stage, text): stage 1 to 6 starts that stage; stage 0 adds a detail line.
    void Run(const GenSettings& gs, World& out, std::atomic<int>& progress,
             const std::function<void(int, const std::string&)>& say = {});

private:
    struct Cache;
    Cache* c_;
};

// Distance in tiles from every cell of a w x h grid to the nearest cell where
// target is non-zero (0 on the target cells themselves).
std::vector<float> DistanceToMask(const std::vector<uint8_t>& target, int w, int h);

// Height in world units at a world-space position (bilinear, clamped to the edges).
float SampleHeight(const World& w, float x, float z);

// Color used for terrain of this biome.
V3 BiomeColor(Biome b);

}  // namespace wm
