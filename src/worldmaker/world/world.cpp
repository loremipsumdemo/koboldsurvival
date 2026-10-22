#include "world/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <thread>
#include <tuple>
#include <utility>

#include "world/noise.hpp"

namespace wm {


namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

size_t Idx(int x, int y, int w) {
    return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
}

float Smooth(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Soft band: 1 inside [lo, hi], fading to 0 over `edge` on each side.
float SoftRange(float x, float lo, float hi, float edge = 0.12f) {
    const float up = std::clamp((x - (lo - edge)) / edge, 0.0f, 1.0f);
    const float down = std::clamp(((hi + edge) - x) / edge, 0.0f, 1.0f);
    return std::min(up, down);
}

// Calls f(row) for every row, split across the available hardware threads.
template <typename F>
void ParallelRows(int rows, F f) {
    unsigned threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 2;
    threads = std::min<unsigned>(threads, static_cast<unsigned>(rows));

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        const int r0 = static_cast<int>(static_cast<long long>(rows) * t / threads);
        const int r1 = static_cast<int>(static_cast<long long>(rows) * (t + 1) / threads);
        pool.emplace_back([f, r0, r1]() {
            for (int y = r0; y < r1; ++y) f(y);
        });
    }
    for (std::thread& th : pool) th.join();
}

// The value t such that `share` of the masked cells are at or above t. Found from a
// histogram, so it costs one pass over the field.
float TopShare(const std::vector<float>& field, const std::vector<uint8_t>& mask, float share) {
    float lo = kInf, hi = -kInf;
    size_t count = 0;
    for (size_t i = 0; i < field.size(); ++i) {
        if (!mask[i]) continue;
        lo = std::min(lo, field[i]);
        hi = std::max(hi, field[i]);
        ++count;
    }
    if (count == 0) return 0.0f;
    if (hi <= lo) return lo;

    constexpr int kBins = 512;
    std::vector<size_t> hist(kBins, 0);
    const float scale = static_cast<float>(kBins) / (hi - lo);
    for (size_t i = 0; i < field.size(); ++i) {
        if (!mask[i]) continue;
        const int b = std::clamp(static_cast<int>((field[i] - lo) * scale), 0, kBins - 1);
        ++hist[static_cast<size_t>(b)];
    }
    const size_t want = static_cast<size_t>(std::clamp(share, 0.0f, 1.0f) * static_cast<float>(count));
    size_t taken = 0;
    int bin = kBins - 1;
    for (; bin > 0 && taken + hist[static_cast<size_t>(bin)] < want; --bin) {
        taken += hist[static_cast<size_t>(bin)];
    }
    return lo + (hi - lo) * static_cast<float>(bin) / static_cast<float>(kBins);
}

// Average of a full-size n x n grid over each cell of a g x g grid.
std::vector<float> Downsample(const std::vector<float>& src, int n, int g) {
    std::vector<double> sum(static_cast<size_t>(g) * g, 0.0);
    std::vector<int> cnt(static_cast<size_t>(g) * g, 0);
    for (int y = 0; y < n; ++y) {
        const int cy = static_cast<int>(static_cast<long long>(y) * g / n);
        for (int x = 0; x < n; ++x) {
            const int cx = static_cast<int>(static_cast<long long>(x) * g / n);
            const size_t c = Idx(cx, cy, g);
            sum[c] += src[Idx(x, y, n)];
            ++cnt[c];
        }
    }
    std::vector<float> out(sum.size(), 0.0f);
    for (size_t i = 0; i < sum.size(); ++i) {
        if (cnt[i] > 0) out[i] = static_cast<float>(sum[i] / cnt[i]);
    }
    return out;
}

// Bilinear sample of a g x g grid at (fx, fy), in grid cell units.
float SampleGrid(const std::vector<float>& c, int g, float fx, float fy) {
    fx = std::clamp(fx, 0.0f, static_cast<float>(g - 1));
    fy = std::clamp(fy, 0.0f, static_cast<float>(g - 1));
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, g - 1), y1 = std::min(y0 + 1, g - 1);
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const float a = c[Idx(x0, y0, g)] + (c[Idx(x1, y0, g)] - c[Idx(x0, y0, g)]) * tx;
    const float b = c[Idx(x0, y1, g)] + (c[Idx(x1, y1, g)] - c[Idx(x0, y1, g)]) * tx;
    return a + (b - a) * ty;
}

// Grid coordinate of tile x on an n-tile map, for a g-cell grid covering it.
float CoarseCoord(int x, int n, int g) {
    return (static_cast<float>(x) + 0.5f) * static_cast<float>(g) / static_cast<float>(n) - 0.5f;
}

// Stretches a g x g grid over the whole n x n map.
std::vector<float> Upsample(const std::vector<float>& c, int g, int n) {
    std::vector<float> out(static_cast<size_t>(n) * n);
    ParallelRows(n, [&](int y) {
        const float fy = CoarseCoord(y, n, g);
        for (int x = 0; x < n; ++x) {
            out[Idx(x, y, n)] = SampleGrid(c, g, CoarseCoord(x, n, g), fy);
        }
    });
    return out;
}

// Rank of each value among the land cells, 0 to 1. Land-only values set the scale.
std::vector<float> RankLand(const std::vector<float>& X, const std::vector<uint8_t>& land) {
    std::vector<float> vals;
    for (size_t i = 0; i < X.size(); ++i) {
        if (land[i]) vals.push_back(X[i]);
    }
    if (vals.empty()) vals = X;
    std::sort(vals.begin(), vals.end());
    std::vector<float> out(X.size());
    const float inv = 1.0f / static_cast<float>(vals.size());
    for (size_t i = 0; i < X.size(); ++i) {
        const auto pos = std::lower_bound(vals.begin(), vals.end(), X[i]) - vals.begin();
        out[i] = std::clamp(static_cast<float>(pos) * inv, 0.0f, 1.0f);
    }
    return out;
}

