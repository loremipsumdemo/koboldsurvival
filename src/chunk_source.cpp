#include "chunk_source.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include "surface_tiles.h"
#include "terrain_mesh.h"

namespace {
class FlatSource : public ChunkSource {
public:
    explicit FlatSource(int size) : size_(size) {
        surface_.ground = surfaceTilePixels(GROUND_TILE_SPEC);
        surface_.water = surfaceTilePixels(WATER_TILE_SPEC);
    }
    int size() const override { return size_; }
    float floorY() const override { return -2.0f; }
    const WorldSurface& surface() const override { return surface_; }
    bool streamed() const override { return false; }
    bool fill(int cx, int cz, ChunkData& out) const override {
        out = emptyChunk(cx, cz);
        const int across = std::clamp(size_ - cx * CHUNK_TILES, 0, CHUNK_TILES);
        const int down = std::clamp(size_ - cz * CHUNK_TILES, 0, CHUNK_TILES);
        for (int j = 0; j < down; ++j) std::fill_n(out.walk.begin() + (size_t)j * CHUNK_TILES, across, (uint8_t)1);
        return true;
    }

private:
    int size_;
    WorldSurface surface_;
};

class WorldFolderSource : public ChunkSource {
public:
    WorldFolderSource(std::string folder, const WorldInfo& info, WorldSurface surface)
        : folder_(std::move(folder)), info_(info), surface_(std::move(surface)) {}
    int size() const override { return info_.size; }
    float floorY() const override { return info_.floorY / info_.heightScale; }
    const WorldSurface& surface() const override { return surface_; }
    bool streamed() const override { return true; }
    bool fill(int cx, int cz, ChunkData& out) const override { return readChunk(folder_, info_, cx, cz, out); }

private:
    std::string folder_;
    WorldInfo info_;
    WorldSurface surface_;
};
}

bool ChunkSource::farField(int res, std::vector<float>& heights, std::vector<uint8_t>& rgb, const std::atomic<bool>& cancel) const {
    const int n = size();
    if (res < 2 || n < 1) return false;
    const int cps = chunksPerSide(n);
    heights.assign((size_t)res * res, 0.0f);
    rgb.assign((size_t)res * res * 3, 0);
    // Which samples fall in each chunk, along an axis
    std::vector<std::vector<int>> samples((size_t)cps);
    for (int i = 0; i < res; ++i) {
        const int at = (int)((int64_t)i * n / (res - 1));
        samples[(size_t)std::min(at / CHUNK_TILES, cps - 1)].push_back(i);
    }
    std::atomic<int> nextRow{ 0 };
    const auto work = [&]() {
        for (int cz = nextRow++; cz < cps && !cancel.load(); cz = nextRow++) {
            if (samples[(size_t)cz].empty()) continue;
            for (int cx = 0; cx < cps; ++cx) {
                if (samples[(size_t)cx].empty()) continue;
                ChunkData c;
                if (!fill(cx, cz, c) || c.heights.size() < (size_t)CHUNK_CORNERS * CHUNK_CORNERS) continue;
                for (const int j : samples[(size_t)cz]) {
                    const int lz = std::min((int)((int64_t)j * n / (res - 1)) - cz * CHUNK_TILES, CHUNK_TILES);
                    for (const int i : samples[(size_t)cx]) {
                        const int lx = std::min((int)((int64_t)i * n / (res - 1)) - cx * CHUNK_TILES, CHUNK_TILES);
                        const size_t k = (size_t)j * res + i;
                        const float h = c.heights[(size_t)lz * CHUNK_CORNERS + lx];
                        heights[k] = h;
                        chunkTileColor(c, lx, lz, h, &rgb[k * 3]);
                    }
                }
            }
        }
    };
    const int helpers = std::clamp((int)std::thread::hardware_concurrency() - 2, 0, 7);
    std::vector<std::thread> threads;
    for (int i = 0; i < helpers; ++i) threads.emplace_back(work);
    work();
    for (std::thread& t : threads) t.join();
    return !cancel.load();
}

std::shared_ptr<const ChunkSource> flatChunkSource(int size) {
    return std::make_shared<FlatSource>(size);
}

std::shared_ptr<const ChunkSource> worldFolderSource(const std::string& folder, std::string& err) {
    WorldInfo info;
    if (!readWorldInfo(folder, info, err)) return nullptr;
    WorldSurface surface;
    if (!readWorldSurface(folder, surface, err)) return nullptr;
    return std::make_shared<WorldFolderSource>(folder, info, std::move(surface));
}
