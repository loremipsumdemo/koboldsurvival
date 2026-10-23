#include "world_format.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "render/surface_pixels.hpp"
#include "theme/themefile.hpp"

namespace {

constexpr uint32_t FORMAT_VERSION = 2;
const char REGION_MAGIC[4] = { 'K', 'R', 'E', 'G' };
constexpr size_t REGION_HEADER = 16;   // magic, format version, region x, region z
constexpr size_t CORNERS = (size_t)CHUNK_CORNERS * CHUNK_CORNERS;
constexpr size_t TILES = (size_t)CHUNK_TILES * CHUNK_TILES;
constexpr size_t WALK_BYTES = TILES / 8;
// One chunk record: a present flag, the lowest and highest height, 16-bit heights, ground types,
// vegetation density and packed walkability
constexpr size_t RECORD = 1 + 4 + 4 + CORNERS * 2 + TILES + TILES + WALK_BYTES;
constexpr int REGION_SLOTS = REGION_CHUNKS * REGION_CHUNKS;

const char LOOK_FILE[] = "look.json";
const char SURFACE_FILE[] = "surface.ksrf";
const char SURFACE_MAGIC[4] = { 'K', 'S', 'R', 'F' };
constexpr uint32_t SURFACE_VERSION = 1;
constexpr size_t SURFACE_HEADER = 12;   // magic, format version, tile size

void putU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}
uint32_t getU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void putF32(uint8_t* p, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    putU32(p, bits);
}
float getF32(const uint8_t* p) {
    const uint32_t bits = getU32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::string regionName(int rx, int rz) {
    return "r." + std::to_string(rx) + "." + std::to_string(rz) + ".kreg";
}

// Heights are stored as 16 bits between the chunk's lowest and highest corner
void encodeRecord(const ChunkData& c, uint8_t* p) {
    float lo = c.heights.empty() ? 0.0f : c.heights[0], hi = lo;
    for (float h : c.heights) { lo = std::min(lo, h); hi = std::max(hi, h); }
    p[0] = 1;
    putF32(p + 1, lo);
    putF32(p + 5, hi);
    const float span = hi - lo;
    uint8_t* hp = p + 9;
    for (size_t i = 0; i < CORNERS; ++i) {
        const float t = span > 0.0f ? (c.heights[i] - lo) / span : 0.0f;
        const uint16_t q = (uint16_t)std::lround(std::clamp(t, 0.0f, 1.0f) * 65535.0f);
        hp[2 * i] = (uint8_t)(q & 0xFF);
        hp[2 * i + 1] = (uint8_t)(q >> 8);
    }
    uint8_t* bp = hp + CORNERS * 2;
    std::memcpy(bp, c.biome.data(), TILES);
    uint8_t* vp = bp + TILES;
    if (c.veg.size() == TILES) std::memcpy(vp, c.veg.data(), TILES);
    else std::memset(vp, 0, TILES);
    uint8_t* wp = vp + TILES;
    std::memset(wp, 0, WALK_BYTES);
    for (size_t i = 0; i < TILES; ++i) {
        if (c.walk[i]) wp[i / 8] |= (uint8_t)(1u << (i % 8));
    }
}

void decodeRecord(const uint8_t* p, ChunkData& c) {
    const float lo = getF32(p + 1), hi = getF32(p + 5);
    const float span = hi - lo;
    c.heights.assign(CORNERS, 0.0f);
    const uint8_t* hp = p + 9;
    for (size_t i = 0; i < CORNERS; ++i) {
        const uint16_t q = (uint16_t)(hp[2 * i] | (hp[2 * i + 1] << 8));
        c.heights[i] = lo + span * (float)q / 65535.0f;
    }
    const uint8_t* bp = hp + CORNERS * 2;
    c.biome.assign(bp, bp + TILES);
    const uint8_t* vp = bp + TILES;
    c.veg.assign(vp, vp + TILES);
    const uint8_t* wp = vp + TILES;
    c.walk.assign(TILES, 0);
    for (size_t i = 0; i < TILES; ++i) c.walk[i] = (wp[i / 8] >> (i % 8)) & 1u;
}

bool writeFileAtomic(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

} // namespace

int chunksPerSide(int size) {
    return (size + CHUNK_TILES - 1) / CHUNK_TILES;
}

bool readWorldInfo(const std::string& folder, WorldInfo& out, std::string& err) {
    std::ifstream in(std::filesystem::path(folder) / "world.txt");
    if (!in) {
        err = "no world.txt in " + folder;
        return false;
    }
    WorldInfo info;
    bool formatOk = false, chunkOk = false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "format") {
            unsigned v = 0;
            ls >> v;
            formatOk = v == FORMAT_VERSION;
        } else if (key == "name") {
            std::getline(ls >> std::ws, info.name);
        } else if (key == "created") {
            ls >> info.created;
        } else if (key == "size") {
            ls >> info.size;
        } else if (key == "chunk") {
            int t = 0;
            ls >> t;
            chunkOk = t == CHUNK_TILES;
        } else if (key == "heightScale") {
            ls >> info.heightScale;
        } else if (key == "floorY") {
            ls >> info.floorY;
        }
    }
    if (!formatOk) { err = "world.txt is not a version " + std::to_string(FORMAT_VERSION) + " world"; return false; }
    if (!chunkOk) { err = "world.txt uses a different chunk size"; return false; }
    if (info.size < 4) { err = "world.txt has no valid size"; return false; }
    if (!(info.heightScale > 0.0f)) { err = "world.txt has no valid height scale"; return false; }
    out = info;
    return true;
}