// Four-neighbour blur with the edges held, `passes` times.
std::vector<float> Diffuse(std::vector<float> a, int g, int passes) {
    for (int p = 0; p < passes; ++p) {
        std::vector<float> b(a.size());
        for (int y = 0; y < g; ++y) {
            const int up = std::max(y - 1, 0), down = std::min(y + 1, g - 1);
            for (int x = 0; x < g; ++x) {
                const int left = std::max(x - 1, 0), right = std::min(x + 1, g - 1);
                b[Idx(x, y, g)] = (a[Idx(x, up, g)] + a[Idx(x, down, g)] + a[Idx(left, y, g)] +
                                   a[Idx(right, y, g)] + 4.0f * a[Idx(x, y, g)]) / 8.0f;
            }
        }
        a.swap(b);
    }
    return a;
}

// ---------- weather ----------

// Rain and carried moisture from air blowing across the map. Westerly bands blow
// one way, the others the other way. Air drops rain where the land rises, and
// loses moisture as it travels inland.
void WindTransport(const std::vector<float>& E, const std::vector<uint8_t>& land,
                   const std::vector<float>& lat, const std::vector<float>& warmth, int g,
                   std::vector<float>& rainOut, std::vector<float>& carriedOut) {
    std::vector<float> rainBy[2], carriedBy[2];
    for (int d = 0; d < 2; ++d) {
        const int dir = d == 0 ? 1 : -1;
        rainBy[d].assign(E.size(), 0.0f);
        carriedBy[d].assign(E.size(), 0.0f);
        for (int y = 0; y < g; ++y) {
            const float l = lat[static_cast<size_t>(y)];
            const float decay = 0.012f + 0.07f * std::exp(-std::pow((l - 0.33f) / 0.09f, 2.0f));
            float m = 0.0f, prev = 0.0f;
            bool first = true;
            for (int k = 0; k < g; ++k) {
                const int x = dir > 0 ? k : g - 1 - k;
                const size_t c = Idx(x, y, g);
                const float e = E[c];
                const float rise = first ? 0.0f : std::max(e - prev, 0.0f);
                if (!land[c]) m = warmth[c];
                const float rain = m * (1.0f - std::exp(-(4.0f * rise + 0.015f)));
                m -= rain;
                rainBy[d][c] = rain;
                carriedBy[d][c] = m;
                m *= (1.0f - decay);
                prev = e;
                first = false;
            }
        }
    }
    rainOut.assign(E.size(), 0.0f);
    carriedOut.assign(E.size(), 0.0f);
    for (int y = 0; y < g; ++y) {
        const float l = lat[static_cast<size_t>(y)];
        const bool westerly = l >= 0.33f && l < 0.66f;
        const int d = westerly ? 0 : 1;
        for (int x = 0; x < g; ++x) {
            rainOut[Idx(x, y, g)] = rainBy[d][Idx(x, y, g)];
            carriedOut[Idx(x, y, g)] = carriedBy[d][Idx(x, y, g)];
        }
    }
}

struct Weather {
    std::vector<float> T0, M0, runoff;
};

// Raw temperature and moisture from geography, and the runoff that feeds erosion.
Weather ComputeWeather(const std::vector<float>& E, int g, float sea, uint32_t seed) {
    const size_t cells = E.size();
    std::vector<uint8_t> land(cells);
    std::vector<float> lat(static_cast<size_t>(g));
    for (int y = 0; y < g; ++y) {
        lat[static_cast<size_t>(y)] = std::fabs(2.0f * ((static_cast<float>(y) + 0.5f) / g) - 1.0f);
    }
    std::vector<float> T0(cells), warmth(cells);
    for (int y = 0; y < g; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / g;
        const float l = lat[static_cast<size_t>(y)];
        for (int x = 0; x < g; ++x) {
            const size_t c = Idx(x, y, g);
            const float u = (static_cast<float>(x) + 0.5f) / g;
            land[c] = E[c] >= sea;
            const float alt = std::max(E[c] - sea, 0.0f);
            T0[c] = 1.0f - std::pow(l, 1.25f) * 0.85f - alt - 0.3f * Smooth(0.78f, 0.95f, l) +
                    noise::Fbm(u * 3.0f, v * 3.0f, seed + 404u, 3) * 0.06f;
            warmth[c] = std::clamp(T0[c], 0.2f, 1.0f);
        }
    }
    std::vector<float> rain, carried;
    WindTransport(E, land, lat, warmth, g, rain, carried);
    rain = Diffuse(std::move(rain), g, 3);
    carried = Diffuse(std::move(carried), g, 6);

    float peak = 1e-9f;
    for (float r : rain) peak = std::max(peak, r);

    Weather w;
    w.T0 = T0;
    w.M0.resize(cells);
    w.runoff.assign(cells, 0.0f);
    double wetSum = 0.0;
    size_t landCount = 0;
    std::vector<float> wet(cells, 0.0f);
    for (int y = 0; y < g; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / g;
        for (int x = 0; x < g; ++x) {
            const size_t c = Idx(x, y, g);
            const float u = (static_cast<float>(x) + 0.5f) / g;
            w.M0[c] = 0.6f * carried[c] + 0.4f * rain[c] / peak +
                      noise::Fbm(u * 3.5f + 13.7f, v * 3.5f + 7.1f, seed + 909u, 4) * 0.06f;
            const float evap = 0.2f + 0.6f * std::clamp(T0[c], 0.0f, 1.0f);
            if (land[c]) {
                wet[c] = rain[c] * (1.0f - evap);
                wetSum += wet[c];
                ++landCount;
            }
        }
    }
    const float meanWet = landCount ? static_cast<float>(wetSum / static_cast<double>(landCount)) : 1.0f;
    for (size_t c = 0; c < cells; ++c) {
        w.runoff[c] = land[c] ? 0.7f * wet[c] / std::max(meanWet, 1e-9f) + 0.3f : 0.0f;
    }
    return w;
}

