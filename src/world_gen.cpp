#include "world_gen.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include "surface_tiles.h"

namespace fs = std::filesystem;

namespace {
int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int lattice(int ix, int iz, uint32_t seed) {
    uint32_t h = seed ^ ((uint32_t)ix * 0x9E3779B1u) ^ ((uint32_t)iz * 0x85EBCA77u);
    h ^= h >> 15;
    h *= 0xC2B2AE3Du;
    h ^= h >> 13;
    h *= 0x27D4EB2Fu;
    h ^= h >> 16;
    return (int)(h >> 16) - 32768;
}

// Smooth noise from -32768 to 32767, with a feature every period tiles
int noise(int x, int z, int period, uint32_t seed) {
    const int ix = floorDiv(x, period), iz = floorDiv(z, period);
    const int64_t fx = (int64_t)(x - ix * period) * 65536 / period;
    const int64_t fz = (int64_t)(z - iz * period) * 65536 / period;
    const int64_t tx = ((fx * fx) >> 16) * (3 * 65536 - 2 * fx) >> 16;
    const int64_t tz = ((fz * fz) >> 16) * (3 * 65536 - 2 * fz) >> 16;
    const int64_t a = lattice(ix, iz, seed), b = lattice(ix + 1, iz, seed);
    const int64_t c = lattice(ix, iz + 1, seed), d = lattice(ix + 1, iz + 1, seed);
    const int64_t top = a + (((b - a) * tx) >> 16);
    const int64_t bottom = c + (((d - c) * tx) >> 16);
    return (int)(top + (((bottom - top) * tz) >> 16));
}

int layered(int x, int z, int period, int octaves, uint32_t seed) {
    int64_t sum = 0;
    int weight = 256, total = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += (int64_t)noise(x, z, std::max(2, period), seed + (uint32_t)i * 101u) * weight;
        total += weight;
        weight /= 2;
        period /= 2;
    }
    return (int)(sum / total);
}

// Sharp lines where layered noise crosses 0: mountain ridges. From 0 to 32767.
int ridged(int x, int z, int period, int octaves, uint32_t seed) {
    return 32767 - std::min(32767, 2 * std::abs(layered(x, z, period, octaves, seed)));
}

constexpr int SLOPE_LIMIT = 900;   // the most a walkable tile's corners may differ, in thousandths of a tile

// How far in from the map's edge a walled map's wall reaches, in tiles, and how much it rises each tile
constexpr int WALL_BAND = 40;
constexpr int WALL_RISE = 2400;
constexpr int VOID_DEPTH = -24000;

// How far inside the wall a tile is, with the wall's foot wandering a little. Negative: in the wall.
int insideWall(const WorldGenSettings& s, int x, int z) {
    const int edge = std::min({ x, z, s.size - x, s.size - z });
    const int wander = (noise(x, z, 48, s.seed + 21) + 32768) * WALL_BAND / 65536;
    return edge - WALL_BAND - wander;
}

// Land at height h, sinking gently into water where above runs out, with no step at the shore
int shoreAt(int h, int above) {
    if (above <= 0) return std::max(above / 2, -9000);
    return 40 + (int)((int64_t)std::max(h, 0) * std::min(above, 2500) / 2500);
}

// The ground steps in flat shelves with sharp edges, for harsh shapes
int shelved(int h, int shelf) {
    const int floorOf = floorDiv(h, shelf), inShelf = h - floorOf * shelf;
    const int rest = shelf * 3 / 4;
    return floorOf * shelf + (inShelf < rest ? 0 : (inShelf - rest) * 4);
}

