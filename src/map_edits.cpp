#include "map_edits.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include "world_gen.h"

namespace fs = std::filesystem;

namespace {
constexpr float EDIT_SLOPE_LIMIT = 0.9f;   // the most a walkable tile's corners may differ, as in the generator (900 thousandths)

MapEdits::Bucket* bucketOf(MapEdits& m, int x, int z, bool make) {
    const long long key = MapEdits::bucketKey(x / CHUNK_TILES, z / CHUNK_TILES);
    const auto it = m.buckets.find(key);
    if (it != m.buckets.end()) return &it->second;
    return make ? &m.buckets[key] : nullptr;
}
const MapEdits::Bucket* bucketOf(const MapEdits& m, int x, int z) {
    const auto it = m.buckets.find(MapEdits::bucketKey(x / CHUNK_TILES, z / CHUNK_TILES));
    return it != m.buckets.end() ? &it->second : nullptr;
}
int localIndex(int x, int z) { return (z % CHUNK_TILES) * CHUNK_TILES + x % CHUNK_TILES; }
}

bool MapEdits::cornerEdit(int x, int z, float& h) const {
    if (x < 0 || z < 0) return false;
    std::lock_guard<std::mutex> g(lock);
    const Bucket* b = bucketOf(*this, x, z);
    if (!b) return false;
    const auto it = b->corners.find(localIndex(x, z));
    if (it == b->corners.end()) return false;
    h = it->second;
    return true;
}
void MapEdits::setCorner(int x, int z, float h) {
    if (x < 0 || z < 0) return;
    std::lock_guard<std::mutex> g(lock);
    bucketOf(*this, x, z, true)->corners[localIndex(x, z)] = h;
}
void MapEdits::clearCorner(int x, int z) {
    if (x < 0 || z < 0) return;
    std::lock_guard<std::mutex> g(lock);
    if (Bucket* b = bucketOf(*this, x, z, false)) b->corners.erase(localIndex(x, z));
}
bool MapEdits::tileEdit(int x, int z, uint8_t& biome) const {
    if (x < 0 || z < 0) return false;
    std::lock_guard<std::mutex> g(lock);
    const Bucket* b = bucketOf(*this, x, z);
    if (!b) return false;
    const auto it = b->tiles.find(localIndex(x, z));
    if (it == b->tiles.end()) return false;
    biome = it->second;
    return true;
}
void MapEdits::setTile(int x, int z, uint8_t biome) {
    if (x < 0 || z < 0) return;
    std::lock_guard<std::mutex> g(lock);
    bucketOf(*this, x, z, true)->tiles[localIndex(x, z)] = biome;
}
void MapEdits::clearTile(int x, int z) {
    if (x < 0 || z < 0) return;
    std::lock_guard<std::mutex> g(lock);
    if (Bucket* b = bucketOf(*this, x, z, false)) b->tiles.erase(localIndex(x, z));
}
bool MapEdits::empty() const {
    std::lock_guard<std::mutex> g(lock);
    for (const auto& [k, b] : buckets) {
        if (!b.corners.empty() || !b.tiles.empty()) return false;
    }
    return true;
}

std::string mapEditsPath(const std::string& stem) { return std::string(MAP_FOLDER) + "/" + stem + ".edits"; }

// KMED1, then the corner count and the tile count, the corners (x, z, height) and the tiles (x, z, biome)
bool readMapEdits(const std::string& path, MapEdits& out) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) return false;
    char magic[5] = {};
    uint32_t nc = 0, nt = 0;
    in.read(magic, 5);
    in.read((char*)&nc, 4);
    in.read((char*)&nt, 4);
    if (!in || std::string(magic, 5) != "KMED1") return false;
    std::lock_guard<std::mutex> g(out.lock);
    out.buckets.clear();
    for (uint32_t i = 0; i < nc && in; ++i) {
        int32_t x = 0, z = 0;
        float h = 0.0f;
        in.read((char*)&x, 4);
        in.read((char*)&z, 4);
        in.read((char*)&h, 4);
        if (in && x >= 0 && z >= 0) bucketOf(out, x, z, true)->corners[localIndex(x, z)] = h;
    }
    for (uint32_t i = 0; i < nt && in; ++i) {
        int32_t x = 0, z = 0;
        uint8_t b = 0;
        in.read((char*)&x, 4);
        in.read((char*)&z, 4);
        in.read((char*)&b, 1);
        if (in && x >= 0 && z >= 0) bucketOf(out, x, z, true)->tiles[localIndex(x, z)] = b;
    }
    return true;
}

bool writeMapEdits(const std::string& path, const MapEdits& edits, std::string& err) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    if (edits.empty()) {
        fs::remove(fs::u8path(path), ec);
        return true;
    }
    std::lock_guard<std::mutex> g(edits.lock);
    uint32_t nc = 0, nt = 0;
    for (const auto& [k, b] : edits.buckets) {
        nc += (uint32_t)b.corners.size();
        nt += (uint32_t)b.tiles.size();
    }
    std::ofstream out(fs::u8path(path), std::ios::binary);
    out.write("KMED1", 5);
    out.write((const char*)&nc, 4);
    out.write((const char*)&nt, 4);
    for (const auto& [k, b] : edits.buckets) {
        const int bx = (int)(k % 100003LL), bz = (int)(k / 100003LL);
        for (const auto& [i, h] : b.corners) {
            const int32_t x = bx * CHUNK_TILES + i % CHUNK_TILES, z = bz * CHUNK_TILES + i / CHUNK_TILES;
            out.write((const char*)&x, 4);
            out.write((const char*)&z, 4);
            out.write((const char*)&h, 4);
        }
    }
    for (const auto& [k, b] : edits.buckets) {
        const int bx = (int)(k % 100003LL), bz = (int)(k / 100003LL);
        for (const auto& [i, v] : b.tiles) {
            const int32_t x = bx * CHUNK_TILES + i % CHUNK_TILES, z = bz * CHUNK_TILES + i / CHUNK_TILES;
            out.write((const char*)&x, 4);
            out.write((const char*)&z, 4);
            out.write((const char*)&v, 1);
        }
    }
    out.close();
    if (!out) {
        err = "could not write " + path;
        return false;
    }
    return true;
}