// ---------- hydrology ----------

// The downhill neighbour of every cell, steepest first. Edge cells drain off the map (-1).
std::vector<int> Receivers(const std::vector<float>& H, int g) {
    static const int kOff[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
    std::vector<int> rec(H.size());
    for (int y = 0; y < g; ++y) {
        for (int x = 0; x < g; ++x) {
            const size_t c = Idx(x, y, g);
            const float h = H[c];
            float best = 0.0f;
            int r = static_cast<int>(c);
            for (const auto& o : kOff) {
                const int nx = x + o[1], ny = y + o[0];
                const bool off = nx < 0 || ny < 0 || nx >= g || ny >= g;
                const float nh = off ? -1.0f : H[Idx(nx, ny, g)];
                const float drop = (h - nh) / ((o[0] && o[1]) ? 1.41421356f : 1.0f);
                if (drop > best) {
                    best = drop;
                    r = off ? -1 : static_cast<int>(Idx(nx, ny, g));
                }
            }
            rec[c] = r;
        }
    }
    return rec;
}

// Water collected by each cell: its own runoff plus everything that drains into it.
std::vector<float> Accumulate(const std::vector<int>& rec, const std::vector<float>& H,
                              const std::vector<float>& runoff) {
    std::vector<uint32_t> order(H.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&H](uint32_t a, uint32_t b) { return H[a] > H[b]; });
    std::vector<float> A = runoff;
    for (uint32_t c : order) {
        const int t = rec[c];
        if (t >= 0 && t != static_cast<int>(c)) A[static_cast<size_t>(t)] += A[c];
    }
    return A;
}

// Water erosion: channels deepen where much water runs, sediment fills the pits, and
// the ground smooths a little. Land is never cut below the sea.
void ErodeCoarse(std::vector<float>& H, const std::vector<float>& runoff, float sea, float strength,
                 int passes, int g, const std::function<void(int, int)>& tell) {
    const float K = 0.0013f * strength;
    constexpr float kDiffuse = 0.12f, kSilt = 0.0015f;
    const size_t cells = H.size();
    for (int pass = 0; pass < passes; ++pass) {
        tell(pass, passes);
        std::vector<uint8_t> land(cells);
        for (size_t c = 0; c < cells; ++c) land[c] = H[c] >= sea;
        const std::vector<int> rec = Receivers(H, g);
        const std::vector<float> A = Accumulate(rec, H, runoff);

        std::vector<float> Hn(cells);
        for (size_t c = 0; c < cells; ++c) {
            const float hc = H[c];
            if (!land[c]) {
                Hn[c] = hc;
                continue;
            }
            const int r = rec[c];
            const float below = r >= 0 ? H[static_cast<size_t>(r)] : sea;
            const float slope = std::max(hc - below, 0.0f);
            const float W = 0.5f + 0.5f * Smooth(sea, sea + 0.2f, hc);
            float v = std::max(hc - K * std::sqrt(A[c]) * slope * W, sea + 1e-4f);
            if (r == static_cast<int>(c) && A[c] > 0.0f) v += std::min(0.02f, kSilt * std::sqrt(A[c]));
            Hn[c] = v;
        }
        for (int y = 0; y < g; ++y) {
            const int up = std::max(y - 1, 0), down = std::min(y + 1, g - 1);
            for (int x = 0; x < g; ++x) {
                const int left = std::max(x - 1, 0), right = std::min(x + 1, g - 1);
                const size_t c = Idx(x, y, g);
                const float lap = Hn[Idx(x, up, g)] + Hn[Idx(x, down, g)] + Hn[Idx(left, y, g)] +
                                  Hn[Idx(right, y, g)] - 4.0f * Hn[c];
                H[c] = Hn[c] + (land[c] ? kDiffuse * lap : 0.0f);
            }
        }
    }
}

// Slope slumping: land that drops more steeply than the talus angle slides part of
// the way down to its neighbours.
void Thermal(std::vector<float>& H, float sea, int iters, float talus, float rate, int g) {
    const size_t cells = H.size();
    for (int it = 0; it < iters; ++it) {
        std::vector<float> delta(cells, 0.0f);
        for (int y = 0; y < g; ++y) {
            for (int x = 0; x < g; ++x) {
                const size_t c = Idx(x, y, g);
                if (H[c] < sea) continue;
                const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& d : dirs) {
                    const int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= g || ny >= g) continue;
                    const size_t nb = Idx(nx, ny, g);
                    const float drop = H[c] - H[nb];
                    if (drop > talus) {
                        const float f = rate * 0.5f * (drop - talus);
                        delta[c] -= f;
                        delta[nb] += f;
                    }
                }
            }
        }
        for (size_t c = 0; c < cells; ++c) {
            if (H[c] >= sea) H[c] = std::max(H[c] + delta[c], sea + 1e-4f);
        }
    }
}

// Limits how far any tile can sit above or below its neighbour. The excess over the
// limit is shared between the two tiles, a few passes at a time, so steps soften
// rather than becoming a wall.
void SoftenSlopes(std::vector<float>& h, int n, float maxStep, int passes) {
    auto relax = [&h, maxStep](size_t a, size_t b) {
        const float d = h[a] - h[b];
        const float excess = std::fabs(d) - maxStep;
        if (excess <= 0.0f) return;
        const float move = 0.5f * excess * (d > 0.0f ? 1.0f : -1.0f);
        h[a] -= move;
        h[b] += move;
    };
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const size_t i = Idx(x, y, n);
                if (x + 1 < n) relax(i, Idx(x + 1, y, n));
                if (y + 1 < n) relax(i, Idx(x, y + 1, n));
            }
        }
    }
}

// ---------- terrain shape ----------