// A harsh, straight-sided shape in a grid of cells: a triangle or a four-sided shard, each turned its own way. Returns how far
// inside it this tile is, in tiles. Negative: outside it.
int shardInside(int x, int z, int cell, int fill, uint32_t seed, int& island) {
    // Directions around a circle, in 1024ths, so that this stays whole numbers
    static const int SIN[12] = { 0, 512, 887, 1024, 887, 512, 0, -512, -887, -1024, -887, -512 };
    const int cx = floorDiv(x, cell), cz = floorDiv(z, cell);
    int best = -100000;
    island = 0;
    for (int j = cz - 1; j <= cz + 1; ++j) {
        for (int i = cx - 1; i <= cx + 1; ++i) {
            const int a = lattice(i, j, seed) + 32768, b = lattice(i, j, seed + 31) + 32768, c = lattice(i, j, seed + 37) + 32768;
            if (c * 100 / 65536 >= fill) continue;
            const int px = i * cell + (a * cell >> 16), pz = j * cell + (b * cell >> 16);
            const int sx = x - px, sz = z - pz;
            const int picks = lattice(i, j, seed + 41) + 32768;
            const int sides = picks % 2 == 0 ? 3 : 4;
            const int turn = (picks >> 1) % 12;
            const int reach = cell * (sides == 3 ? 18 + (lattice(i, j, seed + 43) + 32768) * 24 / 65536 : 24 + (lattice(i, j, seed + 43) + 32768) * 28 / 65536) / 100;
            int far = -100000;
            for (int side = 0; side < sides; ++side) {
                const int at = (turn + side * 12 / sides) % 12;
                far = std::max(far, (sx * SIN[(at + 3) % 12] + sz * SIN[at]) >> 10);
            }
            const int inside = reach - far;
            if (inside > best) {
                best = inside;
                island = (int)((uint32_t)(i * 73 + j * 151) & 0xFFFFu);
            }
        }
    }
    return best;
}

Biome biomeOf(const WorldGenSettings& s, int x, int z, int h, int rise, uint8_t& veg) {
    veg = 0;
    if (s.style == (int)MapStyle::Cavern || s.style == (int)MapStyle::GalaxySea || s.style == (int)MapStyle::VoidIslands || s.style == (int)MapStyle::Caldera) {
        // Bare ground, with no snow or plants
        if (h < 0) return Biome::Silt;
        if (rise > SLOPE_LIMIT) return Biome::CliffRock;
        const int patch = layered(x, z, 96, 3, s.seed + 9);
        if (patch > 6000) return Biome::Gravel;
        return patch < -6000 ? Biome::Scree : Biome::Rock;
    }
    if (h < 0) return h > -1500 ? Biome::Beach : Biome::Silt;
    if (h < 500) return Biome::Beach;
    if (rise > SLOPE_LIMIT) return Biome::CliffRock;
    const uint32_t seed = s.seed;
    const int wet = layered(x, z, 512, 3, seed + 6) + (s.moisture - 50) * 400;
    const int warm = layered(x, z, 1024, 3, seed + 7) + (s.temperature - 50) * 400 - h / 2;
    if (h > 26000 || warm < -16000) return Biome::Snow;
    if (h > 15000) return Biome::Rock;
    if (warm > 8000 && wet < -3000) return wet < -7000 ? Biome::Desert : Biome::AridSoil;
    const int growth = layered(x, z, 64, 3, seed + 8) / 256 + wet / 256 + (s.forests - 50) * 2;
    veg = (uint8_t)std::clamp(225 + growth, 0, 255);
    return growth + (s.forests - 50) > 35 ? Biome::Forest : Biome::Grass;
}

