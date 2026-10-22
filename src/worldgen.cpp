#include "worldgen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numeric>
#include <vector>

#include "grid.h"

// The generator builds a world in five stages. Each reads what the stages before it made:
//   0 land: continents, a sea rim, mountain ranges, extra peaks, plains
//   1 erosion: weather on a coarse grid, water erosion and slope slumping
//   2 climate: temperature and moisture, ranked over the land or real
//   3 ground: coast, beach and shelf, fine rock detail, slopes, ground types
//   4 plants: forest patches
// The steps follow the ideas of the worldmaker tool. The code is written for this game.

namespace {

using Field = std::vector<float>;

// Heights are built as 0 to 1 at the sea level, then scaled to world units
constexpr float kSlopeFullWorld = 0.35f;   // world units per tile that count as a full slope
constexpr float kMaxStepWorld = 0.6f;      // no tile may step more than this from its neighbour
constexpr float kRimStart = 0.01f;         // distance from the edge (0 to 0.5) where the rim begins
constexpr float kRimBlend = 0.12f;         // width of the blend back to normal ground

// ---------- noise ----------

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t hashCell(int x, int y, uint32_t seed) {
    return hash32(hash32(seed ^ ((uint32_t)x * 0x27d4eb2du)) + (uint32_t)y * 0x165667b1u);
}

float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float smoothStep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Soft band: 1 inside [lo, hi], fading to 0 over edge on each side
float softRange(float x, float lo, float hi, float edge = 0.12f) {
    const float up = std::clamp((x - (lo - edge)) / edge, 0.0f, 1.0f);
    const float down = std::clamp(((hi + edge) - x) / edge, 0.0f, 1.0f);
    return std::min(up, down);
}

// Gradient noise, about [-1, 1]
float gradientNoise(float x, float y, uint32_t seed) {
    static const float kDirs[8][2] = {
        { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
        { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f }
    };
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - (float)x0, fy = y - (float)y0;
    auto dot = [&](int ix, int iy, float dx, float dy) {
        const float* d = kDirs[hashCell(ix, iy, seed) & 7u];
        return d[0] * dx + d[1] * dy;
    };
    const float a = dot(x0, y0, fx, fy);
    const float b = dot(x0 + 1, y0, fx - 1.0f, fy);
    const float c = dot(x0, y0 + 1, fx, fy - 1.0f);
    const float d = dot(x0 + 1, y0 + 1, fx - 1.0f, fy - 1.0f);
    const float u = fade(fx), v = fade(fy);
    const float top = a + (b - a) * u, bottom = c + (d - c) * u;
    return (top + (bottom - top) * v) * 1.4142f;
}

// Octaves of gradient noise, each finer and quieter. About [-1, 1].
float fbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * gradientNoise(x, y, seed + (uint32_t)i * 131u);
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// Sharp crests where the noise crosses zero. [0, 1].
float ridged(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const float n = 1.0f - std::fabs(gradientNoise(x, y, seed + (uint32_t)i * 131u));
        sum += amp * n * n;
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// ---------- grids ----------

size_t at(int x, int y, int w) { return (size_t)y * (size_t)w + (size_t)x; }

// Bilinear sample of a g x g grid at grid coordinates
float sampleGrid(const Field& c, int g, float fx, float fy) {
    fx = std::clamp(fx, 0.0f, (float)(g - 1));
    fy = std::clamp(fy, 0.0f, (float)(g - 1));
    const int x0 = (int)fx, y0 = (int)fy;
    const int x1 = std::min(x0 + 1, g - 1), y1 = std::min(y0 + 1, g - 1);
    const float tx = fx - (float)x0, ty = fy - (float)y0;
    const float a = c[at(x0, y0, g)] + (c[at(x1, y0, g)] - c[at(x0, y0, g)]) * tx;
    const float b = c[at(x0, y1, g)] + (c[at(x1, y1, g)] - c[at(x0, y1, g)]) * tx;
    return a + (b - a) * ty;
}

// Grid coordinate on a g x g grid of a position p, given in tiles from the map's edge
float toGrid(float p, int n, int g) { return p * (float)(g - 1) / (float)n; }

// Averages a W x W field (W = n + 1 corners) onto a g x g grid
Field downsample(const Field& src, int W, int g) {
    const int n = W - 1;
    std::vector<double> sum((size_t)g * g, 0.0);
    std::vector<int> cnt((size_t)g * g, 0);
    for (int y = 0; y < W; ++y) {
        const int Y = (int)std::lround((double)y * (g - 1) / n);
        for (int x = 0; x < W; ++x) {
            const int X = (int)std::lround((double)x * (g - 1) / n);
            sum[at(X, Y, g)] += src[at(x, y, W)];
            ++cnt[at(X, Y, g)];
        }
    }
    Field out(sum.size(), 0.0f);
    for (size_t i = 0; i < sum.size(); ++i) {
        if (cnt[i] > 0) out[i] = (float)(sum[i] / cnt[i]);
    }
    return out;
}

// Samples a g x g grid over a W x W field
Field upsample(const Field& c, int g, int W) {
    const int n = W - 1;
    Field out((size_t)W * W);
    for (int y = 0; y < W; ++y) {
        const float fy = toGrid((float)y, n, g);
        for (int x = 0; x < W; ++x) {
            out[at(x, y, W)] = sampleGrid(c, g, toGrid((float)x, n, g), fy);
        }
    }
    return out;
}

// The value at which `share` of the masked cells are at or above it
float topShare(const Field& f, const std::vector<uint8_t>& mask, float share) {
    std::vector<float> vals;
    vals.reserve(f.size() / 2);
    for (size_t i = 0; i < f.size(); ++i) {
        if (mask[i]) vals.push_back(f[i]);
    }
    if (vals.empty()) return 0.0f;
    const size_t count = vals.size();
    const size_t above = (size_t)(std::clamp(share, 0.0f, 1.0f) * (float)count);
    const size_t idx = above >= count ? 0 : count - 1 - above;
    std::nth_element(vals.begin(), vals.begin() + (long)idx, vals.end());
    return vals[idx];
}

// Each value's rank among the land cells, 0 to 1. The land sets the scale.
Field rankLand(const Field& X, const std::vector<uint8_t>& land) {
    std::vector<float> vals;
    for (size_t i = 0; i < X.size(); ++i) {
        if (land[i]) vals.push_back(X[i]);
    }
    if (vals.empty()) vals = X;
    std::sort(vals.begin(), vals.end());
    Field out(X.size());
    const float inv = 1.0f / (float)vals.size();
    for (size_t i = 0; i < X.size(); ++i) {
        const auto pos = std::lower_bound(vals.begin(), vals.end(), X[i]) - vals.begin();
        out[i] = std::clamp((float)pos * inv, 0.0f, 1.0f);
    }
    return out;
}

// Four-neighbour blur with the edges held, `passes` times
Field diffuse(Field a, int g, int passes) {
    for (int p = 0; p < passes; ++p) {
        Field b(a.size());
        for (int y = 0; y < g; ++y) {
            const int up = std::max(y - 1, 0), down = std::min(y + 1, g - 1);
            for (int x = 0; x < g; ++x) {
                const int left = std::max(x - 1, 0), right = std::min(x + 1, g - 1);
                b[at(x, y, g)] = (a[at(x, up, g)] + a[at(x, down, g)] + a[at(left, y, g)] +
                                  a[at(right, y, g)] + 4.0f * a[at(x, y, g)]) / 8.0f;
            }
        }
        a.swap(b);
    }
    return a;
}

// Distance in tiles from every cell of a w x w grid to the nearest cell where target is set
Field distanceTo(const std::vector<uint8_t>& target, int w) {
    const float far = 1e9f, diag = 1.41421356f;
    Field d(target.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = target[i] ? 0.0f : far;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t c = at(x, y, w);
            float v = d[c];
            if (x > 0) v = std::min(v, d[c - 1] + 1.0f);
            if (y > 0) {
                v = std::min(v, d[c - w] + 1.0f);
                if (x > 0) v = std::min(v, d[c - w - 1] + diag);
                if (x < w - 1) v = std::min(v, d[c - w + 1] + diag);
            }
            d[c] = v;
        }
    }
    for (int y = w - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            const size_t c = at(x, y, w);
            float v = d[c];
            if (x < w - 1) v = std::min(v, d[c + 1] + 1.0f);
            if (y < w - 1) {
                v = std::min(v, d[c + w] + 1.0f);
                if (x < w - 1) v = std::min(v, d[c + w + 1] + diag);
                if (x > 0) v = std::min(v, d[c + w - 1] + diag);
            }
            d[c] = v;
        }
    }
    for (float& v : d) v = std::min(v, 2.0f * (float)w);
    return d;
}

// World units per normalised height. The highest land or deepest sea reaches HEIGHT_SCALE.
float worldPerNorm(float sea) {
    return HEIGHT_SCALE / std::max(std::max(1.0f - sea, sea), 1e-3f);
}

// Slope at a corner, 0 to 1, from central differences of a W x W height field in 0 to 1 units
float slopeAt(const Field& h, int W, int x, int z, float scale) {
    const int xl = std::max(x - 1, 0), xr = std::min(x + 1, W - 1);
    const int zu = std::max(z - 1, 0), zd = std::min(z + 1, W - 1);
    const float gx = (h[at(xr, z, W)] - h[at(xl, z, W)]) / (float)std::max(xr - xl, 1);
    const float gz = (h[at(x, zd, W)] - h[at(x, zu, W)]) / (float)std::max(zd - zu, 1);
    return std::clamp(std::hypot(gx, gz) * scale / kSlopeFullWorld, 0.0f, 1.0f);
}

// Limits how far each corner can sit from its neighbours. The excess is shared between the pair.
void softenSlopes(Field& h, int W, float maxStep, int passes) {
    auto relax = [&h, maxStep](size_t a, size_t b) {
        const float d = h[a] - h[b];
        const float excess = std::fabs(d) - maxStep;
        if (excess <= 0.0f) return;
        const float move = 0.5f * excess * (d > 0.0f ? 1.0f : -1.0f);
        h[a] -= move;
        h[b] += move;
    };
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < W; ++y) {
            for (int x = 0; x < W; ++x) {
                if (x + 1 < W) relax(at(x, y, W), at(x + 1, y, W));
                if (y + 1 < W) relax(at(x, y, W), at(x, y + 1, W));
            }
        }
    }
}

