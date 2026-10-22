#include "world/chunk_gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <random>

#include "world/noise.hpp"

namespace wm {


namespace chunkgen {
namespace {

// Landforms are sized for a map of this many tiles, so they keep the same size in
// tiles at any map size. Larger maps simply contain more of them.
constexpr float kRefSize = 4096.0f;

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

// Continents before the rim, mountains, and peaks: a pure function of position.
float ContinentRaw(uint32_t seed, float u, float v) {
    const float wx = u * 3.0f + 0.9f * noise::Fbm(u * 2.0f + 5.2f, v * 2.0f + 1.3f, seed + 300u, 3);
    const float wy = v * 3.0f + 0.9f * noise::Fbm(u * 2.0f - 3.7f, v * 2.0f + 8.1f, seed + 301u, 3);
    return noise::Fbm(wx, wy, seed, 6) * 0.5f + 0.5f;
}

float HeightAt(const Globals& g, float u, float v) {
    const uint32_t seed = g.s.seed;
    const float k = static_cast<float>(g.s.size) / kRefSize;
    const float p = u * k, q = v * k;  // noise coordinates, in reference units

    // Continents, with the map edge pulled down to sea so no land touches it.
    float base = ContinentRaw(seed, p, q);
    const float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
    const float rimFloor = g.sea - 0.15f;
    base = rimFloor + (base - rimFloor) * Smooth(0.01f, 0.13f, edge);

    // Mountains: warped ridges inside a region mask, only on land.
    const float rc = std::clamp(g.s.rangeCover, 0.0f, 1.0f);
    const float regionLo = 0.35f + (0.5f - rc) * 0.6f;
    const float regionHi = regionLo + 0.35f;
    const float relief = std::clamp(g.s.relief, 0.0f, 2.0f);
    const float land = Smooth(g.sea, g.sea + 0.08f, base);
    const float region = Smooth(regionLo, regionHi, noise::Fbm(p * 2.0f, q * 2.0f, seed + 500u, 3) * 0.5f + 0.5f);
    const float wxr = 0.35f * noise::Fbm(p * 3.0f, q * 3.0f, seed + 1100u, 3);
    const float wyr = 0.35f * noise::Fbm(p * 3.0f + 4.4f, q * 3.0f - 2.1f, seed + 1101u, 3);
    const float ridge = noise::Ridged(p * 5.0f + wxr, q * 5.0f + wyr, seed + 1000u, 5);
    const float hills = noise::Fbm(p * 14.0f, q * 14.0f, seed + 2000u, 4) * 0.08f;
    float h = std::clamp(base + land * (ridge * ridge * 0.45f * region * relief + hills), 0.0f, 1.0f);

    // Extra peaks: a Perlin field picks out part of the land.
    if (h > g.shoreTop) {
        const float field = noise::PerlinFbm(p * 6.0f, q * 6.0f, seed + 4000u, 4);
        const float m = Smooth(g.peakThr - 0.12f, g.peakThr + 0.12f, field);
        if (m > 0.0f) {
            h = std::clamp(h + m * (0.02f + 0.05f * noise::Ridged(p * 12.0f, q * 12.0f, seed + 5000u, 4)), 0.0f, 1.0f);
        }
    }

    // Fine rock detail.
    h = std::clamp(h + 0.15f * 2.0f * 0.7f * noise::Fbm(p * 40.0f, q * 40.0f, seed + 6000u, 4) * 0.5f, 0.0f, 1.0f);
    return h;
}

// Same rules as the full generator's ground classifier.
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

// One vegetation patch layer, 0 to 1, at a tile.
float Patch(uint32_t s, float scale, float u, float v, float thr) {
    const float f = noise::Fbm(u * scale, v * scale, s, 4) * 0.5f + 0.5f;
    const float nn = std::clamp((f - 0.2f) / 0.6f, 0.0f, 1.0f);
    return std::clamp((nn - thr) / 0.08f, 0.0f, 1.0f);
}

}  // namespace

// ---------- tracker ----------

Tracker::Tracker(Fn fn, int stageCount)
    : fn_(std::move(fn)), count_(std::max(stageCount, 1)), start_(std::chrono::steady_clock::now()) {}

void Tracker::Stage(int index, const std::string& name) {
    index_ = index;
    name_ = name;
    frac_ = 0.0;
    Emit();
}

void Tracker::Label(const std::string& name) {
    name_ = name;
    Emit();
}

void Tracker::Fraction(double f) {
    frac_ = std::clamp(f, 0.0, 1.0);
    Emit();
}

void Tracker::Emit() {
    const double overall = std::clamp((static_cast<double>(index_ - 1) + frac_) / count_, 0.0, 1.0);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    // Pace so far, carried forward over the rest of the job.
    const double eta = overall > 0.0005 ? elapsed * (1.0 - overall) / overall : -1.0;
    if (fn_) fn_(name_, index_, count_, overall, elapsed, eta);
}

// ---------- globals ----------

// The west-east key that the biome bands are cut from. Matches the whole-map generator.
float BandKey(uint32_t seed, float u, float v) {
    return u + 0.08f * noise::Fbm(u * 4.0f, v * 4.0f, seed + 7000u, 4);
}

float BandSmooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Snow from the west edge, desert from the east, grassland between, with a soft dithered edge.
Biome BandBiome(const Globals& g, float u, float v) {
    constexpr float kBlend = 0.06f;
    const uint32_t seed = g.s.seed;
    const float key = BandKey(seed, u, v);
    const float r = std::clamp(0.5f + 0.5f * noise::Fbm(u * 40.0f, v * 40.0f, seed + 7100u, 3), 0.0f, 0.999f);
    const float snowP = BandSmooth(g.bandLo + kBlend, g.bandLo - kBlend, key);
    const float descP = BandSmooth(g.bandHi - kBlend, g.bandHi + kBlend, key);
    return r < snowP ? Biome::Snow : r >= 1.0f - descP ? Biome::Desert : Biome::Grass;
}

Globals Prepare(const Settings& s, Tracker& tr) {
    Globals g;
    g.s = s;
    constexpr int kS = 1024;  // sample grid over the map
    const float ratio = static_cast<float>(s.size) / kRefSize;
    const size_t total = static_cast<size_t>(kS) * kS;

    tr.Stage(1, "Sampling the continent shape");
    std::vector<float> cont(total);
    for (int y = 0; y < kS; ++y) {
        const float v = (y + 0.5f) / kS;
        for (int x = 0; x < kS; ++x) {
            cont[static_cast<size_t>(y) * kS + x] = ContinentRaw(s.seed, (x + 0.5f) / kS * ratio, v * ratio);
        }
        tr.Fraction((y + 1.0) / kS);
    }

    tr.Stage(2, "Choosing sea level");
    {
        std::vector<float> sorted = cont;
        const size_t k = std::min(total - 1, static_cast<size_t>(std::clamp(s.waterShare, 0.05f, 0.95f) * total));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(k), sorted.end());
        g.sea = sorted[k];
    }
    tr.Fraction(1.0);