std::vector<WorldEntry> listWorlds(const std::string& mapsFolder) {
    std::vector<WorldEntry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(mapsFolder, ec)) return out;

    for (const auto& entry : std::filesystem::directory_iterator(mapsFolder, ec)) {
        if (!entry.is_directory(ec)) continue;
        WorldInfo info;
        std::string err;
        if (!readWorldInfo(entry.path().string(), info, err)) continue;
        WorldEntry w;
        w.folder = entry.path().string();
        w.name = info.name.empty() ? entry.path().filename().string() : info.name;
        w.created = info.created;
        out.push_back(w);
    }
    return out;
}

void sortWorldEntries(std::vector<WorldEntry>& worlds, bool byName, bool ascending) {
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    std::stable_sort(worlds.begin(), worlds.end(), [&](const WorldEntry& a, const WorldEntry& b) {
        if (byName) {
            const std::string la = lower(a.name), lb = lower(b.name);
            if (la != lb) return ascending ? la < lb : lb < la;
            return a.created > b.created;
        }
        if (a.created != b.created) return ascending ? a.created < b.created : a.created > b.created;
        return a.name < b.name;
    });
}

bool readChunk(const std::string& folder, const WorldInfo& info, int cx, int cz, ChunkData& out) {
    const int cps = chunksPerSide(info.size);
    if (cx < 0 || cz < 0 || cx >= cps || cz >= cps) return false;
    const int rx = cx / REGION_CHUNKS, rz = cz / REGION_CHUNKS;
    const int slot = (cz % REGION_CHUNKS) * REGION_CHUNKS + (cx % REGION_CHUNKS);

    std::ifstream in(std::filesystem::path(folder) / regionName(rx, rz), std::ios::binary);
    if (!in) return false;
    in.seekg((std::streamoff)(REGION_HEADER + (size_t)slot * RECORD));
    std::vector<uint8_t> rec(RECORD);
    in.read(reinterpret_cast<char*>(rec.data()), (std::streamsize)RECORD);
    if (!in || rec[0] != 1) return false;

    out = ChunkData{};
    out.cx = cx;
    out.cz = cz;
    decodeRecord(rec.data(), out);
    return true;
}