void previewColor(const MapTheme& theme, Biome b, int h, uint8_t veg, int shade, uint8_t* px) {
    int r = 0, g = 0, bl = 0;
    if (h < 0 && !theme.waterShown) {
        px[0] = 4; px[1] = 3; px[2] = 10; px[3] = 255;
        return;
    }
    switch (b) {
    case Biome::Silt:
    case Biome::Beach:
        if (h < 0) {
            const int depth = std::clamp(-h / 40, 0, 256);
            const int top[3] = { (int)(theme.waterColor >> 16) & 255, (int)(theme.waterColor >> 8) & 255, (int)theme.waterColor & 255 };
            const int deep[3] = { (int)(theme.waterDeepColor >> 16) & 255, (int)(theme.waterDeepColor >> 8) & 255, (int)theme.waterDeepColor & 255 };
            r = top[0] + (deep[0] - top[0]) * depth / 256;
            g = top[1] + (deep[1] - top[1]) * depth / 256;
            bl = top[2] + (deep[2] - top[2]) * depth / 256;
        } else {
            r = 220; g = 205; bl = 160;
        }
        break;
    case Biome::Snow: r = 235; g = 240; bl = 245; break;
    case Biome::Rock: r = 125; g = 120; bl = 115; break;
    case Biome::CliffRock: r = 90; g = 85; bl = 82; break;
    case Biome::Desert: r = 214; g = 190; bl = 135; break;
    case Biome::AridSoil: r = 178; g = 148; bl = 104; break;
    case Biome::Forest:
    default: {
        // As terrain_mesh.cpp colors them: the ground, pulled toward the canopy by its vegetation
        const int base[3] = { b == Biome::Forest ? 97 : 120, b == Biome::Forest ? 84 : 97, b == Biome::Forest ? 59 : 69 };
        const int canopy[3] = { 51, 87, 33 };
        const int share = veg * 115 / 255;   // of 256
        r = base[0] + (canopy[0] - base[0]) * share / 256;
        g = base[1] + (canopy[1] - base[1]) * share / 256;
        bl = base[2] + (canopy[2] - base[2]) * share / 256;
        break;
    }
    }
    if (h >= 0 && theme.groundTintAmount > 0) {
        const int amount = theme.groundTintAmount * 256 / 100;
        const int grey = (r + g + bl) / 3;
        r += (grey * (int)((theme.groundTint >> 16) & 255u) / 128 - r) * amount / 256;
        g += (grey * (int)((theme.groundTint >> 8) & 255u) / 128 - g) * amount / 256;
        bl += (grey * (int)(theme.groundTint & 255u) / 128 - bl) * amount / 256;
    }
    px[0] = (uint8_t)std::clamp(r + shade, 0, 255);
    px[1] = (uint8_t)std::clamp(g + shade, 0, 255);
    px[2] = (uint8_t)std::clamp(bl + shade, 0, 255);
    px[3] = 255;
}

class GeneratedSource : public ChunkSource {
public:
    explicit GeneratedSource(const WorldGenSettings& settings) : generator_(settings) {
        surface_.seed = settings.seed;
        surface_.ground = surfaceTilePixels(seededSpec(GROUND_TILE_SPEC, settings.seed));
        surface_.water = surfaceTilePixels(seededSpec(WATER_TILE_SPEC, settings.seed));
    }
    int size() const override { return generator_.settings().size; }
    float floorY() const override { return WORLD_GEN_FLOOR_Y; }
    const WorldSurface& surface() const override { return surface_; }
    bool streamed() const override { return true; }
    bool fill(int cx, int cz, ChunkData& out) const override {
        generator_.fill(cx, cz, out);
        return true;
    }
    bool picture(int px, std::vector<uint8_t>& rgba, const std::atomic<bool>& cancel) const override {
        rgba = generator_.preview(px, &cancel);
        return !rgba.empty();
    }
    bool farField(int res, std::vector<float>& heights, std::vector<uint8_t>& rgb, const std::atomic<bool>& cancel) const override {
        return generator_.farField(res, heights, rgb, cancel);
    }
    const MapTheme* theme() const override { return &generator_.settings().theme; }
    bool walledIn() const override { return mapStyleInfo(generator_.settings().style).walled; }

private:
    WorldGenerator generator_;
    WorldSurface surface_;
};
}

const MapStyleInfo& mapStyleInfo(int style) {
    static const MapStyleInfo STYLES[] = {
        { "overworld", "UILABEL_STYLE_OVERWORLD", "UILABEL_STYLE_OVERWORLD_ABOUT", 0, false },
        { "valley", "UILABEL_STYLE_VALLEY", "UILABEL_STYLE_VALLEY_ABOUT", 0, true },
        { "archipelago", "UILABEL_STYLE_ARCHIPELAGO", "UILABEL_STYLE_ARCHIPELAGO_ABOUT", 0, false },
        { "cavern", "UILABEL_STYLE_CAVERN", "UILABEL_STYLE_CAVERN_ABOUT", 1, true },
        { "galaxy", "UILABEL_STYLE_GALAXY", "UILABEL_STYLE_GALAXY_ABOUT", 2, false },
        { "void", "UILABEL_STYLE_VOID", "UILABEL_STYLE_VOID_ABOUT", 3, false },
        { "caldera", "UILABEL_STYLE_CALDERA", "UILABEL_STYLE_CALDERA_ABOUT", 4, true },
    };
    return STYLES[std::clamp(style, 0, (int)MapStyle::Count - 1)];
}

