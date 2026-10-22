#include "map.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "grid.h"

namespace {

const char MAGIC[4] = { 'K', 'M', 'A', 'P' };
constexpr uint32_t VERSION = 2;
constexpr size_t HEADER_SIZE = 12;
constexpr uint32_t MAX_SIZE = 8192;

void putU32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}

uint32_t getU32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t cornerCount(size_t size) { return (size + 1) * (size + 1); }

} // namespace

MapData flatMap(int size) {
    MapData m;
    m.size = size;
    m.heights.assign(cornerCount((size_t)size), 0.0f);
    m.walkable.assign((size_t)size * size, 1);
    m.biome.assign((size_t)size * size, BIOME_NONE);
    return m;
}

bool parseMap(const unsigned char* bytes, size_t total, MapData& out) {
    if (!bytes || total < HEADER_SIZE) return false;
    if (std::memcmp(bytes, MAGIC, 4) != 0) return false;
    const uint32_t version = getU32(bytes + 4);
    const uint32_t size = getU32(bytes + 8);
    if (size < 4 || size > MAX_SIZE) return false;

    const size_t verts = cornerCount(size), tiles = (size_t)size * size;
    size_t expected = 0;
    if (version == 1) expected = HEADER_SIZE + verts * 4;
    if (version == 2) expected = HEADER_SIZE + verts * 4 + tiles * 2;
    if (expected == 0 || total != expected) return false;

    MapData m = flatMap((int)size);
    const unsigned char* p = bytes + HEADER_SIZE;
    for (size_t i = 0; i < verts; ++i, p += 4) {
        uint32_t bits = getU32(p);
        float h;
        std::memcpy(&h, &bits, sizeof(h));
        // A broken value becomes flat ground rather than a spike or a NaN in the mesh
        m.heights[i] = std::isfinite(h) ? std::clamp(h, -MAX_HEIGHT, MAX_HEIGHT) : 0.0f;
    }
    if (version == 2) {
        std::copy(p, p + tiles, m.walkable.begin());
        p += tiles;
        std::copy(p, p + tiles, m.biome.begin());
    }
    out = std::move(m);
    return true;
}

std::vector<unsigned char> encodeMap(const MapData& m) {
    const size_t size = (size_t)m.size;
    const size_t verts = cornerCount(size), tiles = size * size;
    std::vector<unsigned char> out;
    out.reserve(HEADER_SIZE + verts * 4 + tiles * 2);
    out.insert(out.end(), MAGIC, MAGIC + 4);
    putU32(out, VERSION);
    putU32(out, (uint32_t)size);
    for (size_t i = 0; i < verts; ++i) {
        float h = i < m.heights.size() ? m.heights[i] : 0.0f;
        uint32_t bits;
        std::memcpy(&bits, &h, sizeof(bits));
        putU32(out, bits);
    }
    for (size_t i = 0; i < tiles; ++i) out.push_back(i < m.walkable.size() ? m.walkable[i] : 1);
    for (size_t i = 0; i < tiles; ++i) out.push_back(i < m.biome.size() ? m.biome[i] : BIOME_NONE);
    return out;
}

bool readMapFile(const std::string& path, MapData& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseMap(bytes.data(), bytes.size(), out);
}

bool writeMapFile(const std::string& path, const MapData& m) {
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    // Write beside the target and rename, so a failed write never leaves a half-written map
    std::filesystem::path tmp = path + ".tmp";
    std::vector<unsigned char> bytes = encodeMap(m);
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
        if (!out) return false;
    }
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}
