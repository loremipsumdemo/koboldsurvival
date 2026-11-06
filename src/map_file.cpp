#include "map_file.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "byte_io.h"
#include "grid.h"

namespace {
const char MAGIC[4] = { 'K', 'M', 'A', 'P' };
constexpr size_t HEADER_SIZE = 12;
constexpr uint32_t MAX_SIZE = 8192;

size_t cornerCount(size_t size) { return (size + 1) * (size + 1); }
}

MapData flatMap(int size) {
    MapData m;
    m.size = size;
    m.heights.assign(cornerCount((size_t)size), 0.0f);
    m.walkable.assign((size_t)size * size, 1);
    m.biome.assign((size_t)size * size, (uint8_t)Biome::None);
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

bool readMapFile(const std::string& path, MapData& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseMap(bytes.data(), bytes.size(), out);
}

