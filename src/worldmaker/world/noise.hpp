#pragma once

#include <cstdint>

namespace wm {


namespace noise {

// Smooth noise summed over octaves. Output is about [-1, 1]. The same inputs
// and seed always give the same value.
float Fbm(float x, float y, uint32_t seed, int octaves);

// Ridged noise: sharp crests where the base noise crosses zero. Output is [0, 1].
float Ridged(float x, float y, uint32_t seed, int octaves);

// Perlin (gradient) noise: smooth, with no axis-aligned blocks. Output is about
// [-1, 1]. PerlinFbm sums octaves of it, like Fbm.
float Perlin(float x, float y, uint32_t seed);
float PerlinFbm(float x, float y, uint32_t seed, int octaves);

}  // namespace noise

}  // namespace wm
