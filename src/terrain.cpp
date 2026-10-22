#include "terrain.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace {

// Copies one chunk out of a whole map. Tiles and corners past the map's edge are left empty.
ChunkData chunkFromMap(const MapData& m, int cx, int cz) {
    ChunkData c = emptyChunk(cx, cz);
    const int size = m.size, vs = size + 1;
    for (int j = 0; j < CHUNK_CORNERS; ++j) {
        const int gz = std::min(cz * CHUNK_TILES + j, size);
        for (int i = 0; i < CHUNK_CORNERS; ++i) {
            const int gx = std::min(cx * CHUNK_TILES + i, size);
            const size_t k = (size_t)gz * vs + gx;
            c.heights[(size_t)j * CHUNK_CORNERS + i] = k < m.heights.size() ? m.heights[k] : 0.0f;
        }
    }
    for (int j = 0; j < CHUNK_TILES; ++j) {
        const int tz = cz * CHUNK_TILES + j;
        for (int i = 0; i < CHUNK_TILES; ++i) {
            const int tx = cx * CHUNK_TILES + i;
            if (tx >= size || tz >= size) continue;
            const size_t k = (size_t)tz * size + tx;
            const size_t t = (size_t)j * CHUNK_TILES + i;
            c.walk[t] = k < m.walkable.size() ? (m.walkable[k] ? 1 : 0) : 1;
            c.biome[t] = k < m.biome.size() ? m.biome[k] : BIOME_NONE;
        }
    }
    return c;
}

} // namespace

Terrain::Terrain(int size) {
    load(flatMap(size));
}

void Terrain::load(const MapData& m) {
    size_ = m.size;
    cps_ = ::chunksPerSide(size_);
    streamed_ = false;
    folder_.clear();
    chunks_.clear();
    chunks_.resize((size_t)cps_ * cps_);
    for (int cz = 0; cz < cps_; ++cz) {
        for (int cx = 0; cx < cps_; ++cx) {
            auto c = std::make_unique<ChunkData>(chunkFromMap(m, cx, cz));
            c->version = nextVersion_++;
            chunks_[(size_t)cz * cps_ + cx] = std::move(c);
        }
    }
}

bool Terrain::openWorld(const std::string& folder, std::string& err) {
    WorldInfo info;
    if (!readWorldInfo(folder, info, err)) return false;
    size_ = info.size;
    cps_ = ::chunksPerSide(size_);
    chunks_.clear();
    chunks_.resize((size_t)cps_ * cps_);
    streamed_ = true;
    folder_ = folder;
    info_ = info;
    return true;
}

const ChunkData* Terrain::chunkAt(int cx, int cz) const {
    if (cx < 0 || cz < 0 || cx >= cps_ || cz >= cps_) return nullptr;
    return chunks_[(size_t)cz * cps_ + cx].get();
}

const ChunkData* Terrain::tileChunk(int x, int y) const {
    if (!inBounds(x, y)) return nullptr;
    return chunkAt(x / CHUNK_TILES, y / CHUNK_TILES);
}

int Terrain::loadedChunks() const {
    int n = 0;
    for (const auto& c : chunks_) if (c) ++n;
    return n;
}

int Terrain::stream(const std::vector<Vec2>& focuses, int radius, int maxLoads) {
    if (!streamed_ || focuses.empty()) return 0;

    // Each focus as a chunk coordinate
    std::vector<std::pair<int, int>> focusChunks;
    for (Vec2 f : focuses) {
        focusChunks.push_back({ (int)std::floor(f.x / CHUNK_TILES), (int)std::floor(f.z / CHUNK_TILES) });
    }
    // Distance in chunks from (cx, cz) to the nearest focus, measured as a square
    auto distance = [&](int cx, int cz) {
        int best = INT_MAX;
        for (const auto& f : focusChunks) {
            best = std::min(best, std::max(std::abs(cx - f.first), std::abs(cz - f.second)));
        }
        return best;
    };

    int changed = 0;

    // Unload chunks that have fallen well outside the area
    for (int cz = 0; cz < cps_; ++cz) {
        for (int cx = 0; cx < cps_; ++cx) {
            auto& slot = chunks_[(size_t)cz * cps_ + cx];
            if (slot && distance(cx, cz) > radius + 1) {
                slot.reset();
                ++changed;
            }
        }
    }

    // Chunks to load: every unloaded chunk within radius of a focus, nearest first
    struct Want { int cx, cz, d; };
    std::vector<Want> want;
    for (const auto& f : focusChunks) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int cx = f.first + dx, cz = f.second + dz;
                if (cx < 0 || cz < 0 || cx >= cps_ || cz >= cps_) continue;
                if (chunks_[(size_t)cz * cps_ + cx]) continue;
                want.push_back({ cx, cz, std::max(std::abs(dx), std::abs(dz)) });
            }
        }
    }
    std::stable_sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.d < b.d; });

    int loaded = 0;
    for (const Want& w : want) {
        if (maxLoads > 0 && loaded >= maxLoads) break;
        auto& slot = chunks_[(size_t)w.cz * cps_ + w.cx];
        if (slot) continue;   // the same chunk can be wanted by two focuses
        auto c = std::make_unique<ChunkData>();
        // A chunk missing from the file is empty sea
        if (!readChunk(folder_, info_, w.cx, w.cz, *c)) *c = emptyChunk(w.cx, w.cz);
        c->version = nextVersion_++;
        slot = std::move(c);
        ++loaded;
    }
    return changed + loaded;
}

