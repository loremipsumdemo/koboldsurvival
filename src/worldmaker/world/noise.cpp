#include "world/noise.hpp"

#include <cmath>

namespace wm {


namespace noise {

namespace {

uint32_t Hash(int x, int y, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x8DA6B343u;
    h ^= static_cast<uint32_t>(y) * 0xD8163841u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

// Random value in [-1, 1) for a lattice point.
float Lattice(int x, int y, uint32_t seed) {
    return static_cast<float>(Hash(x, y, seed) & 0xFFFFFFu) * (2.0f / 16777216.0f) - 1.0f;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Quintic fade: smooth first and second derivatives, so no visible grid seams.
float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float Value(float x, float y, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = Fade(x - fx);
    const float ty = Fade(y - fy);

    const float a = Lattice(ix,     iy,     seed);
    const float b = Lattice(ix + 1, iy,     seed);
    const float c = Lattice(ix,     iy + 1, seed);
    const float d = Lattice(ix + 1, iy + 1, seed);
    return Lerp(Lerp(a, b, tx), Lerp(c, d, tx), ty);
}

}  // namespace

float Fbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float freq = 1.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * Value(x * freq, y * freq, seed + static_cast<uint32_t>(o) * 101u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float Ridged(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float freq = 1.0f;
    for (int o = 0; o < octaves; ++o) {
        float v = 1.0f - std::fabs(Value(x * freq, y * freq, seed + static_cast<uint32_t>(o) * 101u));
        v *= v;
        sum += amp * v;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

namespace {

// Eight evenly spaced unit directions. Each lattice point picks one by hash.
constexpr float kGradX[8] = {1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f};
constexpr float kGradY[8] = {0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f};

// Dot product of the lattice point's direction with the offset from that point.
float Grad(int x, int y, float dx, float dy, uint32_t seed) {
    const uint32_t g = Hash(x, y, seed + 7919u) & 7u;
    return kGradX[g] * dx + kGradY[g] * dy;
}

}  // namespace

float Perlin(float x, float y, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = x - fx;
    const float ty = y - fy;

    const float n00 = Grad(ix,     iy,     tx,        ty,        seed);
    const float n10 = Grad(ix + 1, iy,     tx - 1.0f, ty,        seed);
    const float n01 = Grad(ix,     iy + 1, tx,        ty - 1.0f, seed);
    const float n11 = Grad(ix + 1, iy + 1, tx - 1.0f, ty - 1.0f, seed);

    const float u = Fade(tx);
    const float v = Fade(ty);
    // The raw value is within about +-0.71, so scale it to roughly [-1, 1].
    return Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v) * 1.4142f;
}

float PerlinFbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float freq = 1.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * Perlin(x * freq, y * freq, seed + static_cast<uint32_t>(o) * 101u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace noise

}  // namespace wm
