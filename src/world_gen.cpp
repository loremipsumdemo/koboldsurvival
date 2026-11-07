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

Biome biomeOf(const WorldGenSettings& s, int x, int z, int h, int rise, uint8_t& veg) {
    veg = 0;
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

void previewColor(Biome b, int h, uint8_t veg, int shade, uint8_t* px) {
    int r = 0, g = 0, bl = 0;
    switch (b) {
    case Biome::Silt:
    case Biome::Beach:
        if (h < 0) {
            const int depth = std::clamp(-h / 120, 0, 200);
            r = 30 - depth / 10;
            g = 92 - depth / 4;
            bl = 150 - depth / 4;
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
    px[0] = (uint8_t)std::clamp(r + shade, 0, 255);
    px[1] = (uint8_t)std::clamp(g + shade, 0, 255);
    px[2] = (uint8_t)std::clamp(bl + shade, 0, 255);
    px[3] = 255;
}

class GeneratedSource : public ChunkSource {
public:
    explicit GeneratedSource(const WorldGenSettings& settings) : generator_(settings) {
        surface_.ground = surfaceTilePixels(GROUND_TILE_SPEC);
        surface_.water = surfaceTilePixels(WATER_TILE_SPEC);
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

private:
    WorldGenerator generator_;
    WorldSurface surface_;
};
}

int WorldGenerator::heightAt(int x, int z) const {
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
            out.walk[t] = tx < s_.size && tz < s_.size && low > 0 && high - low <= SLOPE_LIMIT ? 1 : 0;
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
            previewColor(b, h, veg, shade, &out[((size_t)j * px + i) * 4]);
        }
    });
    if (stop.load()) out.clear();
    return out;
}

std::shared_ptr<const ChunkSource> generatedChunkSource(const WorldGenSettings& settings) {
    return std::make_shared<GeneratedSource>(settings);
}

std::string safeFileName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
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