// ---------- stage 0: land ----------

// Continents, rim, mountains, peaks and plains on the W x W corner grid. Heights are 0 to 1.
void shapeLand(const WorldSettings& s, int W, Field& h, float& seaOut) {
    const int n = W - 1;
    const float inv = 1.0f / (float)n;
    const uint32_t seed = (uint32_t)s.seed * 2654435761u + 1u;
    h.assign((size_t)W * W, 0.0f);

    // Continents: fbm warped by fbm, so the coastlines wander
    for (int z = 0; z < W; ++z) {
        const float v = z * inv;
        for (int x = 0; x < W; ++x) {
            const float u = x * inv;
            const float wx = u * 3.0f + 0.9f * fbm(u * 2.0f + 5.2f, v * 2.0f + 1.3f, seed + 300u, 3);
            const float wy = v * 3.0f + 0.9f * fbm(u * 2.0f - 3.7f, v * 2.0f + 8.1f, seed + 301u, 3);
            h[at(x, z, W)] = fbm(wx, wy, seed, 6) * 0.5f + 0.5f;
        }
    }

    // Sea level: the height with the chosen share of the map below it
    Field sorted = h;
    const float water = std::clamp(s.waterShare, 0.05f, 0.95f);
    const size_t seaIdx = std::min(sorted.size() - 1, (size_t)(water * (float)sorted.size()));
    std::nth_element(sorted.begin(), sorted.begin() + (long)seaIdx, sorted.end());
    float sea = sorted[seaIdx];

    // The rim of the map is always sea, so no land touches the edge
    const float rimFloor = sea - 0.15f;
    for (int z = 0; z < W; ++z) {
        const float v = z * inv;
        for (int x = 0; x < W; ++x) {
            const float u = x * inv;
            const float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
            const float keep = smoothStep(kRimStart, kRimStart + kRimBlend, edge);
            float& hv = h[at(x, z, W)];
            hv = rimFloor + (hv - rimFloor) * keep;
        }
    }
    // The rim lowered the edges, so the sea level is taken again from the field as it now stands
    {
        sorted = h;
        const size_t idx = std::min(sorted.size() - 1, (size_t)(water * (float)sorted.size()));
        std::nth_element(sorted.begin(), sorted.begin() + (long)idx, sorted.end());
        sea = sorted[idx];
    }
    seaOut = sea;

    // Mountains: ridged belts on the land. Range cover moves where the belts sit.
    const float rc = std::clamp(s.rangeCover, 0.0f, 1.0f);
    const float regionLo = 0.35f + (0.5f - rc) * 0.6f;
    const float regionHi = regionLo + 0.35f;
    const float relief = std::clamp(s.relief, 0.0f, 2.0f);
    for (int z = 0; z < W; ++z) {
        const float v = z * inv;
        for (int x = 0; x < W; ++x) {
            const float u = x * inv;
            float& hv = h[at(x, z, W)];
            const float base = hv;
            const float land = smoothStep(sea, sea + 0.08f, base);
            const float region = smoothStep(regionLo, regionHi,
                                            fbm(u * 2.0f, v * 2.0f, seed + 500u, 3) * 0.5f + 0.5f);
            // The ranges wander, so they curve rather than repeat
            const float wxr = 0.35f * fbm(u * 3.0f, v * 3.0f, seed + 1100u, 3);
            const float wyr = 0.35f * fbm(u * 3.0f + 4.4f, v * 3.0f - 2.1f, seed + 1101u, 3);
            const float r = ridged(u * 5.0f + wxr, v * 5.0f + wyr, seed + 1000u, 5);
            const float hills = fbm(u * 14.0f, v * 14.0f, seed + 2000u, 4) * 0.08f;
            hv = std::clamp(base + land * (r * r * 0.45f * region * relief + hills), 0.0f, 1.0f);
        }
    }

    // Extra peaks: a noise field picks out 10 to 30% of the land, set by the seed
    const float shoreTop = sea + 0.06f;
    {
        const float share = 0.10f + 0.20f * (float)(hash32(seed + 9001u) & 0xFFFFu) / 65535.0f;
        Field field(h.size());
        std::vector<uint8_t> landMask(h.size());
        for (int z = 0; z < W; ++z) {
            for (int x = 0; x < W; ++x) {
                const size_t c = at(x, z, W);
                field[c] = fbm(x * inv * 6.0f, z * inv * 6.0f, seed + 4000u, 4);
                landMask[c] = h[c] > shoreTop;
            }
        }
        const float threshold = topShare(field, landMask, share);
        for (int z = 0; z < W; ++z) {
            for (int x = 0; x < W; ++x) {
                const size_t c = at(x, z, W);
                if (h[c] <= shoreTop) continue;
                const float m = smoothStep(threshold - 0.12f, threshold + 0.12f, field[c]);
                if (m <= 0.0f) continue;
                const float ridge = ridged(x * inv * 12.0f, z * inv * 12.0f, seed + 5000u, 4);
                h[c] = std::clamp(h[c] + m * (0.02f + 0.05f * ridge), 0.0f, 1.0f);
            }
        }
    }

    // Plains: a share of the land is pulled down toward a plains height. The high ground keeps its peaks.
    const float plainShare = std::clamp(s.plainShare, 0.0f, 1.0f);
    if (plainShare > 0.0f) {
        Field field(h.size());
        std::vector<uint8_t> landMask(h.size());
        for (int z = 0; z < W; ++z) {
            for (int x = 0; x < W; ++x) {
                const size_t c = at(x, z, W);
                field[c] = fbm(x * inv * 1.5f, z * inv * 1.5f, seed + 2600u, 4) * 0.5f + 0.5f;
                landMask[c] = h[c] >= sea;
            }
        }
        const float thr = topShare(field, landMask, plainShare);
        const float level = sea + std::clamp(s.plainLevel, 0.0f, 0.5f);
        for (size_t c = 0; c < h.size(); ++c) {
            if (h[c] < sea) continue;
            const float pull = smoothStep(thr - 0.03f, thr + 0.03f, field[c]) *
                               (1.0f - smoothStep(0.25f, 0.45f, h[c] - sea));
            h[c] = std::max(h[c] + pull * (level - h[c]), sea + 1e-4f);
        }
    }
}