int WorldGenerator::heightAt(int x, int z) const {
    return s_.style == (int)MapStyle::Overworld ? overworldHeight(x, z) : styledHeight(x, z);
}

int WorldGenerator::styledHeight(int x, int z) const {
    const uint32_t seed = s_.seed;
    const MapStyle style = (MapStyle)s_.style;
    // The harsh shapes are barely bent, or their straight sides would turn to curves
    const int warpBy = s_.warp * (style == MapStyle::GalaxySea || style == MapStyle::VoidIslands ? 12 : 80);
    const int wx = x + (int)(((int64_t)noise(x, z, 200, seed + 11) * warpBy) >> 22);
    const int wz = z + (int)(((int64_t)noise(x, z, 200, seed + 12) * warpBy) >> 22);
    const int centerX = wx - s_.size / 2, centerZ = wz - s_.size / 2;
    const int away = (int)std::min<int64_t>(((int64_t)std::abs(centerX) + std::abs(centerZ)) * 1000 / std::max(1, s_.size), 1000);   // 0 middle, 1000 far corner
    const int detail = layered(wx, wz, 96, 4, seed + 2) * s_.hills / 100;
    const int small = layered(x, z, 8, 2, seed + 5) * 120 / 32768;
    int h = 0;

    switch (style) {
    case MapStyle::WalledValley: {
        const int c = layered(wx, wz, std::max(128, s_.size * (110 - s_.continents) / 300), 4, seed + 1);
        h = 1200 + (c + (s_.land - 35) * 500) / 6 + detail / 8 + small;
        h = shoreAt(h, c + 9000 + (s_.land - 50) * 200);   // lakes
        if (h > 0) {
            const int range = layered(x, z, std::max(256, s_.size / 4), 2, seed + 3) - 6000;
            if (range > 0) h += (int)(((int64_t)std::min(range, 12000) * ridged(wx, wz, 128, 5, seed + 4) >> 15) * s_.mountains / 40);
        }
        break;
    }
    case MapStyle::Archipelago: {
        const int c = layered(wx, wz, std::max(64, s_.size * (110 - s_.continents) / 900), 3, seed + 1) + (s_.land - 62) * 400;
        h = c <= 0 ? std::max(c * 2, -14000) : 100 + std::min(c, 6000) * 500 / 3000 + (detail + 8000) / 6 + small;
        // The sea all around the edge
        const int edge = std::min({ x, z, s_.size - x, s_.size - z }), band = std::max(32, s_.size / 12);
        if (edge < band && h > -800) h = std::min(h, h * edge / band - 3000 * (band - edge) / band);
        break;
    }
    case MapStyle::Cavern: {
        const int c = layered(wx, wz, std::max(96, s_.size * (110 - s_.continents) / 500), 3, seed + 1);
        h = 1100 + c / 12 + detail / 10 + small;
        h = shoreAt(h, c + 11000 + (s_.land - 50) * 200);   // acid pools
        // Stalagmites: thin sharp points standing up from the floor
        int island = 0;
        const int spike = shardInside(x, z, 20, 16 + s_.mountains / 3, seed + 51, island);
        if (spike > 0 && h > 0) h += spike * (1400 + (island % 8) * 220) * (40 + s_.mountains) / 85;
        break;
    }
    case MapStyle::GalaxySea: {
        int island = 0;
        const int inside = shardInside(wx, wz, std::max(96, s_.size * (110 - s_.continents) / 700), 45 + s_.land * 2 / 3, seed + 61, island) + layered(x, z, 18, 2, seed + 81) / 4000;
        h = inside <= 0 ? std::max(inside * 500, -16000) : 600 + inside * 260 + detail / 6;
        if (h > 0) {
            h += (int)(((int64_t)ridged(wx, wz, 96, 4, seed + 4)) * s_.mountains / 1100);
            h = shelved(h, 2400);
        }
        break;
    }
    case MapStyle::VoidIslands: {
        // Sheer-sided flat shelves over nothing: there is no water, and the renderer cuts off the ground far below
        int island = 0;
        int inside = shardInside(wx, wz, std::max(128, s_.size * (110 - s_.continents) / 500), 45 + s_.land * 2 / 3, seed + 71, island);
        inside += layered(x, z, 18, 2, seed + 81) / 4000;   // a broken edge
        // A large island in the middle of the map to start on
        const int middle = s_.size / 7 - std::max({ std::abs(centerX), std::abs(centerZ), (std::abs(centerX) * 2 + std::abs(centerZ)) / 3 + s_.size / 40 });
        if (middle > inside) {
            inside = middle;
            island = 7;
        }
        if (inside <= 0) return VOID_DEPTH;
        h = 1800 + (island % 8) * 1100 + (int)std::min<int64_t>(inside * 60, 1500) + detail / 7 + small;
        h += (int)(((int64_t)ridged(wx, wz, 72, 3, seed + 4)) * s_.mountains / 600);
        h = shelved(h, 2000);
        return std::max(h, 200);
    }
    case MapStyle::Caldera: {
        // A ring of land around a lava sea in the middle
        const int c = layered(wx, wz, std::max(96, s_.size * (110 - s_.continents) / 400), 3, seed + 1) / 30;
        const int sea = (100 - s_.land) * 5;   // how far out the sea reaches, in thousandths of the way to the corner
        h = (away - sea) * 12 + c + detail / 8 + small;
        h = h <= 0 ? std::max(h * 2, -14000) : 150 + std::min(h, 4800) / 2 + (int)((int64_t)std::max(h - 4800, 0) * s_.mountains / 40);
        break;
    }
    default:
        break;
    }

    if (mapStyleInfo(s_.style).walled) {
        const int inside = insideWall(s_, x, z);
        if (inside < 0) {
            const int jag = (layered(x, z, 16, 2, seed + 22) + 32768) / 40;
            h = std::max(h, 1500) + -inside * WALL_RISE + jag;
        }
    }
    return std::max(h, -24000);
}