constexpr float kRimStart = 0.01f;    // distance from the edge (0 to 0.5) where the rim begins
constexpr float kRimBlend = 0.12f;    // width of the blend back to normal ground
constexpr float kPlainScale = 1.5f;   // size of the plains patches
constexpr float kBedDepth = 0.01f;    // how far a lake bed sits below its surface (normalised)

// Continents, mountains, plains and extra peaks, at full resolution. Sets sea to the
// height that leaves the chosen share of the map under water.
void ShapeLand(const GenSettings& gs, std::vector<float>& h, float& seaOut, std::atomic<int>& progress) {
    const int n = gs.size;
    const GenParams& p = gs.p;
    const uint32_t seed = gs.seed;
    const float inv = 1.0f / static_cast<float>(n);
    const size_t cells = static_cast<size_t>(n) * n;
    h.assign(cells, 0.0f);

    // Continents: domain-warped fBm.
    ParallelRows(n, [&](int y) {
        const float v = (y + 0.5f) * inv;
        for (int x = 0; x < n; ++x) {
            const float u = (x + 0.5f) * inv;
            const float wx = u * 3.0f + 0.9f * noise::Fbm(u * 2.0f + 5.2f, v * 2.0f + 1.3f, seed + 300u, 3);
            const float wy = v * 3.0f + 0.9f * noise::Fbm(u * 2.0f - 3.7f, v * 2.0f + 8.1f, seed + 301u, 3);
            h[Idx(x, y, n)] = noise::Fbm(wx, wy, seed, 6) * 0.5f + 0.5f;
        }
    });

    // Sea level: the height with the chosen share of the map below it.
    const std::vector<uint8_t> everywhere(cells, 1);
    const float sea = TopShare(h, everywhere, 1.0f - std::clamp(p.waterShare, 0.05f, 0.95f));
    seaOut = sea;

    // The rim of the map is always sea, so no land touches the edge.
    const float rimFloor = sea - 0.15f;
    ParallelRows(n, [&](int y) {
        const float v = (y + 0.5f) * inv;
        for (int x = 0; x < n; ++x) {
            const float u = (x + 0.5f) * inv;
            const float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
            const float keep = Smooth(kRimStart, kRimStart + kRimBlend, edge);
            float& hv = h[Idx(x, y, n)];
            hv = rimFloor + (hv - rimFloor) * keep;
        }
    });
    progress.store(8);

    // Mountains: ridged belts on the land. Range cover moves where the belts sit.
    const float rc = std::clamp(p.rangeCover, 0.0f, 1.0f);
    const float regionLo = 0.35f + (0.5f - rc) * 0.6f;
    const float regionHi = regionLo + 0.35f;
    const float relief = std::clamp(p.relief, 0.0f, 2.0f);
    ParallelRows(n, [&](int y) {
        const float v = (y + 0.5f) * inv;
        for (int x = 0; x < n; ++x) {
            const float u = (x + 0.5f) * inv;
            const size_t i = Idx(x, y, n);
            const float base = h[i];
            const float land = Smooth(sea, sea + 0.08f, base);
            const float region = Smooth(regionLo, regionHi,
                                        noise::Fbm(u * 2.0f, v * 2.0f, seed + 500u, 3) * 0.5f + 0.5f);
            // Ranges wander: the ridges follow a slow warp, so they curve rather than repeat.
            const float wxr = 0.35f * noise::Fbm(u * 3.0f, v * 3.0f, seed + 1100u, 3);
            const float wyr = 0.35f * noise::Fbm(u * 3.0f + 4.4f, v * 3.0f - 2.1f, seed + 1101u, 3);
            const float r = noise::Ridged(u * 5.0f + wxr, v * 5.0f + wyr, seed + 1000u, 5);
            const float hills = noise::Fbm(u * 14.0f, v * 14.0f, seed + 2000u, 4) * 0.08f;
            h[i] = std::clamp(base + land * (r * r * 0.45f * region * relief + hills), 0.0f, 1.0f);
        }
    });
    progress.store(14);

    // Extra peaks: a Perlin field picks out 10 to 30% of the land (set by the seed).
    const float shoreTop = sea + 0.06f;
    {
        std::mt19937 rng(seed + 9001u);
        const float share = std::uniform_real_distribution<float>(0.10f, 0.30f)(rng);
        std::vector<float> field(cells);
        ParallelRows(n, [&](int y) {
            const float v = (y + 0.5f) * inv;
            for (int x = 0; x < n; ++x) {
                const float u = (x + 0.5f) * inv;
                field[Idx(x, y, n)] = noise::PerlinFbm(u * 6.0f, v * 6.0f, seed + 4000u, 4);
            }
        });
        std::vector<uint8_t> landMask(cells);
        for (size_t i = 0; i < cells; ++i) landMask[i] = h[i] > shoreTop;
        const float threshold = TopShare(field, landMask, share);
        ParallelRows(n, [&](int y) {
            const float v = (y + 0.5f) * inv;
            for (int x = 0; x < n; ++x) {
                const float u = (x + 0.5f) * inv;
                const size_t i = Idx(x, y, n);
                if (h[i] <= shoreTop) continue;
                const float m = Smooth(threshold - 0.12f, threshold + 0.12f, field[i]);
                if (m <= 0.0f) continue;
                const float ridge = noise::Ridged(u * 12.0f, v * 12.0f, seed + 5000u, 4);
                h[i] = std::clamp(h[i] + m * (0.02f + 0.05f * ridge), 0.0f, 1.0f);
            }
        });
    }
    progress.store(18);

    // Plains: a share of the land is pulled down toward a plains height. The high
    // ground keeps its peaks.
    const float plainShare = std::clamp(p.plainShare, 0.0f, 1.0f);
    if (plainShare > 0.0f) {
        std::vector<float> field(cells);
        ParallelRows(n, [&](int y) {
            const float v = (y + 0.5f) * inv;
            for (int x = 0; x < n; ++x) {
                const float u = (x + 0.5f) * inv;
                field[Idx(x, y, n)] = noise::Fbm(u * kPlainScale, v * kPlainScale, seed + 2600u, 4) * 0.5f + 0.5f;
            }
        });
        std::vector<uint8_t> landMask(cells);
        for (size_t i = 0; i < cells; ++i) landMask[i] = h[i] >= sea;
        const float thr = TopShare(field, landMask, plainShare);
        const float level = sea + std::clamp(p.plainLevel, 0.0f, 0.5f);
        ParallelRows(n, [&](int y) {
            for (int x = 0; x < n; ++x) {
                const size_t i = Idx(x, y, n);
                if (h[i] < sea) continue;
                const float pull = Smooth(thr - 0.03f, thr + 0.03f, field[i]) *
                                   (1.0f - Smooth(0.25f, 0.45f, h[i] - sea));
                h[i] = std::max(h[i] + pull * (level - h[i]), sea + 1e-4f);
            }
        });
    }
    progress.store(24);
}

