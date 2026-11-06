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