    tr.Stage(3, "Finding the peak threshold");
    g.shoreTop = g.sea + 0.06f;
    {
        std::mt19937 rng(s.seed + 9001u);
        const float share = std::uniform_real_distribution<float>(0.10f, 0.30f)(rng);
        std::vector<float> land;
        land.reserve(total / 2);
        for (int y = 0; y < kS; ++y) {
            const float v = (y + 0.5f) / kS;
            for (int x = 0; x < kS; ++x) {
                if (cont[static_cast<size_t>(y) * kS + x] <= g.shoreTop) continue;
                land.push_back(noise::PerlinFbm((x + 0.5f) / kS * ratio * 6.0f, v * ratio * 6.0f, s.seed + 4000u, 4));
            }
            tr.Fraction((y + 1.0) / kS);
        }
        g.peakThr = 0.0f;
        if (!land.empty()) {
            const size_t k = std::min(land.size() - 1, static_cast<size_t>((1.0f - share) * land.size()));
            std::nth_element(land.begin(), land.begin() + static_cast<std::ptrdiff_t>(k), land.end());
            g.peakThr = land[k];
        }
    }
    tr.Fraction(1.0);

    // Biome band edges: the quartiles of the key over the land, so the snow and desert shares
    // hold for any seed. Sampled on a coarse grid. Beaches and sea cliffs are left out.
    tr.Stage(4, "Choosing the biome bands");
    {
        constexpr int kB = 256;
        std::vector<float> keys;
        keys.reserve(static_cast<size_t>(kB) * kB / 2);
        for (int y = 0; y < kB; ++y) {
            const float v = (y + 0.5f) / kB;
            for (int x = 0; x < kB; ++x) {
                const float u = (x + 0.5f) / kB;
                if (HeightAt(g, u, v) < g.sea + 0.03f) continue;
                keys.push_back(BandKey(s.seed, u, v));
            }
            tr.Fraction((y + 1.0) / kB);
        }
        if (keys.size() >= 8) {
            const size_t k1 = keys.size() / 4, k2 = keys.size() * 3 / 4;
            std::nth_element(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(k1), keys.end());
            g.bandLo = keys[k1];
            std::nth_element(keys.begin() + static_cast<std::ptrdiff_t>(k1) + 1,
                             keys.begin() + static_cast<std::ptrdiff_t>(k2), keys.end());
            g.bandHi = keys[k2];
            g.bands = true;
        }
    }
    tr.Fraction(1.0);
    return g;
}

// ---------- chunks ----------

void GenerateChunk(const Globals& g, int cx, int cz, int step, Chunk& out) {
    const int n = kChunk / step;
    const int H = n + 2;  // one extra sample on each side, for slopes
    const float size = static_cast<float>(g.s.size);
    const float ratio = size / kRefSize;
    const uint32_t seed = g.s.seed;
    out.cx = cx;
    out.cz = cz;
    out.step = step;
    out.n = n;

    // Tile index of sample i in a chunk whose origin is chunk index c, clamped to the map.
    auto tileAt = [&](int i, int c) { return std::clamp(c * kChunk + i * step, 0, g.s.size - 1); };

    std::vector<float> hh(static_cast<size_t>(H) * H);
    for (int j = 0; j < H; ++j) {
        const float v = (tileAt(j - 1, cz) + 0.5f) / size;
        for (int i = 0; i < H; ++i) {
            hh[static_cast<size_t>(j) * H + i] = HeightAt(g, (tileAt(i - 1, cx) + 0.5f) / size, v);
        }
    }

    const size_t count = static_cast<size_t>(n) * n;
    out.height.assign(count, 0.0f);
    out.biome.assign(count, 0);
    out.veg.assign(count, 0.0f);
    const float sea = g.sea;
    const float cover = std::clamp(g.s.cover, 0.0f, 1.0f);
    const float thr = 1.0f - cover;

    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const size_t c = static_cast<size_t>(y + 1) * H + (x + 1);
            const float h = hh[c];
            const float gx = (hh[c + 1] - hh[c - 1]) / (2.0f * step);
            const float gz = (hh[c + H] - hh[c - H]) / (2.0f * step);
            const float slope = std::clamp(std::hypot(gx, gz) * 68.0f, 0.0f, 1.0f);

            const float u = (tileAt(x, cx) + 0.5f) / size;
            const float v = (tileAt(y, cz) + 0.5f) / size;
            const float p = u * ratio, q = v * ratio;

            // Temperature falls with latitude and altitude. Moisture is a noise stand-in
            // for the wind model, which needs the whole map.
            const float l = std::fabs(2.0f * v - 1.0f);
            const float alt = std::max(h - sea, 0.0f);
            const float T = 1.0f - std::pow(l, 1.25f) * 0.85f - alt - 0.3f * Smooth(0.78f, 0.95f, l) +
                            noise::Fbm(p * 3.0f, q * 3.0f, seed + 404u, 3) * 0.06f;
            const float M = 0.5f + 0.35f * noise::Fbm(p * 3.5f + 13.7f, q * 3.5f + 7.1f, seed + 909u, 4);
            const float t = std::clamp(T + noise::Fbm(p * 25.0f, q * 25.0f, seed + 97u, 3) * 0.08f, 0.0f, 1.0f);
            const float m = std::clamp(M + noise::Fbm(p * 25.0f, q * 25.0f, seed + 98u, 3) * 0.08f, 0.0f, 1.0f);

            Biome b = Classify(h, h, t, m, slope, sea);
            if (g.bands && h >= sea && b != Biome::BeachSand && b != Biome::CliffRock) b = BandBiome(g, u, v);
            float veg = 0.0f;
            if (h >= sea && Viable(b)) {
                const float l0 = Patch(seed + 3300u, 40.0f, p, q, thr) * SoftRange(t, 0.22f, 0.84f) * SoftRange(m, 0.28f, 1.0f);
                const float l1 = Patch(seed + 3400u, 55.0f, p, q, thr) * SoftRange(t, 0.20f, 0.80f) * SoftRange(m, 0.22f, 0.82f);
                const float l2 = Patch(seed + 3500u, 28.0f, p, q, thr) * SoftRange(t, 0.30f, 0.76f) *
                                 SoftRange(m, 0.46f, 1.0f) * (slope < 0.5f ? 1.0f : 0.0f);
                veg = std::max(l0, std::max(l1, l2));
            }

            const size_t o = static_cast<size_t>(y) * n + x;
            out.height[o] = h;
            out.biome[o] = static_cast<uint8_t>(b);
            out.veg[o] = veg;
        }
    }
}