// ---------- ground and vegetation ----------

Biome Classify(float elev, float h, float t, float m, float sl, float sea) {
    const float alt = h - sea;
    if (h < sea - 0.055f) return Biome::Silt;
    if (h < sea) return Biome::Sand;
    if (h < sea + 0.03f && sl < 0.5f) return Biome::BeachSand;
    if (h < sea + 0.02f && sl >= 0.97f) return Biome::CliffRock;
    if (elev > 0.86f && sl > 0.06f) return Biome::Rock;
    if (sl > 0.6f && alt > 0.12f) return Biome::Scree;
    if (t < 0.05f) return Biome::Snow;
    if (t < 0.14f && alt > 0.22f) return Biome::Snow;
    if (t < 0.14f) return Biome::FrozenClay;
    if (t < 0.30f) return m > 0.4f ? Biome::Clay : Biome::Gravel;
    if (m < 0.22f && t > 0.40f) return Biome::Desert;
    if (m < 0.32f && t > 0.40f) return Biome::AridSoil;
    if (m < 0.18f) return Biome::Gravel;
    if (m < 0.50f) return Biome::Grass;
    if (m < 0.86f || t <= 0.55f) return Biome::Grass;
    return Biome::Clay;
}

bool Viable(Biome b) {
    switch (b) {
        case Biome::Sand: case Biome::BeachSand: case Biome::Snow: case Biome::Desert:
        case Biome::Rock: case Biome::Scree: case Biome::CliffRock: case Biome::Silt:
            return false;
        default:
            return true;
    }
}

// Distance in tiles from every cell to the nearest cell where target is set.
std::vector<float> DistanceTo(const std::vector<uint8_t>& target, int w, int h) {
    std::vector<float> d(target.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = target[i] ? 0.0f : kInf;
    const float diag = 1.41421356f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t c = Idx(x, y, w);
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
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            const size_t c = Idx(x, y, w);
            float v = d[c];
            if (x < w - 1) v = std::min(v, d[c + 1] + 1.0f);
            if (y < h - 1) {
                v = std::min(v, d[c + w] + 1.0f);
                if (x < w - 1) v = std::min(v, d[c + w + 1] + diag);
                if (x > 0) v = std::min(v, d[c + w - 1] + diag);
            }
            d[c] = v;
        }
    }
    const float cap = static_cast<float>(w + h);
    for (float& v : d) v = std::min(v, cap);
    return d;
}

// Slope of a height grid, 0 to 1, from central differences.
float SlopeAt(const std::vector<float>& h, int n, int x, int y) {
    const int xl = std::max(x - 1, 0), xr = std::min(x + 1, n - 1);
    const int yu = std::max(y - 1, 0), yd = std::min(y + 1, n - 1);
    const float gx = (h[Idx(xr, y, n)] - h[Idx(xl, y, n)]) / static_cast<float>(std::max(xr - xl, 1));
    const float gz = (h[Idx(x, yd, n)] - h[Idx(x, yu, n)]) / static_cast<float>(std::max(yd - yu, 1));
    return std::clamp(std::hypot(gx, gz) * 68.0f, 0.0f, 1.0f);
}

}  // namespace

// ---------- stage cache ----------

namespace {

// The settings each cached stage depends on.
struct KeyA {
    uint32_t seed = 0;
    int size = 0;
    float waterShare = 0, relief = 0, rangeCover = 0, plainShare = 0, plainLevel = 0;
};
struct KeyB {
    KeyA a;
    float erosion = 0, thermal = 0;
    int passes = 0;
};

KeyA MakeKeyA(const GenSettings& gs) {
    KeyA k;
    k.seed = gs.seed;
    k.size = gs.size;
    k.waterShare = gs.p.waterShare;
    k.relief = gs.p.relief;
    k.rangeCover = gs.p.rangeCover;
    k.plainShare = gs.p.plainShare;
    k.plainLevel = gs.p.plainLevel;
    return k;
}

bool SameA(const KeyA& a, const KeyA& b) {
    return std::tie(a.seed, a.size, a.waterShare, a.relief, a.rangeCover, a.plainShare, a.plainLevel) ==
           std::tie(b.seed, b.size, b.waterShare, b.relief, b.rangeCover, b.plainShare, b.plainLevel);
}

KeyB MakeKeyB(const GenSettings& gs) {
    KeyB k;
    k.a = MakeKeyA(gs);
    k.erosion = gs.p.erosion;
    k.passes = gs.p.passes;
    k.thermal = gs.p.thermal;
    return k;
}

bool SameB(const KeyB& a, const KeyB& b) {
    return SameA(a.a, b.a) && std::tie(a.erosion, a.passes, a.thermal) ==
                                  std::tie(b.erosion, b.passes, b.thermal);
}

}  // namespace

struct WorldGen::Cache {
    // Stage A: the shape of the land, at full size.
    bool haveA = false;
    KeyA ka;
    float sea = 0.0f;                 // normalised sea level
    std::vector<float> h0;            // normalised heights after the shape stage

