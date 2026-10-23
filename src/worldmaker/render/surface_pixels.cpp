#include "render/surface_pixels.hpp"

#include <algorithm>
#include <cmath>

namespace wm {

namespace {

constexpr int   kSize = SURFACE_TILE_SIZE;
constexpr float kSlopeScale = 24.0f;    // how strongly height differences tilt the normal

uint32_t HashCell(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x27d4eb2du + y * 0x165667b1u + seed * 0x9e3779b9u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

// Random value in [0, 1) for one lattice cell.
float Lattice(int x, int y, uint32_t seed) {
    const uint32_t h = HashCell(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed);
    return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

// Value noise at (u, v) in [0, 1). The lattice wraps every `period` cells, so the
// noise is exactly periodic over the tile and the tile repeats without a seam.
float PeriodicNoise(float u, float v, int period, uint32_t seed) {
    const float fx = u * static_cast<float>(period);
    const float fy = v * static_cast<float>(period);
    const float ix = std::floor(fx);
    const float iy = std::floor(fy);
    const float tx = fx - ix;
    const float ty = fy - iy;
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);

    const int x0 = static_cast<int>(ix) % period;
    const int y0 = static_cast<int>(iy) % period;
    const int x1 = (x0 + 1) % period;
    const int y1 = (y0 + 1) % period;

    const float a = Lattice(x0, y0, seed);
    const float b = Lattice(x1, y0, seed);
    const float c = Lattice(x0, y1, seed);
    const float d = Lattice(x1, y1, seed);
    const float top = a + (b - a) * sx;
    const float bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

unsigned char Encode(float v) {
    const long q = std::lround((v * 0.5f + 0.5f) * 255.0f);
    return static_cast<unsigned char>(std::clamp(q, 0L, 255L));
}

}  // namespace

std::vector<uint8_t> SurfaceTilePixels(const SurfaceTileSpec& spec) {
    const std::size_t count = static_cast<std::size_t>(kSize) * kSize;

    // Three octaves: big shapes, medium shapes, small shapes. Ridged octaves are
    // folded so their peaks are sharp, which reads as grain rather than bumps.
    const bool ridged = spec.ridged;
    auto octave = [ridged](float u, float v, int period, uint32_t s) {
        const float n = PeriodicNoise(u, v, period, s);
        return ridged ? 1.0f - std::fabs(2.0f * n - 1.0f) : n;
    };

    std::vector<float> height(count);
    float lo = 1e9f;
    float hi = -1e9f;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = static_cast<float>(x) / kSize;
            const float v = static_cast<float>(y) / kSize;
            const int base = spec.basePeriod;
            float s = 0.5f * octave(u, v, base, spec.seed)
                    + 0.3f * octave(u, v, base * 2, spec.seed + 1u)
                    + 0.2f * octave(u, v, base * 4, spec.seed + 2u);
            if (ridged) s *= s;
            height[static_cast<std::size_t>(y) * kSize + x] = s;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
    }
    const float range = std::max(hi - lo, 1e-5f);

    // Height in [0, 1] with wrap-around, so the normals match across tile edges.
    auto at = [&](int x, int y) {
        const int xx = (x + kSize) % kSize;
        const int yy = (y + kSize) % kSize;
        return (height[static_cast<std::size_t>(yy) * kSize + xx] - lo) / range;
    };

    std::vector<uint8_t> pixels(count * 4);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = (at(x + 1, y) - at(x - 1, y)) * kSlopeScale;
            const float dy = (at(x, y + 1) - at(x, y - 1)) * kSlopeScale;
            const float len = std::sqrt(dx * dx + dy * dy + 1.0f);

            uint8_t* p = &pixels[(static_cast<std::size_t>(y) * kSize + x) * 4];
            p[0] = Encode(-dx / len);
            p[1] = Encode(-dy / len);
            p[2] = Encode(1.0f / len);
            p[3] = static_cast<uint8_t>(std::lround(at(x, y) * 255.0f));
        }
    }
    return pixels;
}

}  // namespace wm
