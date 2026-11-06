#include "surface_tiles.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int   TILE = SURFACE_TILE_SIZE;
constexpr float SLOPE_SCALE = 24.0f;

uint32_t hashCell(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x27d4eb2du + y * 0x165667b1u + seed * 0x9e3779b9u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

float lattice(int x, int y, uint32_t seed) {
    const uint32_t h = hashCell(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed);
    return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

float periodicNoise(float u, float v, int period, uint32_t seed) {
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

    const float a = lattice(x0, y0, seed);
    const float b = lattice(x1, y0, seed);
    const float c = lattice(x0, y1, seed);
    const float d = lattice(x1, y1, seed);
    const float top = a + (b - a) * sx;
    const float bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

unsigned char encode(float v) {
    const long q = std::lround((v * 0.5f + 0.5f) * 255.0f);
    return static_cast<unsigned char>(std::clamp(q, 0L, 255L));
}
}

std::vector<uint8_t> surfaceTilePixels(const SurfaceTileSpec& spec) {
    const std::size_t count = static_cast<std::size_t>(TILE) * TILE;

    const bool ridged = spec.ridged;
    auto octave = [ridged](float u, float v, int period, uint32_t s) {
        const float n = periodicNoise(u, v, period, s);
        return ridged ? 1.0f - std::fabs(2.0f * n - 1.0f) : n;
    };

    std::vector<float> height(count);
    float lo = 1e9f;
    float hi = -1e9f;
    for (int y = 0; y < TILE; ++y) {
        for (int x = 0; x < TILE; ++x) {
            const float u = static_cast<float>(x) / TILE;
            const float v = static_cast<float>(y) / TILE;
            const int base = spec.basePeriod;
            float s = 0.5f * octave(u, v, base, spec.seed)
                    + 0.3f * octave(u, v, base * 2, spec.seed + 1u)
                    + 0.2f * octave(u, v, base * 4, spec.seed + 2u);
            if (ridged) s *= s;
            height[static_cast<std::size_t>(y) * TILE + x] = s;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
    }
    const float range = std::max(hi - lo, 1e-5f);

    auto at = [&](int x, int y) {
        const int xx = (x + TILE) % TILE;
        const int yy = (y + TILE) % TILE;
        return (height[static_cast<std::size_t>(yy) * TILE + xx] - lo) / range;
    };

    std::vector<uint8_t> pixels(count * 4);
    for (int y = 0; y < TILE; ++y) {
        for (int x = 0; x < TILE; ++x) {
            const float dx = (at(x + 1, y) - at(x - 1, y)) * SLOPE_SCALE;
            const float dy = (at(x, y + 1) - at(x, y - 1)) * SLOPE_SCALE;
            const float len = std::sqrt(dx * dx + dy * dy + 1.0f);

            uint8_t* p = &pixels[(static_cast<std::size_t>(y) * TILE + x) * 4];
            p[0] = encode(-dx / len);
            p[1] = encode(-dy / len);
            p[2] = encode(1.0f / len);
            p[3] = static_cast<uint8_t>(std::lround(at(x, y) * 255.0f));
        }
    }
    return pixels;
}