// ---------- stage 1: erosion and weather ----------

// The coarse grid that weather and erosion run on, with what the later stages need from it
struct CoarseLand {
    int g = 0;
    std::vector<uint8_t> land;   // 1 on land
    Field elev;                  // ranked height over the land, 0 to 1
    Field T0, M0;                // temperature and moisture from geography
    Field acc;                   // water collected at each cell, for floodplains
};

// Rain and carried moisture from air blowing across the map. Westerly bands blow one way and the
// rest blow the other. Rain falls where the land rises, and the air dries out travelling inland.
void windTransport(const Field& E, const std::vector<uint8_t>& land, const std::vector<float>& lat,
                   const Field& warmth, int g, Field& rain, Field& carried) {
    rain.assign(E.size(), 0.0f);
    carried.assign(E.size(), 0.0f);
    for (int y = 0; y < g; ++y) {
        const float l = lat[(size_t)y];
        const bool westerly = l >= 0.33f && l < 0.66f;
        const int dir = westerly ? 1 : -1;
        const float decay = 0.012f + 0.07f * std::exp(-std::pow((l - 0.33f) / 0.09f, 2.0f));
        float m = 0.0f, prev = 0.0f;
        bool first = true;
        for (int k = 0; k < g; ++k) {
            const int x = dir > 0 ? k : g - 1 - k;
            const size_t c = at(x, y, g);
            const float e = E[c];
            const float rise = first ? 0.0f : std::max(e - prev, 0.0f);
            if (!land[c]) m = warmth[c];   // over the sea the air takes up warmth as moisture
            const float rainHere = m * (1.0f - std::exp(-(4.0f * rise + 0.015f)));
            m -= rainHere;
            rain[c] = rainHere;
            carried[c] = m;
            m *= (1.0f - decay);
            prev = e;
            first = false;
        }
    }
}