int WorldGenerator::overworldHeight(int x, int z) const {
    const uint32_t seed = s_.seed;
    const int warpBy = s_.warp * 120;
    const int wx = x + (int)(((int64_t)noise(x, z, 300, seed + 11) * warpBy) >> 22);
    const int wz = z + (int)(((int64_t)noise(x, z, 300, seed + 12) * warpBy) >> 22);

    int c = layered(wx, wz, std::max(128, s_.size * (110 - s_.continents) / 200), 4, seed + 1);
    // The sea all around the edge, so the world ends in water
    const int edge = std::min({ x, z, s_.size - x, s_.size - z });
    const int band = std::max(32, s_.size / 8);
    if (edge < band) {
        const int64_t t = (int64_t)(band - edge) * 256 / band;
        c -= (int)(40000 * t * t >> 16);
    }
    const int above = c - (50 - s_.land) * 400;
    if (above <= 0) return std::max(above * 3 / 2, -24000);

    // Rises quickly off the shore, so low ground is only ever at the coast
    int64_t h = 100 + std::min(above, 3000) * 1100 / 3000 + std::min(above, 16000) * 2000 / 16000;
    const int inland = std::min(above, 2000);
    h += (int64_t)(layered(wx, wz, 96, 4, seed + 2) + 32768) * s_.hills * 40 / 65536 * inland / 2000;
    const int range = layered(x, z, std::max(256, s_.size / 4), 2, seed + 3) - 3000;
    if (range > 0) {
        const int64_t ridge = ridged(wx, wz, 128, 5, seed + 4);
        h += ((int64_t)std::min(range, 16000) * ridge >> 15) * s_.mountains * 5 * inland / 2000 / 1000 * 6;
    }
    h += (int64_t)layered(x, z, 8, 2, seed + 5) * 120 / 32768 * inland / 2000;
    return (int)std::max<int64_t>(h, 1);
}

