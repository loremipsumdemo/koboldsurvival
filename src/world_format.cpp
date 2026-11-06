#include "textcase.h"
#include "world_format.h"
#include "byte_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "pak.h"
#include "surface_tiles.h"
#include "look.h"

namespace {
constexpr uint32_t FORMAT_VERSION = 2;
constexpr size_t REGION_HEADER = 16;
constexpr size_t CORNERS = (size_t)CHUNK_CORNERS * CHUNK_CORNERS;
constexpr size_t TILES = (size_t)CHUNK_TILES * CHUNK_TILES;
constexpr size_t WALK_BYTES = TILES / 8;
constexpr size_t RECORD = 1 + 4 + 4 + CORNERS * 2 + TILES + TILES + WALK_BYTES;

const char LOOK_FILE[] = "look.json";
const char SURFACE_FILE[] = "surface.ksrf";
const char SURFACE_MAGIC[4] = { 'K', 'S', 'R', 'F' };
constexpr uint32_t SURFACE_VERSION = 1;
constexpr size_t SURFACE_HEADER = 12;

float getF32(const uint8_t* p) {
    const uint32_t bits = getU32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::string regionName(int rx, int rz) {
    return "r." + std::to_string(rx) + "." + std::to_string(rz) + ".kreg";
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

}

int chunksPerSide(int size) {
    return (size + CHUNK_TILES - 1) / CHUNK_TILES;
}

namespace {
bool isPakWorld(const std::string& folder) {
    return folder.rfind(PAK_WORLD_PREFIX, 0) == 0;
}

std::string pakNameOf(const std::string& folder, const std::string& file) {
    return folder.substr(std::strlen(PAK_WORLD_PREFIX)) + "/" + file;
}

bool readWorldFile(const std::string& folder, const std::string& file, std::vector<uint8_t>& out) {
    out.clear();
    if (isPakWorld(folder)) {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (!pakFind(pakNameOf(folder, file), bytes, size)) return false;
        out.assign(bytes, bytes + size);
        return true;
    }
    std::ifstream in(std::filesystem::path(folder) / file, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
}

bool readWorldInfo(const std::string& folder, WorldInfo& out, std::string& err) {
    std::vector<uint8_t> raw;
    if (!readWorldFile(folder, "world.txt", raw)) {
        err = "no world.txt in " + folder;
        return false;
    }
    std::istringstream in(std::string(raw.begin(), raw.end()));
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

bool readChunk(const std::string& folder, const WorldInfo& info, int cx, int cz, ChunkData& out) {
    const int cps = chunksPerSide(info.size);
    if (cx < 0 || cz < 0 || cx >= cps || cz >= cps) return false;
    const int rx = cx / REGION_CHUNKS, rz = cz / REGION_CHUNKS;
    const int slot = (cz % REGION_CHUNKS) * REGION_CHUNKS + (cx % REGION_CHUNKS);

    const size_t at = REGION_HEADER + (size_t)slot * RECORD;
    std::vector<uint8_t> rec(RECORD);
    if (isPakWorld(folder)) {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (!pakFind(pakNameOf(folder, regionName(rx, rz)), bytes, size) || at + RECORD > size) return false;
        std::memcpy(rec.data(), bytes + at, RECORD);
    } else {
        std::ifstream in(std::filesystem::path(folder) / regionName(rx, rz), std::ios::binary);
        if (!in) return false;
        in.seekg((std::streamoff)at);
        in.read(reinterpret_cast<char*>(rec.data()), (std::streamsize)RECORD);
        if (!in) return false;
    }
    if (rec[0] != 1) return false;

    out = ChunkData{};
    out.cx = cx;
    out.cz = cz;
    decodeRecord(rec.data(), out);
    const float inv = 1.0f / info.heightScale;
    for (float& h : out.heights) h *= inv;
    return true;
}

bool readWorldSurface(const std::string& folder, WorldSurface& out, std::string& err) {
    std::vector<uint8_t> lookRaw;
    if (!readWorldFile(folder, LOOK_FILE, lookRaw)) { err = "no " + std::string(LOOK_FILE) + " in " + folder; return false; }
    if (!loadLookText(std::string(lookRaw.begin(), lookRaw.end()), out.look, err)) return false;

    std::vector<uint8_t> bytes;
    if (!readWorldFile(folder, SURFACE_FILE, bytes)) { err = "no " + std::string(SURFACE_FILE) + " in " + folder; return false; }
    const bool sized = bytes.size() == SURFACE_HEADER + 2 * SURFACE_TILE_BYTES;
    if (!sized || std::memcmp(bytes.data(), SURFACE_MAGIC, 4) != 0 ||
        getU32(bytes.data() + 4) != SURFACE_VERSION || getU32(bytes.data() + 8) != (uint32_t)SURFACE_TILE_SIZE) {
        err = std::string(SURFACE_FILE) + " is not a version " + std::to_string(SURFACE_VERSION) + " surface";
        return false;
    }
    const uint8_t* g = bytes.data() + SURFACE_HEADER;
    out.ground.assign(g, g + SURFACE_TILE_BYTES);
    out.water.assign(g + SURFACE_TILE_BYTES, g + 2 * SURFACE_TILE_BYTES);
    return true;
}
