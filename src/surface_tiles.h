#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr int SURFACE_TILE_SIZE = 256;
constexpr std::size_t SURFACE_TILE_BYTES = (std::size_t)SURFACE_TILE_SIZE * SURFACE_TILE_SIZE * 4;

struct SurfaceTileSpec {
    uint32_t seed;
    int basePeriod;
    bool ridged;
};

constexpr SurfaceTileSpec GROUND_TILE_SPEC{ 1234u, 12, true };
constexpr SurfaceTileSpec WATER_TILE_SPEC{ 777u, 6, false };

std::vector<uint8_t> surfaceTilePixels(const SurfaceTileSpec& spec);
// The same kind of tile, different for each world seed. Seed 0 gives the spec as it is.
SurfaceTileSpec seededSpec(SurfaceTileSpec spec, uint32_t worldSeed);

// A seamless picture of soft noise, SKY_NOISE_SIZE square, made when a map loads and never saved: red and green are the two
// layers of cloud, blue and alpha the slow changes across the ground and water (brightness and bumpiness)
constexpr int SKY_NOISE_SIZE = 256;
std::vector<uint8_t> skyNoisePixels(uint32_t worldSeed);