// The downhill neighbour of every cell, steepest first. Cells at the edge drain off the map (-1).
std::vector<int> receivers(const Field& H, int g) {
    static const int kOff[8][2] = { { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 },
                                    { 0, 1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } };
    std::vector<int> rec(H.size());
    for (int y = 0; y < g; ++y) {
        for (int x = 0; x < g; ++x) {
            const size_t c = at(x, y, g);
            const float h = H[c];
            float best = 0.0f;
            int r = (int)c;
            for (const auto& o : kOff) {
                const int nx = x + o[1], ny = y + o[0];
                const bool off = nx < 0 || ny < 0 || nx >= g || ny >= g;
                const float nh = off ? -1.0f : H[at(nx, ny, g)];
                const float drop = (h - nh) / ((o[0] && o[1]) ? 1.41421356f : 1.0f);
                if (drop > best) {
                    best = drop;
                    r = off ? -1 : (int)at(nx, ny, g);
                }
            }
            rec[c] = r;
        }
    }
    return rec;
}

// Water collected by each cell: its own runoff plus everything that drains into it
Field accumulate(const std::vector<int>& rec, const Field& H, const Field& runoff) {
    std::vector<uint32_t> order(H.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&H](uint32_t a, uint32_t b) { return H[a] > H[b]; });
    Field A = runoff;
    for (uint32_t c : order) {
        const int t = rec[c];
        if (t >= 0 && t != (int)c) A[(size_t)t] += A[c];
    }
    return A;
}

// Water erosion: channels deepen where much water runs, silt fills the pits, and the ground
// smooths a little. Land is never cut below the sea.
void erodeWater(Field& H, const Field& runoff, float sea, float strength, int passes, int g,
                const std::function<void(float)>& report) {
    const float K = 0.0013f * strength;
    constexpr float kDiffuse = 0.12f, kSilt = 0.0015f;
    const size_t cells = H.size();
    for (int pass = 0; pass < passes; ++pass) {
        if (report) report((float)pass / (float)passes);
        std::vector<uint8_t> land(cells);
        for (size_t c = 0; c < cells; ++c) land[c] = H[c] >= sea;
        const std::vector<int> rec = receivers(H, g);
        const Field A = accumulate(rec, H, runoff);

        Field Hn(cells);
        for (size_t c = 0; c < cells; ++c) {
            const float hc = H[c];
            if (!land[c]) {
                Hn[c] = hc;
                continue;
            }
            const int r = rec[c];
            const float below = r >= 0 ? H[(size_t)r] : sea;
            const float slope = std::max(hc - below, 0.0f);
            const float W = 0.5f + 0.5f * smoothStep(sea, sea + 0.2f, hc);
            float v = std::max(hc - K * std::sqrt(A[c]) * slope * W, sea + 1e-4f);
            if (r == (int)c && A[c] > 0.0f) v += std::min(0.02f, kSilt * std::sqrt(A[c]));
            Hn[c] = v;
        }
        for (int y = 0; y < g; ++y) {
            const int up = std::max(y - 1, 0), down = std::min(y + 1, g - 1);
            for (int x = 0; x < g; ++x) {
                const int left = std::max(x - 1, 0), right = std::min(x + 1, g - 1);
                const size_t c = at(x, y, g);
                const float lap = Hn[at(x, up, g)] + Hn[at(x, down, g)] + Hn[at(left, y, g)] +
                                  Hn[at(right, y, g)] - 4.0f * Hn[c];
                H[c] = Hn[c] + (land[c] ? kDiffuse * lap : 0.0f);
            }
        }
    }
}

// Slope slumping: land that drops more steeply than the talus angle slides part way down
void thermalSlump(Field& H, float sea, int iters, float talus, float rate, int g) {
    const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (int it = 0; it < iters; ++it) {
        Field delta(H.size(), 0.0f);
        for (int y = 0; y < g; ++y) {
            for (int x = 0; x < g; ++x) {
                const size_t c = at(x, y, g);
                if (H[c] < sea) continue;
                for (const auto& d : dirs) {
                    const int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= g || ny >= g) continue;
                    const size_t nb = at(nx, ny, g);
                    const float drop = H[c] - H[nb];
                    if (drop > talus) {
                        const float f = rate * 0.5f * (drop - talus);
                        delta[c] -= f;
                        delta[nb] += f;
                    }
                }
            }
        }
        for (size_t c = 0; c < H.size(); ++c) {
            if (H[c] >= sea) H[c] = std::max(H[c] + delta[c], sea + 1e-4f);
        }
    }
}