// ---------- encoding ----------

namespace {
constexpr uint32_t kMagic = 0x31434D57u;  // "WMC1"

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t Get32(const std::vector<uint8_t>& in, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(in[at + i]) << (8 * i);
    return v;
}
}  // namespace

std::vector<uint8_t> Encode(const Chunk& c) {
    std::vector<uint8_t> out;
    out.reserve(16 + c.height.size() * 4);
    Put32(out, kMagic);
    Put32(out, static_cast<uint32_t>(c.cx));
    Put32(out, static_cast<uint32_t>(c.cz));
    Put32(out, static_cast<uint32_t>(c.step));

    for (float h : c.height) {
        const auto q = static_cast<uint16_t>(std::lround(std::clamp(h, 0.0f, 1.0f) * 65535.0f));
        out.push_back(static_cast<uint8_t>(q & 0xFF));
        out.push_back(static_cast<uint8_t>(q >> 8));
    }

    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t b : c.biome) {
        acc |= (static_cast<uint32_t>(b) & 31u) << bits;
        bits += 5;
        while (bits >= 8) {
            out.push_back(static_cast<uint8_t>(acc & 0xFF));
            acc >>= 8;
            bits -= 8;
        }
    }
    if (bits > 0) out.push_back(static_cast<uint8_t>(acc & 0xFF));

    for (float v : c.veg) out.push_back(static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)));
    return out;
}

