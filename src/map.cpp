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
constexpr uint32_t VERSION = 1;
constexpr size_t HEADER_SIZE = 12;
constexpr size_t VERT_COUNT = (size_t)VERT_SIZE * VERT_SIZE;

void putU32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}

uint32_t getU32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

} // namespace

MapData flatMap() {
    MapData m;
    m.heights.assign(VERT_COUNT, 0.0f);
    return m;
}

bool parseMap(const unsigned char* bytes, size_t size, MapData& out) {
    if (!bytes || size != HEADER_SIZE + VERT_COUNT * 4) return false;
    if (std::memcmp(bytes, MAGIC, 4) != 0) return false;
    if (getU32(bytes + 4) != VERSION) return false;
    if (getU32(bytes + 8) != (uint32_t)MAP_SIZE) return false;

    out.heights.resize(VERT_COUNT);
    const unsigned char* p = bytes + HEADER_SIZE;
    for (size_t i = 0; i < VERT_COUNT; ++i, p += 4) {
        uint32_t bits = getU32(p);
        float h;
        std::memcpy(&h, &bits, sizeof(h));
        // A broken value becomes flat ground rather than a spike or a NaN in the mesh
        h = std::isfinite(h) ? std::clamp(h, -HEIGHT_SCALE, HEIGHT_SCALE) : 0.0f;
        out.heights[i] = h;
    }
    return true;
}

std::vector<unsigned char> encodeMap(const MapData& m) {
    std::vector<unsigned char> out;
    out.reserve(HEADER_SIZE + VERT_COUNT * 4);
    out.insert(out.end(), MAGIC, MAGIC + 4);
    putU32(out, VERSION);
    putU32(out, (uint32_t)MAP_SIZE);
    for (size_t i = 0; i < VERT_COUNT; ++i) {
        float h = i < m.heights.size() ? m.heights[i] : 0.0f;
        uint32_t bits;
        std::memcpy(&bits, &h, sizeof(bits));
        putU32(out, bits);
    }
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