// Runs erosion on the coarse grid, then carries the change back to the full corner grid
void erodeLand(const WorldSettings& s, int W, const Field& h0, float sea, Field& h1, CoarseLand& cl,
               const std::function<void(float)>& report) {
    const int n = W - 1;
    const int g = std::clamp(n / 8, 256, 512);
    cl.g = g;
    const Field E0 = downsample(h0, W, g);

    std::vector<float> lat((size_t)g);
    for (int y = 0; y < g; ++y) lat[(size_t)y] = std::fabs(2.0f * (((float)y + 0.5f) / (float)g) - 1.0f);

    const uint32_t seed = (uint32_t)s.seed * 2654435761u + 1u;
    const size_t cells = (size_t)g * g;
    Field T0(cells), warmth(cells);
    cl.land.assign(cells, 0);
    for (int y = 0; y < g; ++y) {
        const float v = ((float)y + 0.5f) / (float)g;
        const float l = lat[(size_t)y];
        for (int x = 0; x < g; ++x) {
            const size_t c = at(x, y, g);
            const float u = ((float)x + 0.5f) / (float)g;
            cl.land[c] = E0[c] >= sea;
            const float alt = std::max(E0[c] - sea, 0.0f);
            T0[c] = 1.0f - std::pow(l, 1.25f) * 0.85f - alt - 0.3f * smoothStep(0.78f, 0.95f, l) +
                    fbm(u * 3.0f, v * 3.0f, seed + 404u, 3) * 0.06f;
            warmth[c] = std::clamp(T0[c], 0.2f, 1.0f);
        }
    }

    Field rain, carried;
    windTransport(E0, cl.land, lat, warmth, g, rain, carried);
    rain = diffuse(std::move(rain), g, 3);
    carried = diffuse(std::move(carried), g, 6);

    float peak = 1e-9f;
    for (float r : rain) peak = std::max(peak, r);

    Field M0(cells), runoff(cells, 0.0f), wet(cells, 0.0f);
    double wetSum = 0.0;
    size_t landCount = 0;
    for (int y = 0; y < g; ++y) {
        const float v = ((float)y + 0.5f) / (float)g;
        for (int x = 0; x < g; ++x) {
            const size_t c = at(x, y, g);
            const float u = ((float)x + 0.5f) / (float)g;
            M0[c] = 0.6f * carried[c] + 0.4f * rain[c] / peak +
                    fbm(u * 3.5f + 13.7f, v * 3.5f + 7.1f, seed + 909u, 4) * 0.06f;
            const float evap = 0.2f + 0.6f * std::clamp(T0[c], 0.0f, 1.0f);
            if (cl.land[c]) {
                wet[c] = rain[c] * (1.0f - evap);
                wetSum += wet[c];
                ++landCount;
            }
        }
    }
    const float meanWet = landCount ? (float)(wetSum / (double)landCount) : 1.0f;
    for (size_t c = 0; c < cells; ++c) {
        runoff[c] = cl.land[c] ? 0.7f * wet[c] / std::max(meanWet, 1e-9f) + 0.3f : 0.0f;
    }

    // Water erosion, then slumping on the steep ground
    Field E = E0;
    const int passes = std::clamp(s.passes, 8, 60);
    erodeWater(E, runoff, sea, std::clamp(s.erosion, 0.0f, 1.0f), passes, g,
               [&](float f) { if (report) report(0.9f * f); });
    thermalSlump(E, sea, (int)std::lround(std::clamp(s.thermal, 0.0f, 1.0f) * 40.0f), 0.02f, 0.25f, g);

    // The change in the coarse grid is applied to the full grid
    Field delta(cells);
    for (size_t c = 0; c < cells; ++c) delta[c] = E[c] - E0[c];
    const Field deltaFull = upsample(delta, g, W);
    h1.assign(h0.size(), 0.0f);
    for (size_t c = 0; c < h1.size(); ++c) h1[c] = std::clamp(h0[c] + deltaFull[c], 0.0f, 1.0f);

    cl.land.assign(cells, 0);
    for (size_t c = 0; c < cells; ++c) cl.land[c] = E[c] >= sea;
    cl.elev = rankLand(E, cl.land);
    cl.T0 = T0;
    cl.M0 = M0;
    cl.acc = accumulate(receivers(E, g), E, runoff);
}

// ---------- stage 2: climate ----------

void climate(const WorldSettings& s, const CoarseLand& cl, Field& tempC, Field& moistC) {
    const size_t cells = cl.T0.size();
    Field T(cells), M(cells);
    for (size_t i = 0; i < cells; ++i) {
        T[i] = cl.T0[i] + s.warmth;
        M[i] = cl.M0[i] + s.wetness;
    }
    if (s.rankedClimate) {
        tempC = rankLand(T, cl.land);
        moistC = rankLand(M, cl.land);
    } else {
        tempC.resize(cells);
        moistC.resize(cells);
        for (size_t i = 0; i < cells; ++i) {
            tempC[i] = std::clamp(T[i], 0.0f, 1.0f);
            moistC[i] = std::clamp(M[i], 0.0f, 1.0f);
        }
    }
}

// ---------- stage 3: ground and coast ----------

// The ground after shaping: heights, and per tile its type, walkability and climate
struct Ground {
    Field h;                       // W x W corners, 0 to 1
    std::vector<uint8_t> biome;    // n x n tiles
    std::vector<uint8_t> walk;     // n x n tiles
    Field temp, moist, slope;      // n x n tiles
};

// Biome for a tile, from its height, slope, temperature and moisture. Mirrors the worldmaker ladder.
uint8_t classify(float elev, float h, float t, float m, float sl, float sea, float coast, float shelfW) {
    const float alt = h - sea;
    if (h < sea) {
        if (coast > -shelfW) return BIOME_SHELF;
        return h < sea - 0.055f ? BIOME_WATER : BIOME_SILT;
    }
    if (h < sea + 0.02f && sl >= 0.97f) return BIOME_CLIFF_ROCK;
    if (elev > 0.86f && sl > 0.06f) return BIOME_ROCK;
    if (sl > 0.6f && alt > 0.12f) return BIOME_SCREE;
    if (t < 0.05f) return BIOME_SNOW;
    if (t < 0.14f && alt > 0.22f) return BIOME_SNOW;
    if (t < 0.14f) return BIOME_FROZEN_CLAY;
    if (t < 0.30f) return m > 0.4f ? BIOME_CLAY : BIOME_GRAVEL;
    if (m < 0.22f && t > 0.40f) return BIOME_DESERT;
    if (m < 0.32f && t > 0.40f) return BIOME_ARID_SOIL;
    if (m < 0.18f) return BIOME_GRAVEL;
    if (m < 0.50f) return BIOME_GRASS;
    if (m < 0.86f || t <= 0.55f) return BIOME_GRASS;
    return BIOME_CLAY;
}

