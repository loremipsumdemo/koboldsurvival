#pragma once

// The two surface tiles the ground and the sea are drawn with. They are made on the CPU, with no
// OpenGL in them, so the world folder can store the pixels and the game can read them back.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wm {

constexpr int SURFACE_TILE_SIZE = 256;   // texels per side
constexpr std::size_t SURFACE_TILE_BYTES = static_cast<std::size_t>(SURFACE_TILE_SIZE) * SURFACE_TILE_SIZE * 4;

// How a tile is made. The same spec always gives the same pixels.
struct SurfaceTileSpec {
    uint32_t seed;
    int basePeriod;   // noise cells across the tile for the largest shapes
    bool ridged;      // sharp crinkled grain (ground) or smooth rounded bumps (water)
};

constexpr SurfaceTileSpec kGroundTileSpec{ 1234u, 12, true };
constexpr SurfaceTileSpec kWaterTileSpec{ 777u, 6, false };

// RGBA8 pixels of a seamless tile, SURFACE_TILE_SIZE squared, rows from the top. RGB holds a tangent-space
// normal (x and y are the horizontal slopes, z points up) and alpha holds the height.
std::vector<uint8_t> SurfaceTilePixels(const SurfaceTileSpec& spec);

}  // namespace wm