    // Stage B: erosion. The grid is coarser than the map.
    bool haveB = false;
    KeyB kb;
    int g = 0;                        // coarse grid size
    std::vector<float> h1;            // full-size heights after erosion
    std::vector<float> elevC;         // coarse: height rank over land, 0 to 1
    std::vector<uint8_t> landC;       // coarse: 1 on land
    std::vector<float> T0c, M0c;      // coarse: raw temperature and moisture
};

WorldGen::WorldGen() : c_(new Cache) {}
WorldGen::~WorldGen() { delete c_; }

std::vector<float> DistanceToMask(const std::vector<uint8_t>& target, int w, int h) {
    return DistanceTo(target, w, h);
}

void WorldGen::Run(const GenSettings& gs, World& out, std::atomic<int>& progress,
                   const std::function<void(int, const std::string&)>& say) {
    const int n = gs.size;
    const GenParams& p = gs.p;
    const size_t cells = static_cast<size_t>(n) * n;
    Cache& c = *c_;
    progress.store(0);

    // Stage A: the shape, reused unless its settings changed.
    if (say) say(1, "shaping land: continents, ridges, peaks, plains");
    const KeyA ka = MakeKeyA(gs);
    if (!c.haveA || !SameA(c.ka, ka) || c.h0.size() != cells) {
        c.haveB = false;
        ShapeLand(gs, c.h0, c.sea, progress);
        c.ka = ka;
        c.haveA = true;
    }
    progress.store(25);

    // Stage B: erosion and weather, on a coarse grid. Reused unless its settings changed.
    if (say) say(2, "weather and erosion, " + std::to_string(std::clamp(p.passes, 1, 80)) + " passes");
    const KeyB kb = MakeKeyB(gs);
    if (!c.haveB || !SameB(c.kb, kb)) {
        const float sea = c.sea;
        const int g = std::min(n, std::clamp(n / 8, 256, 512));
        c.g = g;
        std::vector<float> E0 = Downsample(c.h0, n, g);
        const Weather wx = ComputeWeather(E0, g, sea, gs.seed);

        std::vector<float> E = E0;
        ErodeCoarse(E, wx.runoff, sea, std::clamp(p.erosion, 0.0f, 1.0f), std::clamp(p.passes, 1, 80), g,
                    [&](int pass, int passes) {
                        progress.store(25 + static_cast<int>(20.0f * (pass + 1) / std::max(passes, 1)));
                        if (say) say(0, "erosion pass " + std::to_string(pass + 1) + " of " + std::to_string(passes));
                    });
        Thermal(E, sea, static_cast<int>(std::round(std::clamp(p.thermal, 0.0f, 1.0f) * 40.0f)), 0.02f, 0.25f, g);

        std::vector<float> delta(E.size());
        for (size_t i = 0; i < E.size(); ++i) delta[i] = E[i] - E0[i];
        const std::vector<float> deltaFull = Upsample(delta, g, n);
        c.h1.assign(cells, 0.0f);
        for (size_t i = 0; i < cells; ++i) c.h1[i] = std::clamp(c.h0[i] + deltaFull[i], 0.0f, 1.0f);

        c.landC.assign(E.size(), 0);
        for (size_t i = 0; i < E.size(); ++i) c.landC[i] = E[i] >= sea;
        c.elevC = RankLand(E, c.landC);
        c.T0c = wx.T0;
        c.M0c = wx.M0;
        c.kb = kb;
        c.haveB = true;
    }
    progress.store(60);
    if (say) say(3, "climate: temperature, moisture, ranking");

    // Stage C: climate on the coarse grid.
    const int g = c.g;
    std::vector<float> tempC(c.T0c.size()), moistC(c.M0c.size());
    {
        std::vector<float> T(c.T0c.size()), M(c.M0c.size());
        for (size_t i = 0; i < T.size(); ++i) {
            T[i] = c.T0c[i] + p.warmth;
            M[i] = c.M0c[i] + p.wetness;
        }
        if (p.rankedClimate) {
            tempC = RankLand(T, c.landC);
            moistC = RankLand(M, c.landC);
        } else {
            for (size_t i = 0; i < T.size(); ++i) {
                tempC[i] = std::clamp(T[i], 0.0f, 1.0f);
                moistC[i] = std::clamp(M[i], 0.0f, 1.0f);
            }
        }
    }
    progress.store(62);
    if (say) say(4, "ground, coast, slopes and biomes");

    // Stage D: ground and coast, at full size.
    const float sea = c.sea;
    const float hs = gs.heightScale;
    std::vector<float> h = c.h1;
    std::vector<float> slope(cells);
    std::vector<float> temp(cells), moist(cells);
    out.biome.assign(cells, 0);
    {
        std::vector<uint8_t> land(cells);
        for (size_t i = 0; i < cells; ++i) land[i] = h[i] >= sea;

        // Distance to the coast: positive inland, negative out to sea.
        std::vector<uint8_t> seaMask(cells), landMask(cells);
        for (size_t i = 0; i < cells; ++i) {
            seaMask[i] = !land[i];
            landMask[i] = land[i];
        }
        std::vector<float> coast = DistanceTo(seaMask, n, n);
        const std::vector<float> offshore = DistanceTo(landMask, n, n);
        for (size_t i = 0; i < cells; ++i) {
            if (!land[i]) coast[i] = -offshore[i];
        }

        // The coastline wobbles a little, so it is not a smooth distance band.
        const float amp = std::max(2.0f, 0.003f * static_cast<float>(n));
        const float inv = 1.0f / static_cast<float>(n);
        const float beachW = std::max(0.5f, std::clamp(p.beach, 0.0f, 20.0f));
        const float shelfW = std::max(0.5f, std::clamp(p.shelf, 0.0f, 30.0f));
        const bool rugged = p.rugged;
        const float detail = std::clamp(p.detail, 0.0f, 1.0f);
        const float steep = std::clamp(p.steep, 0.0f, 1.0f);
        const uint32_t seed = gs.seed;

        // Fine rock detail, weighted by slope, then the beach and shelf profile.
        std::vector<float> base = h;
        ParallelRows(n, [&](int y) {
            const float v = (y + 0.5f) * inv;
            for (int x = 0; x < n; ++x) {
                const float u = (x + 0.5f) * inv;
                const size_t i = Idx(x, y, n);
                const float s = SlopeAt(base, n, x, y);
                slope[i] = s;
                const float fine = rugged ? noise::Ridged(u * 40.0f, v * 40.0f, seed + 6000u, 4) - 0.5f
                                          : noise::Fbm(u * 40.0f, v * 40.0f, seed + 6000u, 4) * 0.5f;
                const float weight = (1.0f - steep) + steep * s;
                float hv = base[i] + 0.15f * 2.0f * detail * weight * fine;
                const float wob = noise::Fbm(u * 60.0f, v * 60.0f, seed + 95u, 3) * amp;
                const float cw = coast[i] + wob;
                const float beach = Smooth(0.0f, beachW, cw);
                const float shelf = Smooth(0.0f, shelfW, -cw);
                hv = land[i] ? sea + (hv - sea) * (0.25f + 0.75f * beach)
                             : sea - (sea - hv) * (0.2f + 0.8f * shelf);
                h[i] = std::clamp(hv, 0.0f, 1.0f);
                coast[i] = cw;
            }
        });

        // Lowlands: a power curve sinks the middle heights and keeps the peaks.
        const float L = std::clamp(p.lowlands, 0.0f, 1.0f);
        if (L > 0.0f) {
            const float top = std::max(1.0f - sea, 1e-6f);
            for (size_t i = 0; i < cells; ++i) {
                if (h[i] < sea) continue;
                const float t = std::clamp((h[i] - sea) / top, 0.0f, 1.0f);
                const float curved = sea + top * std::pow(t, 1.0f + 2.5f * L);
                h[i] = (1.0f - L) * h[i] + L * curved;
            }
        }

        // No step may be steeper than about 1.5 world units per tile, so no sheer wall survives.
        constexpr float kMaxStepWorld = 1.5f;
        SoftenSlopes(h, n, kMaxStepWorld / hs, 6);

        // Slopes after shaping, then ground types from height, slope and climate.
        ParallelRows(n, [&](int y) {
            const float v = (y + 0.5f) * inv;
            for (int x = 0; x < n; ++x) {
                const float u = (x + 0.5f) * inv;
                const size_t i = Idx(x, y, n);
                slope[i] = SlopeAt(h, n, x, y);
                const float fx = CoarseCoord(x, n, g), fy = CoarseCoord(y, n, g);
                const float t = SampleGrid(tempC, g, fx, fy) +
                                noise::Fbm(u * 25.0f, v * 25.0f, seed + 97u, 3) * 0.08f;
                const float m = SampleGrid(moistC, g, fx, fy) +
                                noise::Fbm(u * 25.0f, v * 25.0f, seed + 98u, 3) * 0.08f;
                const float elev = SampleGrid(c.elevC, g, fx, fy);
                temp[i] = std::clamp(t, 0.0f, 1.0f);
                moist[i] = std::clamp(m, 0.0f, 1.0f);
                Biome b = Classify(elev, h[i], temp[i], moist[i], slope[i], sea);
                const bool isLand = h[i] >= sea;
                if (isLand && coast[i] < beachW && slope[i] < 0.5f) b = Biome::BeachSand;
                else if (!isLand && coast[i] > -shelfW) b = Biome::Sand;
                out.biome[i] = static_cast<uint8_t>(b);
            }
        });
    }
    // Biome bands over the land above sea level: about a quarter snow from the west edge,
    // a quarter desert from the east edge, and grassland between. Beaches and sea cliffs
    // keep their own biome. The shares are taken from quantiles of a wobbled west-east
    // position, so they hold for any seed.
    {
        const uint32_t seed = gs.seed;
        std::vector<float> key(cells, -1.0f);
        ParallelRows(n, [&](int y) {
            const float v = (y + 0.5f) / n;
            for (int x = 0; x < n; ++x) {
                const size_t i = Idx(x, y, n);
                const Biome b = static_cast<Biome>(out.biome[i]);
                if (!(h[i] >= sea) || b == Biome::BeachSand || b == Biome::CliffRock) continue;
                const float u = (x + 0.5f) / n;
                key[i] = u + 0.08f * noise::Fbm(u * 4.0f, v * 4.0f, seed + 7000u, 4);
            }
        });
        std::vector<float> vals;
        vals.reserve(cells / 2);
        for (float k : key) {
            if (k >= 0.0f) vals.push_back(k);
        }
        if (vals.size() >= 8) {
            const size_t k1 = vals.size() / 4, k2 = vals.size() * 3 / 4;
            std::nth_element(vals.begin(), vals.begin() + static_cast<long>(k1), vals.end());
            const float lo = vals[k1];
            std::nth_element(vals.begin() + static_cast<long>(k1) + 1, vals.begin() + static_cast<long>(k2), vals.end());
            const float hi = vals[k2];
            // Across a band edge the biomes mix: a cell takes the nearer biome's band with a
            // chance that falls off smoothly over kBlend, dithered by fine noise, so the edge
            // reads as a gradient rather than a line. The shares stay about the same.
            constexpr float kBlend = 0.06f;
            auto smooth = [](float a, float b, float x) {
                const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };
            ParallelRows(n, [&](int y) {
                for (int x = 0; x < n; ++x) {
                    const size_t i = Idx(x, y, n);
                    if (key[i] < 0.0f) continue;
                    const float r = std::clamp(0.5f + 0.5f * noise::Fbm((x + 0.5f) / n * 40.0f, (y + 0.5f) / n * 40.0f,
                                                                        seed + 7100u, 3), 0.0f, 0.999f);
                    const float snowP = smooth(lo + kBlend, lo - kBlend, key[i]);
                    const float descP = smooth(hi - kBlend, hi + kBlend, key[i]);
                    out.biome[i] = static_cast<uint8_t>(r < snowP ? Biome::Snow
                                                   : r >= 1.0f - descP ? Biome::Desert
                                                                       : Biome::Grass);
                }
            });
        }
    }
    progress.store(90);
    if (say) say(5, "vegetation patches");

    // Stage E: vegetation. Patches of grass, shrubs and forest, each with its own
    // climate range; forest keeps to gentle slopes.
    out.veg.assign(cells, 0.0f);
    {
        const float thr = 1.0f - std::clamp(p.cover, 0.0f, 1.0f);
        const uint32_t seed = gs.seed;
        auto patchGrid = [&](uint32_t s, float scale) {
            std::vector<float> P(static_cast<size_t>(g) * g);
            for (int y = 0; y < g; ++y) {
                for (int x = 0; x < g; ++x) {
                    const float u = (x + 0.5f) / g, v = (y + 0.5f) / g;
                    const float f = noise::Fbm(u * scale, v * scale, s, 4) * 0.5f + 0.5f;
                    const float nn = std::clamp((f - 0.2f) / 0.6f, 0.0f, 1.0f);
                    P[Idx(x, y, g)] = std::clamp((nn - thr) / 0.08f, 0.0f, 1.0f);
                }
            }
            return P;
        };
        const std::vector<float> P0 = patchGrid(seed + 3300u, 40.0f);
        const std::vector<float> P1 = patchGrid(seed + 3400u, 55.0f);
        const std::vector<float> P2 = patchGrid(seed + 3500u, 28.0f);
        ParallelRows(n, [&](int y) {
            for (int x = 0; x < n; ++x) {
                const size_t i = Idx(x, y, n);
                if (!(h[i] >= sea) || !Viable(static_cast<Biome>(out.biome[i]))) continue;
                const float fx = CoarseCoord(x, n, g), fy = CoarseCoord(y, n, g);
                const float t = temp[i], m = moist[i];
                const float l0 = SampleGrid(P0, g, fx, fy) * SoftRange(t, 0.22f, 0.84f) * SoftRange(m, 0.28f, 1.0f);
                const float l1 = SampleGrid(P1, g, fx, fy) * SoftRange(t, 0.20f, 0.80f) * SoftRange(m, 0.22f, 0.82f);
                const float l2 = SampleGrid(P2, g, fx, fy) * SoftRange(t, 0.30f, 0.76f) * SoftRange(m, 0.46f, 1.0f) *
                                 (slope[i] < 0.5f ? 1.0f : 0.0f);
                out.veg[i] = std::max(l0, std::max(l1, l2));
            }
        });
    }
    progress.store(98);
    if (say) say(6, "writing heights and finishing");

    // The finished world, in world units.
    out.size = n;
    out.seed = gs.seed;
    out.seaLevel = sea * hs;
    out.height.resize(cells);
    for (size_t i = 0; i < cells; ++i) out.height[i] = h[i] * hs;
    progress.store(100);
}