void ground(const WorldSettings& s, int W, const Field& h1, float sea, const CoarseLand& cl,
            const Field& tempC, const Field& moistC, Ground& out) {
    const int n = W - 1;
    const float inv = 1.0f / (float)n;
    const uint32_t seed = (uint32_t)s.seed * 2654435761u + 1u;
    const float scale = worldPerNorm(sea);
    // Slope is judged as on a 256-tile map: the same mountains spread over more tiles are gentler per tile
    const float slopeScale = scale * (float)n / 256.0f;
    const size_t corners = (size_t)W * W;

    // Distance to the coast: positive inland, negative out to sea
    std::vector<uint8_t> landMask(corners), seaMask(corners);
    for (size_t c = 0; c < corners; ++c) {
        landMask[c] = h1[c] >= sea;
        seaMask[c] = !landMask[c];
    }
    Field coast = distanceTo(seaMask, W);
    const Field offshore = distanceTo(landMask, W);
    for (size_t c = 0; c < corners; ++c) {
        if (!landMask[c]) coast[c] = -offshore[c];
    }

    // The coast wobbles a little, so it is not a smooth band
    const float wobbleAmp = std::max(2.0f, 0.003f * (float)n);
    const float beachW = std::max(0.5f, std::clamp(s.beach, 0.0f, 20.0f));
    const float shelfW = std::max(0.5f, std::clamp(s.shelf, 0.0f, 30.0f));
    const float detail = std::clamp(s.detail, 0.0f, 1.0f);
    const float steep = std::clamp(s.steep, 0.0f, 1.0f);

    // Fine rock detail, weighted by slope, then the beach and shelf profile
    Field h = h1;
    Field coastW(corners);
    for (int z = 0; z < W; ++z) {
        const float v = (float)z * inv;
        for (int x = 0; x < W; ++x) {
            const float u = (float)x * inv;
            const size_t c = at(x, z, W);
            const float sl = slopeAt(h1, W, x, z, slopeScale);
            const float fine = s.rugged ? ridged(u * 40.0f, v * 40.0f, seed + 6000u, 4) - 0.5f
                                        : fbm(u * 40.0f, v * 40.0f, seed + 6000u, 4) * 0.5f;
            const float weight = (1.0f - steep) + steep * sl;
            float hv = h1[c] + 0.3f * detail * weight * fine;
            const float wob = fbm(u * 60.0f, v * 60.0f, seed + 95u, 3) * wobbleAmp;
            const float cw = coast[c] + wob;
            const float beachF = smoothStep(0.0f, beachW, cw);
            const float shelfF = smoothStep(0.0f, shelfW, -cw);
            hv = landMask[c] ? sea + (hv - sea) * (0.25f + 0.75f * beachF)
                             : sea - (sea - hv) * (0.2f + 0.8f * shelfF);
            h[c] = std::clamp(hv, 0.0f, 1.0f);
            coastW[c] = cw;
        }
    }

    // Lowlands: a power curve sinks the middle heights and keeps the peaks
    const float L = std::clamp(s.lowlands, 0.0f, 1.0f);
    if (L > 0.0f) {
        const float top = std::max(1.0f - sea, 1e-6f);
        for (size_t c = 0; c < corners; ++c) {
            if (h[c] < sea) continue;
            const float t = std::clamp((h[c] - sea) / top, 0.0f, 1.0f);
            const float curved = sea + top * std::pow(t, 1.0f + 2.5f * L);
            h[c] = (1.0f - L) * h[c] + L * curved;
        }
    }

    // No step may be steeper than the limit, so no sheer wall survives
    softenSlopes(h, W, kMaxStepWorld / scale, 6);

    // Slopes after shaping, the floodplain threshold, then the tiles
    const float accThr = topShare(cl.acc, cl.land, 0.03f);
    const int g = cl.g;
    out.h = h;
    out.biome.assign((size_t)n * n, BIOME_GRASS);
    out.walk.assign((size_t)n * n, 1);
    out.temp.assign((size_t)n * n, 0.0f);
    out.moist.assign((size_t)n * n, 0.0f);
    out.slope.assign((size_t)n * n, 0.0f);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const size_t t = at(x, z, n);
            const size_t c00 = at(x, z, W), c10 = c00 + 1, c01 = c00 + (size_t)W, c11 = c01 + 1;
            const float hA = (h[c00] + h[c10] + h[c01] + h[c11]) * 0.25f;
            const float slopeT = (slopeAt(h, W, x, z, slopeScale) + slopeAt(h, W, x + 1, z, slopeScale) +
                                  slopeAt(h, W, x, z + 1, slopeScale) + slopeAt(h, W, x + 1, z + 1, slopeScale)) * 0.25f;
            const float coastT = (coastW[c00] + coastW[c10] + coastW[c01] + coastW[c11]) * 0.25f;
            const bool isLand = hA >= sea;

            // Climate and coarse-grid values at the tile's centre
            const float fx = toGrid((float)x + 0.5f, n, g), fy = toGrid((float)z + 0.5f, n, g);
            const float u = ((float)x + 0.5f) * inv, v = ((float)z + 0.5f) * inv;
            const float tt = std::clamp(sampleGrid(tempC, g, fx, fy) + fbm(u * 25.0f, v * 25.0f, seed + 97u, 3) * 0.08f, 0.0f, 1.0f);
            const float mm = std::clamp(sampleGrid(moistC, g, fx, fy) + fbm(u * 25.0f, v * 25.0f, seed + 98u, 3) * 0.08f, 0.0f, 1.0f);
            const float elev = sampleGrid(cl.elev, g, fx, fy);
            out.temp[t] = tt;
            out.moist[t] = mm;
            out.slope[t] = slopeT;

            uint8_t b = classify(elev, hA, tt, mm, slopeT, sea, coastT, shelfW);
            if (isLand && coastT < beachW && slopeT < 0.5f) {
                b = BIOME_BEACH;
            } else if (isLand && b == BIOME_GRASS && hA - sea < 0.05f && sampleGrid(cl.acc, g, fx, fy) > accThr) {
                b = BIOME_FLOODPLAIN;
            }
            out.biome[t] = b;
            out.walk[t] = (b == BIOME_WATER || b == BIOME_SILT || b == BIOME_CLIFF_ROCK || b == BIOME_ROCK) ? 0 : 1;
        }
    }
}

