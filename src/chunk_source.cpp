#include "chunk_source.h"
#include <algorithm>
#include "surface_tiles.h"

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