bool writeWorld(const std::string& folder, const WorldInfo& info,
                const std::function<void(int cx, int cz, ChunkData& c)>& makeChunk, std::string& err) {
    namespace fs = std::filesystem;
    if (info.size < 4) { err = "the world has no size"; return false; }
    const int cps = chunksPerSide(info.size);
    const int regions = (cps + REGION_CHUNKS - 1) / REGION_CHUNKS;
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) { err = "could not create " + folder; return false; }

    for (int rz = 0; rz < regions; ++rz) {
        for (int rx = 0; rx < regions; ++rx) {
            std::vector<uint8_t> buf(REGION_HEADER + (size_t)REGION_SLOTS * RECORD, 0);
            std::memcpy(buf.data(), REGION_MAGIC, 4);
            putU32(buf.data() + 4, FORMAT_VERSION);
            putU32(buf.data() + 8, (uint32_t)rx);
            putU32(buf.data() + 12, (uint32_t)rz);
            for (int slot = 0; slot < REGION_SLOTS; ++slot) {
                const int cx = rx * REGION_CHUNKS + slot % REGION_CHUNKS;
                const int cz = rz * REGION_CHUNKS + slot / REGION_CHUNKS;
                if (cx >= cps || cz >= cps) continue;
                ChunkData c = emptyChunk(cx, cz);
                makeChunk(cx, cz, c);
                encodeRecord(c, buf.data() + REGION_HEADER + (size_t)slot * RECORD);
            }
            if (!writeFileAtomic(fs::path(folder) / regionName(rx, rz), buf)) {
                err = "could not write " + regionName(rx, rz);
                return false;
            }
        }
    }

    // The header is written last, so a folder with a header has every region
    std::ostringstream txt;
    txt << "format " << FORMAT_VERSION << "\n";
    txt << "name " << info.name << "\n";
    txt << "created " << info.created << "\n";
    txt << "size " << info.size << "\n";
    txt << "chunk " << CHUNK_TILES << "\n";
    txt << "heightScale " << info.heightScale << "\n";
    txt << "floorY " << info.floorY << "\n";
    const std::string s = txt.str();
    if (!writeFileAtomic(fs::path(folder) / "world.txt", std::vector<uint8_t>(s.begin(), s.end()))) {
        err = "could not write world.txt";
        return false;
    }
    return true;
}

bool writeWorldSurface(const std::string& folder, const WorldSurface& s, std::string& err) {
    namespace fs = std::filesystem;
    if (s.ground.size() != wm::SURFACE_TILE_BYTES || s.water.size() != wm::SURFACE_TILE_BYTES) {
        err = "the surface tiles are the wrong size";
        return false;
    }
    if (!wm::SaveLookFile(s.look, (fs::path(folder) / LOOK_FILE).string(), err)) return false;

    std::vector<uint8_t> bytes(SURFACE_HEADER + 2 * wm::SURFACE_TILE_BYTES);
    std::memcpy(bytes.data(), SURFACE_MAGIC, 4);
    putU32(bytes.data() + 4, SURFACE_VERSION);
    putU32(bytes.data() + 8, (uint32_t)wm::SURFACE_TILE_SIZE);
    std::memcpy(bytes.data() + SURFACE_HEADER, s.ground.data(), wm::SURFACE_TILE_BYTES);
    std::memcpy(bytes.data() + SURFACE_HEADER + wm::SURFACE_TILE_BYTES, s.water.data(), wm::SURFACE_TILE_BYTES);
    if (!writeFileAtomic(fs::path(folder) / SURFACE_FILE, bytes)) {
        err = "could not write " + std::string(SURFACE_FILE);
        return false;
    }
    return true;
}

bool readWorldSurface(const std::string& folder, WorldSurface& out, std::string& err) {
    namespace fs = std::filesystem;
    if (!wm::LoadLookFile((fs::path(folder) / LOOK_FILE).string(), out.look, err)) return false;

    std::ifstream in(fs::path(folder) / SURFACE_FILE, std::ios::binary);
    if (!in) { err = "no " + std::string(SURFACE_FILE) + " in " + folder; return false; }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool sized = bytes.size() == SURFACE_HEADER + 2 * wm::SURFACE_TILE_BYTES;
    if (!sized || std::memcmp(bytes.data(), SURFACE_MAGIC, 4) != 0 ||
        getU32(bytes.data() + 4) != SURFACE_VERSION || getU32(bytes.data() + 8) != (uint32_t)wm::SURFACE_TILE_SIZE) {
        err = std::string(SURFACE_FILE) + " is not a version " + std::to_string(SURFACE_VERSION) + " surface";
        return false;
    }
    const uint8_t* g = bytes.data() + SURFACE_HEADER;
    out.ground.assign(g, g + wm::SURFACE_TILE_BYTES);
    out.water.assign(g + wm::SURFACE_TILE_BYTES, g + 2 * wm::SURFACE_TILE_BYTES);
    return true;
}
