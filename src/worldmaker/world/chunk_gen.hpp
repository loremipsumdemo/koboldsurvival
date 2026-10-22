#pragma once

// Seed-based chunk generation. Every tile is a pure function of the seed, the settings
// and its position, so any chunk can be made on its own, in any order, at any detail.
// Only a few global numbers (sea level and a peak threshold) are sampled once up front.

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "world/world.hpp"

namespace wm {


namespace chunkgen {

constexpr int kChunk = 64;  // tiles per chunk side

struct Settings {
    uint32_t seed = 1;
    int size = 65536;           // tiles per side
    float heightScale = 500.0f; // world units for a normalised height of 1
    float waterShare = 0.42f;
    float relief = 1.0f;
    float rangeCover = 0.5f;
    float cover = 0.7f;
};

// Reports a multi-stage job: which stage is running, the overall fraction, the time
// spent so far, and an estimate of the time left from the pace so far.
class Tracker {
public:
    using Fn = std::function<void(const std::string& stage, int index, int count, double overall,
                                  double elapsed, double eta)>;
    Tracker(Fn fn, int stageCount);
    void Stage(int index, const std::string& name);
    void Fraction(double f);  // 0 to 1 within the current stage
    void Label(const std::string& name);  // changes the name, keeps the progress

private:
    void Emit();
    Fn fn_;
    int count_;
    int index_ = 0;
    std::string name_;
    double frac_ = 0.0;
    std::chrono::steady_clock::time_point start_;
};

// The few values that depend on the whole map, sampled once from the seed.
struct Globals {
    Settings s;
    float sea = 0.0f;      // normalised sea level
    float shoreTop = 0.0f; // land above this can carry peaks
    float peakThr = 0.0f;  // peak field threshold for the land
    bool bands = false;    // biome bands on, see Prepare
    float bandLo = 0.0f;   // west-east key at the 25% point of the land
    float bandHi = 0.0f;   // west-east key at the 75% point of the land
};

Globals Prepare(const Settings& s, Tracker& tr);

// A chunk at a given detail. step is the tile spacing of its samples: 1 for full detail,
// 4 for distant chunks. Each chunk has n x n samples, n = kChunk / step.
struct Chunk {
    int cx = 0, cz = 0, step = 1, n = kChunk;
    std::vector<float> height;   // normalised, 0 to 1
    std::vector<uint8_t> biome;  // Biome values
    std::vector<float> veg;      // 0 to 1
};

void GenerateChunk(const Globals& g, int cx, int cz, int step, Chunk& out);

// Compact storage: heights as 16-bit, biomes packed at 5 bits, vegetation as 8-bit.
std::vector<uint8_t> Encode(const Chunk& c);
bool Decode(const std::vector<uint8_t>& bytes, Chunk& out);

}  // namespace chunkgen

}  // namespace wm
