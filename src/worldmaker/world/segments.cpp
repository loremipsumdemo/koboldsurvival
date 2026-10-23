#include "world/segments.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace wm {


namespace segments {
namespace {

constexpr uint32_t kMagic = 0x31534D57u;  // "WMS1"
constexpr int kChunksPerSide = kSegment / chunkgen::kChunk;

void Put32(std::ofstream& f, uint32_t v) {
    char b[4];
    for (int i = 0; i < 4; ++i) b[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
    f.write(b, 4);
}

bool Get32(std::ifstream& f, uint32_t& v) {
    unsigned char b[4];
    if (!f.read(reinterpret_cast<char*>(b), 4)) return false;
    v = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
        (static_cast<uint32_t>(b[3]) << 24);
    return true;
}

}  // namespace

std::string SegmentPath(const std::string& dir, int sx, int sz) {
    return dir + "/seg_" + std::to_string(sx) + "_" + std::to_string(sz) + ".wms";
}

bool Build(const chunkgen::Settings& s, const std::string& dir, chunkgen::Tracker& tr,
           const std::atomic<bool>& cancel, std::string& err) {
    if (s.size <= 0 || s.size % kSegment != 0) {
        err = "size must be a multiple of " + std::to_string(kSegment);
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        err = "cannot create " + dir;
        return false;
    }
    {
        std::ofstream m(dir + "/world.txt");
        if (!m) {
            err = "cannot write world.txt";
            return false;
        }
        m << "seed " << s.seed << "\nsize " << s.size << "\nheight_scale " << s.heightScale
          << "\nwater_share " << s.waterShare << "\nrelief " << s.relief << "\nrange_cover " << s.rangeCover
          << "\ncover " << s.cover << "\nsegment " << kSegment << "\nchunk " << chunkgen::kChunk << "\n";
    }

    const chunkgen::Globals g = chunkgen::Prepare(s, tr);
    {
        std::ofstream m(dir + "/world.txt", std::ios::app);
        m << std::setprecision(9) << "sea_level " << g.sea << "\n";
    }

    const int perSide = s.size / kSegment;
    const int total = perSide * perSide;
    const int chunksPerSegment = kChunksPerSide * kChunksPerSide;
    tr.Stage(5, "Writing segments");
    chunkgen::Chunk c;
    int k = 0;
    for (int sz = 0; sz < perSide; ++sz) {
        for (int sx = 0; sx < perSide; ++sx, ++k) {
            if (cancel.load()) {
                err = "canceled";
                return false;
            }
            tr.Label("Segment " + std::to_string(k + 1) + " of " + std::to_string(total));
            std::ofstream f(SegmentPath(dir, sx, sz), std::ios::binary);
            if (!f) {
                err = "cannot write segment " + std::to_string(k + 1);
                return false;
            }
            Put32(f, kMagic);
            Put32(f, static_cast<uint32_t>(sx));
            Put32(f, static_cast<uint32_t>(sz));
            Put32(f, static_cast<uint32_t>(chunksPerSegment));
            for (int j = 0; j < kChunksPerSide; ++j) {
                for (int i = 0; i < kChunksPerSide; ++i) {
                    if (cancel.load()) {
                        err = "canceled";
                        return false;
                    }
                    chunkgen::GenerateChunk(g, sx * kChunksPerSide + i, sz * kChunksPerSide + j, 1, c);
                    const std::vector<uint8_t> bytes = chunkgen::Encode(c);
                    Put32(f, static_cast<uint32_t>(bytes.size()));
                    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    const int done = j * kChunksPerSide + i + 1;
                    tr.Fraction((k + static_cast<double>(done) / chunksPerSegment) / total);
                }
            }
            if (!f) {
                err = "write failed on segment " + std::to_string(k + 1);
                return false;
            }
        }
    }
    return true;
}

bool Assemble(const std::string& dir, World& out, chunkgen::Tracker& tr, const std::atomic<bool>& cancel,
              std::string& err) {
    std::ifstream m(dir + "/world.txt");
    if (!m) {
        err = "no world.txt in " + dir;
        return false;
    }
    int size = 0;
    uint32_t seed = 1;
    float hs = 0.0f, sea = -1.0f;
    std::string key;
    while (m >> key) {
        if (key == "seed") m >> seed;
        else if (key == "size") m >> size;
        else if (key == "height_scale") m >> hs;
        else if (key == "sea_level") m >> sea;
        else {
            std::string rest;
            std::getline(m, rest);
        }
    }
    if (size <= 0 || size % kSegment != 0 || hs <= 0.0f || sea < 0.0f) {
        err = "world.txt is incomplete";
        return false;
    }

    const size_t cells = static_cast<size_t>(size) * static_cast<size_t>(size);
    out = World{};
    out.size = size;
    out.seed = seed;
    out.seaLevel = sea * hs;
    out.height.assign(cells, 0.0f);
    out.biome.assign(cells, 0);
    out.veg.assign(cells, 0.0f);

    const int perSide = size / kSegment;
    const int total = perSide * perSide;
    tr.Stage(6, "Loading segments into the viewer");
    std::vector<chunkgen::Chunk> chunks;
    int k = 0;
    for (int sz = 0; sz < perSide; ++sz) {
        for (int sx = 0; sx < perSide; ++sx, ++k) {
            if (cancel.load()) {
                err = "canceled";
                return false;
            }
            if (!ReadSegment(SegmentPath(dir, sx, sz), chunks)) {
                err = "cannot read segment " + std::to_string(k + 1);
                return false;
            }
            for (const chunkgen::Chunk& c : chunks) {
                if (c.step != 1 || c.n != chunkgen::kChunk || c.height.size() != static_cast<size_t>(chunkgen::kChunk * chunkgen::kChunk)) {
                    err = "segment " + std::to_string(k + 1) + " has an unexpected chunk";
                    return false;
                }
                for (int j = 0; j < chunkgen::kChunk; ++j) {
                    for (int i = 0; i < chunkgen::kChunk; ++i) {
                        const int tx = c.cx * chunkgen::kChunk + i;
                        const int tz = c.cz * chunkgen::kChunk + j;
                        if (tx >= size || tz >= size) continue;
                        const size_t idx = static_cast<size_t>(tz) * size + tx;
                        const size_t src = static_cast<size_t>(j * chunkgen::kChunk + i);
                        out.height[idx] = c.height[src] * hs;
                        out.biome[idx] = c.biome[src];
                        out.veg[idx] = c.veg[src];
                    }
                }
            }
            tr.Fraction(static_cast<double>(k + 1) / total);
        }
    }
    return true;
}

bool ReadSegment(const std::string& path, std::vector<chunkgen::Chunk>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0, sx = 0, sz = 0, count = 0;
    if (!Get32(f, magic) || magic != kMagic || !Get32(f, sx) || !Get32(f, sz) || !Get32(f, count)) return false;
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t len = 0;
        if (!Get32(f, len)) return false;
        std::vector<uint8_t> buf(len);
        if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(len))) return false;
        chunkgen::Chunk c;
        if (!chunkgen::Decode(buf, c)) return false;
        out.push_back(std::move(c));
    }
    return true;
}

}  // namespace segments

}  // namespace wm