void WorldGenerator::fill(int cx, int cz, ChunkData& out) const {
    out = emptyChunk(cx, cz);
    std::vector<int> corners((size_t)CHUNK_CORNERS * CHUNK_CORNERS);
    const int x0 = cx * CHUNK_TILES, z0 = cz * CHUNK_TILES;
    for (int j = 0; j < CHUNK_CORNERS; ++j) {
        for (int i = 0; i < CHUNK_CORNERS; ++i) {
            const int h = heightAt(x0 + i, z0 + j);
            corners[(size_t)j * CHUNK_CORNERS + i] = h;
            out.heights[(size_t)j * CHUNK_CORNERS + i] = (float)h / 1000.0f;
        }
    }
    for (int j = 0; j < CHUNK_TILES; ++j) {
        for (int i = 0; i < CHUNK_TILES; ++i) {
            const int* c = &corners[(size_t)j * CHUNK_CORNERS + i];
            const int a = c[0], b = c[1], d = c[CHUNK_CORNERS], e = c[CHUNK_CORNERS + 1];
            const int low = std::min({ a, b, d, e }), high = std::max({ a, b, d, e });
            const size_t t = (size_t)j * CHUNK_TILES + i;
            const int tx = x0 + i, tz = z0 + j;
            uint8_t veg = 0;
            out.biome[t] = (uint8_t)biomeOf(s_, tx, tz, (a + b + d + e) / 4, high - low, veg);
            out.veg[t] = veg;
            const bool inWall = mapStyleInfo(s_.style).walled && insideWall(s_, tx, tz) < 2;
            out.walk[t] = tx < s_.size && tz < s_.size && low > 0 && high - low <= SLOPE_LIMIT && !inWall ? 1 : 0;
        }
    }
}

std::vector<uint8_t> WorldGenerator::preview(int px, const std::atomic<bool>* cancel) const {
    std::atomic<bool> never{ false };
    const std::atomic<bool>& stop = cancel ? *cancel : never;
    // Runs body(row) for every row, sharing the rows out between threads
    const auto eachRow = [&](const auto& body) {
        std::atomic<int> next{ 0 };
        const auto work = [&]() {
            for (int j = next++; j < px && !stop.load(); j = next++) body(j);
        };
        const int helpers = std::clamp((int)std::thread::hardware_concurrency() - 2, 0, 15);
        std::vector<std::thread> threads;
        for (int i = 0; i < helpers; ++i) threads.emplace_back(work);
        work();
        for (std::thread& t : threads) t.join();
    };
    const auto tile = [&](int i) { return (int)((int64_t)i * s_.size / px); };

    std::vector<int> heights((size_t)px * px);
    eachRow([&](int j) {
        const int z = tile(j);
        for (int i = 0; i < px; ++i) heights[(size_t)j * px + i] = heightAt(tile(i), z);
    });
    std::vector<uint8_t> out((size_t)px * px * 4);
    eachRow([&](int j) {
        const int z = tile(j);
        for (int i = 0; i < px; ++i) {
            const int h = heights[(size_t)j * px + i];
            const int left = i > 0 ? heights[(size_t)j * px + i - 1] : h;
            const int above = j > 0 ? heights[(size_t)(j - 1) * px + i] : h;
            uint8_t veg = 0;
            const Biome b = biomeOf(s_, tile(i), z, h, 0, veg);
            const int shade = h > 0 ? std::clamp((int)(((int64_t)(h - left) + (h - above)) * 40 * px / s_.size / 256), -60, 60) : 0;
            previewColor(s_.theme, b, h, veg, shade, &out[((size_t)j * px + i) * 4]);
        }
    });
    if (stop.load()) out.clear();
    return out;
}

bool WorldGenerator::farField(int res, std::vector<float>& heights, std::vector<uint8_t>& rgb, const std::atomic<bool>& stop) const {
    if (res < 2) return false;
    const auto eachRow = [&](const auto& body) {
        std::atomic<int> next{ 0 };
        const auto work = [&]() {
            for (int j = next++; j < res && !stop.load(); j = next++) body(j);
        };
        const int helpers = std::clamp((int)std::thread::hardware_concurrency() - 2, 0, 15);
        std::vector<std::thread> threads;
        for (int i = 0; i < helpers; ++i) threads.emplace_back(work);
        work();
        for (std::thread& t : threads) t.join();
    };
    const auto at = [&](int i) { return (int)((int64_t)i * s_.size / (res - 1)); };
    std::vector<int> mm((size_t)res * res);
    eachRow([&](int j) {
        for (int i = 0; i < res; ++i) mm[(size_t)j * res + i] = heightAt(at(i), at(j));
    });
    heights.resize((size_t)res * res);
    rgb.resize((size_t)res * res * 3);
    eachRow([&](int j) {
        for (int i = 0; i < res; ++i) {
            const size_t k = (size_t)j * res + i;
            const int h = mm[k];
            heights[k] = (float)h / 1000.0f;
            uint8_t veg = 0, px[4];
            const Biome b = biomeOf(s_, at(i), at(j), h, 0, veg);
            previewColor(s_.theme, b, h, veg, 0, px);
            rgb[k * 3] = px[0];
            rgb[k * 3 + 1] = px[1];
            rgb[k * 3 + 2] = px[2];
        }
    });
    return !stop.load();
}