bool Decode(const std::vector<uint8_t>& in, Chunk& out) {
    if (in.size() < 16 || Get32(in, 0) != kMagic) return false;
    out.cx = static_cast<int>(Get32(in, 4));
    out.cz = static_cast<int>(Get32(in, 8));
    out.step = static_cast<int>(Get32(in, 12));
    if (out.step <= 0 || kChunk % out.step != 0) return false;
    out.n = kChunk / out.step;
    const size_t count = static_cast<size_t>(out.n) * out.n;
    const size_t biomeBytes = (count * 5 + 7) / 8;
    if (in.size() != 16 + count * 2 + biomeBytes + count) return false;

    size_t at = 16;
    out.height.resize(count);
    for (size_t i = 0; i < count; ++i, at += 2) {
        out.height[i] = static_cast<float>(in[at] | (in[at + 1] << 8)) / 65535.0f;
    }

    out.biome.resize(count);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < count; ++i) {
        while (bits < 5) {
            acc |= static_cast<uint32_t>(in[at++]) << bits;
            bits += 8;
        }
        out.biome[i] = static_cast<uint8_t>(acc & 31u);
        acc >>= 5;
        bits -= 5;
    }

    out.veg.resize(count);
    for (size_t i = 0; i < count; ++i, ++at) out.veg[i] = static_cast<float>(in[at]) / 255.0f;
    return true;
}

}  // namespace chunkgen

}  // namespace wm
