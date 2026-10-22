#pragma once

#include <cstdint>

#include "render/gl_api.hpp"

namespace wm {


// Creates a seamless 256 by 256 tile texture. RGB holds a tangent-space normal
// (x and y are the horizontal slopes, z points up) and alpha holds the height.
// The tile repeats across the world, so surface detail is a grid of the same shapes.
// basePeriod is the number of noise cells across the tile for the largest shapes.
// ridged gives sharp crinkled grain (for ground); false gives smooth rounded bumps
// (for water). Must be called after the GL context exists. Returns 0 on failure.
GLuint CreateSurfaceTile(uint32_t seed, int basePeriod, bool ridged);

}  // namespace wm
