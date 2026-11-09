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

// Gradient (Perlin) noise that repeats every period cells, so the picture tiles without a seam. About -0.7 to 0.7.
float periodicGradient(float u, float v, int period, uint32_t seed) {
    const float fx = u * static_cast<float>(period);
    const float fy = v * static_cast<float>(period);
    const float ix = std::floor(fx);
    const float iy = std::floor(fy);
    const float tx = fx - ix;
    const float ty = fy - iy;
    auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const int x0 = ((static_cast<int>(ix) % period) + period) % period;
    const int y0 = ((static_cast<int>(iy) % period) + period) % period;
    const int x1 = (x0 + 1) % period;
    const int y1 = (y0 + 1) % period;
    auto corner = [&](int cx, int cy, float dx, float dy) {
        const float a = static_cast<float>(hashCell(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), seed) & 0xFFFFu) *
                        (6.2831853f / 65536.0f);
        return std::cos(a) * dx + std::sin(a) * dy;
    };
    const float a = corner(x0, y0, tx, ty);
    const float b = corner(x1, y0, tx - 1.0f, ty);
    const float c = corner(x0, y1, tx, ty - 1.0f);
    const float d = corner(x1, y1, tx - 1.0f, ty - 1.0f);
    const float sx = fade(tx), sy = fade(ty);
    const float top = a + (b - a) * sx;
    const float bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

uint32_t mixSeed(uint32_t s) {
    s ^= s >> 16;
    s *= 0x7feb352du;
    s ^= s >> 15;
    s *= 0x846ca68bu;
    s ^= s >> 16;
    return s;
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

SurfaceTileSpec seededSpec(SurfaceTileSpec spec, uint32_t worldSeed) {
    if (worldSeed != 0) spec.seed ^= mixSeed(worldSeed);
    return spec;
}

std::vector<uint8_t> skyNoisePixels(uint32_t worldSeed) {
    constexpr int N = SKY_NOISE_SIZE;
    const uint32_t seed = mixSeed(worldSeed ^ 0x5bd1e995u);
    struct Channel {
        int basePeriod;
        int octaves;
        uint32_t salt;
    };
    // Clouds get more octaves for ragged edges; the ground's slow changes need few
    const Channel channels[4] = { { 4, 6, 11u }, { 3, 6, 23u }, { 2, 4, 37u }, { 5, 4, 41u } };
    std::vector<uint8_t> pixels(static_cast<std::size_t>(N) * N * 4);
    std::vector<float> value(static_cast<std::size_t>(N) * N);
    for (int c = 0; c < 4; ++c) {
        const Channel& ch = channels[c];
        float lo = 1e9f, hi = -1e9f;
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const float u = static_cast<float>(x) / N, v = static_cast<float>(y) / N;
                float sum = 0.0f, weight = 1.0f;
                int period = ch.basePeriod;
                for (int o = 0; o < ch.octaves; ++o) {
                    sum += weight * periodicGradient(u, v, period, seed + ch.salt + static_cast<uint32_t>(o) * 101u);
                    weight *= 0.5f;
                    period *= 2;
                }
                value[static_cast<std::size_t>(y) * N + x] = sum;
                lo = std::min(lo, sum);
                hi = std::max(hi, sum);
            }
        }
        const float range = std::max(hi - lo, 1e-5f);
        for (std::size_t i = 0; i < value.size(); ++i) {
            pixels[i * 4 + static_cast<std::size_t>(c)] = static_cast<uint8_t>(std::lround((value[i] - lo) / range * 255.0f));
        }
    }
    return pixels;
}