std::shared_ptr<const ChunkSource> generatedChunkSource(const WorldGenSettings& settings) {
    return std::make_shared<GeneratedSource>(settings);
}

std::string safeFileName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        // Bytes of a non-English letter (UTF-8, 0x80 and up) are kept, so names in other languages work; no ASCII punctuation
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || u >= 0x80;
        if (ok) out += c;
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

std::string mapFilePath(const std::string& name) {
    return std::string(MAP_FOLDER) + "/" + name + ".map";
}

std::string mapFileText(const WorldGenSettings& s) {
    std::ostringstream out;
    out << "format 1\ngenerator " << WORLD_GEN_VERSION << "\nseed " << s.seed << "\nsize " << s.size << "\nland " << s.land
        << "\ncontinents " << s.continents << "\nwarp " << s.warp << "\nhills " << s.hills << "\nmountains " << s.mountains
        << "\nmoisture " << s.moisture << "\ntemperature " << s.temperature << "\nforests " << s.forests << "\n";
    if (s.style != (int)MapStyle::Overworld) out << "style " << mapStyleInfo(s.style).fileName << "\n";
    const std::string theme = themeFileText(s.theme, s.themePreset);
    if (theme != std::string("theme ") + themePresets()[0].fileName + "\n") out << theme;
    return out.str();
}

bool mapFromText(const std::string& text, WorldGenSettings& out, std::string& err) {
    WorldGenSettings s;
    std::istringstream in(text);
    std::string line;
    int format = 0, generator = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        // A key this game doesn't know is skipped, so a newer file still opens as far as it can
        if (key == "format") ls >> format;
        else if (key == "generator") ls >> generator;
        else if (key == "seed") ls >> s.seed;
        else if (key == "size") ls >> s.size;
        else if (key == "land") ls >> s.land;
        else if (key == "continents") ls >> s.continents;
        else if (key == "warp") ls >> s.warp;
        else if (key == "hills") ls >> s.hills;
        else if (key == "mountains") ls >> s.mountains;
        else if (key == "moisture") ls >> s.moisture;
        else if (key == "temperature") ls >> s.temperature;
        else if (key == "forests") ls >> s.forests;
        else if (key == "style") {
            std::string name;
            ls >> name;
            for (int i = 0; i < (int)MapStyle::Count; ++i) {
                if (name == mapStyleInfo(i).fileName) s.style = i;
            }
        } else {
            std::string value;
            ls >> value;
            themeLineFromText(key, value, s.theme, s.themePreset);
        }
    }
    if (format != 1) { err = "not a version 1 map"; return false; }
    if (generator != WORLD_GEN_VERSION) { err = "made by generator " + std::to_string(generator) + ", which this game doesn't have"; return false; }
    if (s.size < 64 || s.size > 65536 || s.size % CHUNK_TILES != 0) { err = "no valid size"; return false; }
    out = s;
    return true;
}

bool writeMapFile(const std::string& path, const WorldGenSettings& settings, std::string& err) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    std::ofstream out(fs::u8path(path), std::ios::binary);
    out << mapFileText(settings);
    out.close();
    if (!out) { err = "could not write " + path; return false; }
    return true;
}

bool readMapFile(const std::string& path, WorldGenSettings& out, std::string& err) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) { err = "could not read " + path; return false; }
    std::ostringstream all;
    all << in.rdbuf();
    return mapFromText(all.str(), out, err);
}

std::vector<std::string> listMapFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(MAP_FOLDER), ec)) {
        if (entry.path().extension() == ".map") out.push_back(entry.path().stem().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}