// ---------- stage 4: plants ----------

// Forest patches on the grass and floodplain. Each patch field has its own climate range, and
// forest keeps to gentle slopes. Where the patches reach the cover amount, the tile is forest.
void plants(const WorldSettings& s, int n, int g, const Ground& gr, std::vector<uint8_t>& biome) {
    const uint32_t seed = (uint32_t)s.seed * 2654435761u + 1u;
    const float thr = 1.0f - std::clamp(s.cover, 0.0f, 1.0f);
    const size_t cells = (size_t)g * g;
    auto patchGrid = [&](uint32_t sd, float scale) {
        Field P(cells);
        for (int y = 0; y < g; ++y) {
            for (int x = 0; x < g; ++x) {
                const float u = ((float)x + 0.5f) / (float)g, v = ((float)y + 0.5f) / (float)g;
                const float f = fbm(u * scale, v * scale, sd, 4) * 0.5f + 0.5f;
                const float nn = std::clamp((f - 0.2f) / 0.6f, 0.0f, 1.0f);
                P[at(x, y, g)] = std::clamp((nn - thr) / 0.08f, 0.0f, 1.0f);
            }
        }
        return P;
    };
    const Field P0 = patchGrid(seed + 3300u, 40.0f);
    const Field P1 = patchGrid(seed + 3400u, 55.0f);
    const Field P2 = patchGrid(seed + 3500u, 28.0f);

    biome = gr.biome;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const size_t t = at(x, z, n);
            const uint8_t b = gr.biome[t];
            if (b != BIOME_GRASS && b != BIOME_FLOODPLAIN) continue;
            const float fx = toGrid((float)x + 0.5f, n, g), fy = toGrid((float)z + 0.5f, n, g);
            const float tt = gr.temp[t], mm = gr.moist[t], sl = gr.slope[t];
            const float l0 = sampleGrid(P0, g, fx, fy) * softRange(tt, 0.22f, 0.84f) * softRange(mm, 0.28f, 1.0f);
            const float l1 = sampleGrid(P1, g, fx, fy) * softRange(tt, 0.20f, 0.80f) * softRange(mm, 0.22f, 0.82f);
            const float l2 = sampleGrid(P2, g, fx, fy) * softRange(tt, 0.30f, 0.76f) * softRange(mm, 0.46f, 1.0f) *
                             (sl < 0.5f ? 1.0f : 0.0f);
            if (std::max(l0, std::max(l1, l2)) > 0.5f) biome[t] = BIOME_FOREST;
        }
    }
}

// ---------- settings keys ----------

// Keys say which settings a stage was built from, so the cache knows when to rebuild it
void addKey(std::string& k, float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g;", (double)v);
    k += b;
}
void addKey(std::string& k, int v) { k += std::to_string(v) + ";"; }
void addKey(std::string& k, bool v) { k += v ? "1;" : "0;"; }

} // namespace

// ---------- the cache and the public functions ----------

struct WorldGenerator::Cache {
    std::string key[WorldGenerator::kStages];   // settings each stage was built from, with the stages before
    Field h0;                 // stage 0: land heights
    float sea = 0.5f;         // stage 0: sea level, 0 to 1
    Field h1;                 // stage 1: heights after erosion
    CoarseLand cl;            // stage 1: coarse-grid weather and water
    Field tempC, moistC;      // stage 2: climate
    Ground gr;                // stage 3: ground
    std::vector<uint8_t> biome;   // stage 4: final ground types, with forest
    int firstRebuilt = WorldGenerator::kStages;
};

WorldGenerator::WorldGenerator() : c_(new Cache) {}
WorldGenerator::~WorldGenerator() = default;

void WorldGenerator::reset() { c_.reset(new Cache); }

int WorldGenerator::firstRebuiltStage() const { return c_->firstRebuilt; }