float SampleHeight(const World& w, float x, float z) {
    if (w.size <= 0 || w.height.empty()) return 0.0f;
    const int n = w.size;
    const float fn = static_cast<float>(n);
    const float gx = std::clamp(x + fn * 0.5f, 0.0f, fn - 1.0f);
    const float gz = std::clamp(z + fn * 0.5f, 0.0f, fn - 1.0f);

    const int x0 = static_cast<int>(gx);
    const int z0 = static_cast<int>(gz);
    const int x1 = std::min(x0 + 1, n - 1);
    const int z1 = std::min(z0 + 1, n - 1);
    const float fx = gx - static_cast<float>(x0);
    const float fz = gz - static_cast<float>(z0);

    const float h00 = w.height[Idx(x0, z0, n)];
    const float h10 = w.height[Idx(x1, z0, n)];
    const float h01 = w.height[Idx(x0, z1, n)];
    const float h11 = w.height[Idx(x1, z1, n)];
    const float top = h00 + (h10 - h00) * fx;
    const float bottom = h01 + (h11 - h01) * fx;
    return top + (bottom - top) * fz;
}

V3 BiomeColour(Biome b) {
    // Colours of the ground types, as 0 to 1 values.
    switch (b) {
        case Biome::Sand:       return {0.69f, 0.64f, 0.47f};   // seabed sand
        case Biome::Grass:      return {0.47f, 0.38f, 0.27f};   // loam
        case Biome::Forest:     return {0.38f, 0.33f, 0.23f};   // forest floor
        case Biome::Desert:     return {0.84f, 0.75f, 0.53f};   // desert sand
        case Biome::Rock:       return {0.43f, 0.42f, 0.40f};   // bedrock
        case Biome::Snow:       return {0.89f, 0.93f, 0.96f};   // ice
        case Biome::BeachSand:  return {0.87f, 0.81f, 0.63f};
        case Biome::Silt:       return {0.36f, 0.34f, 0.31f};
        case Biome::FrozenClay: return {0.59f, 0.61f, 0.62f};
        case Biome::Clay:       return {0.65f, 0.45f, 0.35f};
        case Biome::Gravel:     return {0.58f, 0.55f, 0.50f};
        case Biome::AridSoil:   return {0.70f, 0.58f, 0.41f};
        case Biome::Scree:      return {0.49f, 0.47f, 0.44f};
        case Biome::Floodplain: return {0.59f, 0.54f, 0.41f};
        case Biome::CliffRock:  return {0.36f, 0.34f, 0.33f};
    }
    return {0.69f, 0.64f, 0.47f};   // value from an older save: seabed sand
}

uint32_t RandomSeed() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint32_t> dist(1u, 999999u);
    return dist(rng);
}

}  // namespace wm