float Terrain::heightAt(float x, float z) const {
    if (size_ <= 0) return 0.0f;
    x = std::clamp(x, 0.0f, (float)size_ - 0.001f);
    z = std::clamp(z, 0.0f, (float)size_ - 0.001f);
    const int x0 = (int)x, z0 = (int)z;
    const float fx = x - x0, fz = z - z0;
    const ChunkData* c = tileChunk(x0, z0);
    if (!c) return 0.0f;

    const int lx = x0 % CHUNK_TILES, lz = z0 % CHUNK_TILES;
    auto h = [&](int i, int j) { return c->heights[(size_t)j * CHUNK_CORNERS + i]; };
    const float ha = h(lx, lz);          // corner a: (0,0)
    const float hb = h(lx + 1, lz);      // corner b: (1,0)
    const float hc = h(lx, lz + 1);      // corner c: (0,1)
    const float hd = h(lx + 1, lz + 1);  // corner d: (1,1)

    // The mesh splits each tile along the a-d diagonal into two planar triangles.
    // Sampling with the same split keeps units exactly on the drawn surface.
    if (fz >= fx) return ha + fx * (hd - hc) + fz * (hc - ha);   // triangle a, c, d
    return ha + fx * (hb - ha) + fz * (hd - hb);                 // triangle a, d, b
}

bool Terrain::walkable(int x, int y) const {
    const ChunkData* c = tileChunk(x, y);
    if (!c) return false;
    return c->walk[(size_t)(y % CHUNK_TILES) * CHUNK_TILES + x % CHUNK_TILES] != 0;
}

bool Terrain::tileGround(int x, int y, bool& walkable, float& height) const {
    if (!inBounds(x, y)) return false;
    const int cx = x / CHUNK_TILES, cz = y / CHUNK_TILES;
    const int lx = x % CHUNK_TILES, lz = y % CHUNK_TILES;

    ChunkData fromFolder;
    const ChunkData* c = chunkAt(cx, cz);
    if (!c) {
        if (!streamed_ || !readChunk(folder_, info_, cx, cz, fromFolder)) return false;
        c = &fromFolder;
    }
    walkable = c->walk[(size_t)lz * CHUNK_TILES + lx] != 0;

    // Average of the tile's four corners: the height at its centre
    const size_t i = (size_t)lz * CHUNK_CORNERS + lx;
    const float* h = c->heights.data();
    height = (h[i] + h[i + 1] + h[i + CHUNK_CORNERS] + h[i + CHUNK_CORNERS + 1]) * 0.25f;
    return true;
}

uint8_t Terrain::biome(int x, int y) const {
    const ChunkData* c = tileChunk(x, y);
    if (!c) return BIOME_NONE;
    return c->biome[(size_t)(y % CHUNK_TILES) * CHUNK_TILES + x % CHUNK_TILES];
}

bool Terrain::isFree(float x, float z) const {
    // The point must be on walkable ground, and no blocked tile may be within the clearance
    const int tx = (int)std::floor(x), tz = (int)std::floor(z);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = tx + dx, nz = tz + dz;
            if (walkable(nx, nz)) continue;
            // Distance from the point to the blocked tile's square
            const float cx = std::clamp(x, (float)nx, (float)nx + 1.0f);
            const float cz = std::clamp(z, (float)nz, (float)nz + 1.0f);
            const float ex = x - cx, ez = z - cz;
            if (ex * ex + ez * ez < AGENT_CLEARANCE * AGENT_CLEARANCE) return false;
        }
    }
    return walkable(tx, tz);
}

Vec2 Terrain::snap(Vec2 p) const {
    if (isFree(p.x, p.z)) return p;

    // Search outward in square rings, a quarter tile apart, up to 8 tiles. The nearest free point
    // wins. Once a ring has a free point, rings up to sqrt(2) times as far out can still hold a nearer one.
    constexpr float STEP = 0.25f;
    constexpr int RINGS = 32;
    bool found = false;
    float bestD2 = 0.0f;
    Vec2 best = p;
    int last = RINGS;
    for (int r = 1; r <= last; ++r) {
        for (int j = -r; j <= r; ++j) {
            for (int i = -r; i <= r; ++i) {
                if (std::max(std::abs(i), std::abs(j)) != r) continue;
                const float x = p.x + i * STEP, z = p.z + j * STEP;
                if (!isFree(x, z)) continue;
                const float d2 = (float)(i * i + j * j) * STEP * STEP;
                if (!found || d2 < bestD2) {
                    found = true;
                    bestD2 = d2;
                    best = { x, z };
                }
            }
        }
        if (found && last == RINGS) last = std::min(RINGS, (int)std::ceil(r * 1.4143f));
    }
    return found ? best : p;
}