MapData WorldGenerator::build(const WorldSettings& s, const std::function<void(float, const char*)>& progress) {
    Cache& c = *c_;
    const int n = s.size();
    const int W = n + 1;
    static const char* const kNames[kStages] = {
        "Shaping land", "Eroding and weather", "Climate", "Ground and coast", "Planting forest"
    };
    // Where each stage starts in the whole build, and how much of it the stage takes
    static const float kStart[kStages] = { 0.0f, 0.25f, 0.65f, 0.70f, 0.95f };
    static const float kWidth[kStages] = { 0.25f, 0.40f, 0.05f, 0.25f, 0.05f };
    auto report = [&](int stage, float f) {
        if (progress) progress(kStart[stage] + kWidth[stage] * std::clamp(f, 0.0f, 1.0f), kNames[stage]);
    };

    // Each stage's key includes the keys of the stages before, so a change reaches everything after it
    std::string k[kStages];
    addKey(k[0], s.seed);
    addKey(k[0], s.sizeIndex);
    addKey(k[0], s.waterShare);
    addKey(k[0], s.relief);
    addKey(k[0], s.rangeCover);
    addKey(k[0], s.plainShare);
    addKey(k[0], s.plainLevel);
    k[1] = k[0];
    addKey(k[1], s.erosion);
    addKey(k[1], s.passes);
    addKey(k[1], s.thermal);
    k[2] = k[1];
    addKey(k[2], s.rankedClimate);
    addKey(k[2], s.warmth);
    addKey(k[2], s.wetness);
    k[3] = k[2];
    addKey(k[3], s.lowlands);
    addKey(k[3], s.rugged);
    addKey(k[3], s.detail);
    addKey(k[3], s.steep);
    addKey(k[3], s.beach);
    addKey(k[3], s.shelf);
    k[4] = k[3];
    addKey(k[4], s.cover);

    int first = kStages;
    if (k[0] != c.key[0] || c.h0.empty()) {
        report(0, 0.0f);
        shapeLand(s, W, c.h0, c.sea);
        c.key[0] = k[0];
        first = std::min(first, 0);
        report(0, 1.0f);
    }
    if (k[1] != c.key[1] || c.h1.empty()) {
        erodeLand(s, W, c.h0, c.sea, c.h1, c.cl, [&](float f) { report(1, f); });
        c.key[1] = k[1];
        first = std::min(first, 1);
        report(1, 1.0f);
    }
    if (k[2] != c.key[2] || c.tempC.empty()) {
        report(2, 0.0f);
        climate(s, c.cl, c.tempC, c.moistC);
        c.key[2] = k[2];
        first = std::min(first, 2);
        report(2, 1.0f);
    }
    if (k[3] != c.key[3] || c.gr.biome.empty()) {
        report(3, 0.0f);
        ground(s, W, c.h1, c.sea, c.cl, c.tempC, c.moistC, c.gr);
        c.key[3] = k[3];
        first = std::min(first, 3);
        report(3, 1.0f);
    }
    if (k[4] != c.key[4] || c.biome.empty()) {
        report(4, 0.0f);
        plants(s, n, c.cl.g, c.gr, c.biome);
        c.key[4] = k[4];
        first = std::min(first, 4);
        report(4, 1.0f);
    }
    c.firstRebuilt = first;

    // The finished map: heights in world units with sea level at zero
    MapData m = flatMap(n);
    const float scale = worldPerNorm(c.sea);
    for (size_t i = 0; i < m.heights.size(); ++i) {
        m.heights[i] = std::clamp((c.gr.h[i] - c.sea) * scale, -HEIGHT_SCALE, HEIGHT_SCALE);
    }
    m.walkable = c.gr.walk;
    m.biome = c.biome;
    if (progress) progress(1.0f, "Done");
    return m;
}

MapData generateWorld(const WorldSettings& s, const std::function<void(float, const char*)>& progress) {
    WorldGenerator gen;
    return gen.build(s, progress);
}

// ---------- settings rows ----------

const char* genTabName(int tab) {
    static const char* const names[GEN_TAB_COUNT] = { "Land", "Erosion", "Climate", "Ground", "Plants" };
    return names[tab];
}

std::vector<GenParam> genParams(WorldSettings& s) {
    std::vector<GenParam> out;
    auto percent = [&](GenTab t, const char* label, float* v) {
        GenParam g; g.label = label; g.tab = t; g.kind = GenKind::Percent; g.f = v;
        g.lo = 0.0f; g.hi = 1.0f; g.step = 0.05f; g.decimals = 0;
        out.push_back(g);
    };
    auto number = [&](GenTab t, const char* label, float* v, float lo, float hi, float step, int dec) {
        GenParam g; g.label = label; g.tab = t; g.kind = GenKind::Number; g.f = v;
        g.lo = lo; g.hi = hi; g.step = step; g.decimals = dec;
        out.push_back(g);
    };
    auto integer = [&](GenTab t, const char* label, int* v, int lo, int hi) {
        GenParam g; g.label = label; g.tab = t; g.kind = GenKind::Integer; g.i = v;
        g.lo = (float)lo; g.hi = (float)hi; g.step = 1.0f; g.decimals = 0;
        out.push_back(g);
    };
    auto toggle = [&](GenTab t, const char* label, bool* v) {
        GenParam g; g.label = label; g.tab = t; g.kind = GenKind::Toggle; g.b = v;
        out.push_back(g);
    };

    percent(GenTab::Land, "Water", &s.waterShare);
    number(GenTab::Land, "Mountains", &s.relief, 0.25f, 2.0f, 0.05f, 2);
    percent(GenTab::Land, "Range cover", &s.rangeCover);
    percent(GenTab::Land, "Plains", &s.plainShare);
    number(GenTab::Land, "Plains height", &s.plainLevel, 0.0f, 0.5f, 0.01f, 2);

    percent(GenTab::Erosion, "Water erosion", &s.erosion);
    integer(GenTab::Erosion, "Erosion passes", &s.passes, 8, 60);
    percent(GenTab::Erosion, "Slope slumping", &s.thermal);

    toggle(GenTab::Climate, "Ranked climate", &s.rankedClimate);
    number(GenTab::Climate, "Warmth", &s.warmth, -0.3f, 0.3f, 0.01f, 2);
    number(GenTab::Climate, "Wetness", &s.wetness, -0.3f, 0.3f, 0.01f, 2);

    percent(GenTab::Ground, "Lowlands", &s.lowlands);
    toggle(GenTab::Ground, "Rugged rock", &s.rugged);
    percent(GenTab::Ground, "Rock detail", &s.detail);
    percent(GenTab::Ground, "Detail on slopes", &s.steep);
    number(GenTab::Ground, "Beach width", &s.beach, 0.0f, 20.0f, 0.5f, 1);
    number(GenTab::Ground, "Shelf width", &s.shelf, 0.0f, 30.0f, 0.5f, 1);

    percent(GenTab::Plants, "Forest cover", &s.cover);
    return out;
}
