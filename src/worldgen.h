#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "map.h"

// Map sizes offered in the editor, in tiles per side
constexpr int MAP_SIZES[] = { 256, 512, 1024, 2048, 4096, 8192 };
constexpr int MAP_SIZE_COUNT = 6;
constexpr int DEFAULT_SIZE_INDEX = 2;      // 1024
// The largest map that can be generated and drawn right now. Bigger sizes are listed but can't be
// built until the chunked renderer exists.
constexpr int MAX_BUILD_SIZE = 2048;

// The groups of settings on the generator screen, one tab each
enum class GenTab { Land, Erosion, Climate, Ground, Plants };
constexpr int GEN_TAB_COUNT = 5;
const char* genTabName(int tab);

// Everything the generator reads. The defaults give a typical world.
struct WorldSettings {
    int seed = 1;
    int sizeIndex = DEFAULT_SIZE_INDEX;

    // Land: continents, mountains and plains
    float waterShare = 0.42f;   // share of the map under the sea, 0.05 to 0.95
    float relief = 1.0f;        // mountain height, 0.25 to 2
    float rangeCover = 0.5f;    // how much of the land carries mountain ranges, 0 to 1
    float plainShare = 0.0f;    // share of the land pulled down to plains, 0 to 1
    float plainLevel = 0.06f;   // plains height above the sea, 0 to 0.5

    // Erosion: water wears the land down, and slopes slump
    float erosion = 0.6f;       // strength of water erosion, 0 to 1
    int passes = 24;            // water erosion passes, 8 to 60
    float thermal = 0.3f;       // slope slumping, 0 to 1

    // Climate: warmth and wetness, added to what the geography gives
    bool rankedClimate = true;  // true: the climates spread evenly over the land; false: real values
    float warmth = 0.0f;        // -0.3 to 0.3
    float wetness = 0.0f;       // -0.3 to 0.3

    // Ground and coast
    float lowlands = 0.3f;      // pushes land toward the low ground, 0 to 1
    bool rugged = false;        // rock detail: false soft, true rugged
    float detail = 0.7f;       // fine rock detail, 0 to 1
    float steep = 0.3f;         // how much the detail follows steep slopes, 0 to 1
    float beach = 5.0f;         // beach width in tiles, 0 to 20
    float shelf = 10.0f;        // shallow shelf width in tiles, 0 to 30

    // Plants
    float cover = 0.7f;         // how much of the grass is forest, 0 to 1

    int size() const { return MAP_SIZES[sizeIndex]; }
};

// One row on the generator screen. Exactly one of f, i and b is set, by kind.
enum class GenKind { Percent, Number, Integer, Toggle };
struct GenParam {
    std::string label;
    GenTab tab;
    GenKind kind;
    float* f = nullptr;
    int* i = nullptr;
    bool* b = nullptr;
    float lo = 0.0f, hi = 1.0f, step = 0.05f;
    int decimals = 2;
};

// Every setting row, with pointers into s. The screen lists the rows of one tab at a time.
std::vector<GenParam> genParams(WorldSettings& s);

// Builds worlds in stages. Each stage's result is kept, so a build after a change only redoes
// the stages from the first changed one onward. Only one build runs at a time.
class WorldGenerator {
public:
    static constexpr int kStages = 5;

    WorldGenerator();
    ~WorldGenerator();
    WorldGenerator(const WorldGenerator&) = delete;
    WorldGenerator& operator=(const WorldGenerator&) = delete;

    // progress is called with 0 to 1 and the name of the stage that is running
    MapData build(const WorldSettings& s, const std::function<void(float, const char*)>& progress);

    // Forgets every stage, so the next build runs them all
    void reset();

    // The first stage the last build ran, 0 to 4. kStages if the last build reused everything.
    int firstRebuiltStage() const;

private:
    struct Cache;
    std::unique_ptr<Cache> c_;
};

// Builds a world without keeping anything for the next build
MapData generateWorld(const WorldSettings& s, const std::function<void(float, const char*)>& progress);