namespace {
class EditedSource : public ChunkSource {
public:
    EditedSource(std::shared_ptr<const ChunkSource> inner, std::shared_ptr<MapEdits> edits) : inner_(std::move(inner)), edits_(std::move(edits)) {}
    int size() const override { return inner_->size(); }
    float floorY() const override { return inner_->floorY(); }
    const WorldSurface& surface() const override { return inner_->surface(); }
    bool streamed() const override { return inner_->streamed(); }
    const MapTheme* theme() const override { return inner_->theme(); }
    bool walledIn() const override { return inner_->walledIn(); }
    bool fill(int cx, int cz, ChunkData& out) const override {
        if (!baseFill(cx, cz, out)) return false;
        std::lock_guard<std::mutex> g(edits_->lock);
        bool touched = false;
        std::vector<char> dirty((size_t)CHUNK_TILES * CHUNK_TILES, 0);
        // The chunk's corners belong to this chunk's bucket and, along its far edges, to the next one's
        for (int bz = cz; bz <= cz + 1; ++bz) {
            for (int bx = cx; bx <= cx + 1; ++bx) {
                const auto it = edits_->buckets.find(MapEdits::bucketKey(bx, bz));
                if (it == edits_->buckets.end()) continue;
                for (const auto& [i, h] : it->second.corners) {
                    const int lx = i % CHUNK_TILES + (bx - cx) * CHUNK_TILES, lz = i / CHUNK_TILES + (bz - cz) * CHUNK_TILES;
                    if (lx > CHUNK_TILES || lz > CHUNK_TILES) continue;
                    out.heights[(size_t)lz * CHUNK_CORNERS + lx] = h;
                    touched = true;
                    for (int dz = -1; dz <= 0; ++dz) {
                        for (int dx = -1; dx <= 0; ++dx) {
                            const int tx = lx + dx, tz = lz + dz;
                            if (tx >= 0 && tz >= 0 && tx < CHUNK_TILES && tz < CHUNK_TILES) dirty[(size_t)tz * CHUNK_TILES + tx] = 1;
                        }
                    }
                }
            }
        }
        const auto own = edits_->buckets.find(MapEdits::bucketKey(cx, cz));
        if (own != edits_->buckets.end()) {
            for (const auto& [i, b] : own->second.tiles) {
                out.biome[(size_t)i] = b;
                out.veg[(size_t)i] = 0;
                touched = true;
            }
        }
        if (!touched) return true;
        const int x0 = cx * CHUNK_TILES, z0 = cz * CHUNK_TILES, size = inner_->size();
        for (int j = 0; j < CHUNK_TILES; ++j) {
            for (int i = 0; i < CHUNK_TILES; ++i) {
                const size_t t = (size_t)j * CHUNK_TILES + i;
                if (!dirty[t]) continue;
                const float* c = &out.heights[(size_t)j * CHUNK_CORNERS + i];
                const float a = c[0], b = c[1], d = c[CHUNK_CORNERS], e = c[CHUNK_CORNERS + 1];
                const float low = std::min({ a, b, d, e }), high = std::max({ a, b, d, e });
                out.walk[t] = x0 + i < size && z0 + j < size && low > 0.0f && high - low <= EDIT_SLOPE_LIMIT ? 1 : 0;
            }
        }
        return true;
    }
private:
    // The unedited chunk, kept for the last few made: a brush makes the same chunks again every stroke, and making one from the recipe is slow
    bool baseFill(int cx, int cz, ChunkData& out) const {
        const long long key = (long long)cz * 100003LL + cx;
        {
            std::lock_guard<std::mutex> g(cacheLock_);
            const auto it = cache_.find(key);
            if (it != cache_.end()) {
                out = it->second;
                return true;
            }
        }
        if (!inner_->fill(cx, cz, out)) return false;
        std::lock_guard<std::mutex> g(cacheLock_);
        if (cache_.size() >= 96) {
            cache_.erase(order_.front());
            order_.pop_front();
        }
        if (cache_.emplace(key, out).second) order_.push_back(key);
        return true;
    }
    mutable std::mutex cacheLock_;
    mutable std::unordered_map<long long, ChunkData> cache_;
    mutable std::deque<long long> order_;
    std::shared_ptr<const ChunkSource> inner_;
    std::shared_ptr<MapEdits> edits_;
};
}

std::shared_ptr<const ChunkSource> editedSource(std::shared_ptr<const ChunkSource> inner, std::shared_ptr<MapEdits> edits) {
    return std::make_shared<EditedSource>(std::move(inner), std::move(edits));
}
