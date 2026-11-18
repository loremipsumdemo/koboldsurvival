#include "terrain.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace {
// Knowing which tiles connect means visiting them all, so maps above this size don't track it. It only sets how long a
// path search keeps trying before it gives up.
constexpr int MOST_SIZE_WITH_AREAS = 2048;
}

Terrain::Terrain(int size) {
    open(flatChunkSource(size));
}

Terrain::Terrain(std::shared_ptr<const ChunkSource> source) {
    open(std::move(source));
}

const Terrain::FarField* Terrain::farField() const {
    if (!streamed_ || farDone_) return far_.get();
    // About a sample every 4 tiles, as a 2048 map has had, for as far as the limit lets it grow (farFieldRes): a bigger map is coarser past
    // that, 8 tiles at 8192. Past that the far ground is a window that follows the camera (Renderer::updateFarWindow), which is as fine on a
    // map of any size, and this is not asked for.
    const int FAR_FIELD_RES = farFieldRes(size_);
    if (!farJob_.valid()) {
        const std::shared_ptr<const ChunkSource> src = source_;
        farJob_ = std::async(std::launch::async, [src, FAR_FIELD_RES]() -> std::shared_ptr<FarField> {
            auto f = std::make_shared<FarField>();
            const std::atomic<bool> never{ false };
            if (!src->farField(FAR_FIELD_RES, f->heights, f->rgb, never)) return nullptr;
            f->res = FAR_FIELD_RES;
            f->spacing = (float)src->size() / (float)(FAR_FIELD_RES - 1);
            return f;
        });
        return nullptr;
    }
    if (farJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        far_ = farJob_.get();
        farDone_ = true;
    }
    return far_.get();
}

void Terrain::open(std::shared_ptr<const ChunkSource> source) {
    far_.reset();
    farDone_ = false;
    if (farJob_.valid()) farJob_.wait();
    farJob_ = {};
    nav_.reset();
    blocked_.clear();
    sightBlocks_.clear();
    decks_.clear();
    heightEdits_.clear();
    source_ = std::move(source);
    size_ = source_->size();
    cps_ = ::chunksPerSide(size_);
    streamed_ = source_->streamed();
    surface_ = source_->surface();
    surfaceVersion_ = nextVersion_++;
    heightScale_ = 1.0f;
    floorY_ = source_->floorY();
    chunks_.clear();
    chunks_.resize((size_t)cps_ * cps_);
    blockedBits_.clear();
    blockedBits_.resize((size_t)cps_ * cps_);
    blockedChunks_.clear();
    drawn_.clear();
    ground_.clear();
    ground_.resize((size_t)cps_ * cps_);
    groundHeld_.clear();
    if (!streamed_) {
        for (int cz = 0; cz < cps_; ++cz) {
            for (int cx = 0; cx < cps_; ++cx) {
                auto c = std::make_unique<ChunkData>();
                if (!source_->fill(cx, cz, *c)) *c = emptyChunk(cx, cz);
                c->version = nextVersion_++;
                chunks_[(size_t)cz * cps_ + cx] = std::move(c);
                drawn_.push_back(cz * cps_ + cx);
            }
        }
    }
    findAreas();
}

bool Terrain::openWorld(const std::string& folder, std::string& err) {
    std::shared_ptr<const ChunkSource> source = worldFolderSource(folder, err);
    if (!source) return false;
    open(std::move(source));
    return true;
}

const Terrain::GroundChunk& Terrain::groundOf(int cx, int cz) const {
    std::unique_ptr<GroundChunk>& slot = ground_[(size_t)cz * cps_ + cx];
    if (!slot) {
        if (groundHeld_.size() >= MAX_GROUND_CHUNKS) {
            size_t oldest = 0;
            for (size_t i = 1; i < groundHeld_.size(); ++i) {
                if (ground_[(size_t)groundHeld_[i]]->lastUsed < ground_[(size_t)groundHeld_[oldest]]->lastUsed) oldest = i;
            }
            ground_[(size_t)groundHeld_[oldest]].reset();
            groundHeld_[oldest] = groundHeld_.back();
            groundHeld_.pop_back();
        }
        ChunkData c;
        if (!source_->fill(cx, cz, c)) c = emptyChunk(cx, cz);
        applyEdits(cx, cz, c.heights);
        slot = std::make_unique<GroundChunk>();
        slot->heights = std::move(c.heights);
        slot->walk = std::move(c.walk);
        groundHeld_.push_back(cz * cps_ + cx);
    }
    slot->lastUsed = ++groundClock_;
    return *slot;
}

void Terrain::findAreas() {
    groundArea_.clear();
    if (size_ > MOST_SIZE_WITH_AREAS) return;
    groundArea_.assign((size_t)size_ * size_, 0);
    std::vector<int> open;
    int next = 0;
    for (size_t first = 0; first < groundArea_.size(); ++first) {
        if (groundArea_[first] || !walkable((int)(first % size_), (int)(first / size_))) continue;
        ++next;
        groundArea_[first] = next;
        open.push_back((int)first);
        while (!open.empty()) {
            const int at = open.back();
            open.pop_back();
            const int x = at % size_, z = at / size_;
            auto visit = [&](int nx, int nz) {
                if (!inBounds(nx, nz)) return;
                const size_t k = (size_t)nz * size_ + nx;
                if (!groundArea_[k] && walkable(nx, nz)) {
                    groundArea_[k] = next;
                    open.push_back((int)k);
                }
            };
            visit(x + 1, z);
            visit(x - 1, z);
            visit(x, z + 1);
            visit(x, z - 1);
        }
    }
}

std::vector<int> Terrain::areaSizes() const {
    std::vector<int> sizes;
    for (int a : groundArea_) {
        if (a <= 0) continue;
        if ((size_t)a >= sizes.size()) sizes.resize((size_t)a + 1, 0);
        ++sizes[(size_t)a];
    }
    return sizes;
}

int Terrain::areaOf(int x, int y) const {
    if (!inBounds(x, y)) return 0;
    return groundArea_.empty() ? 1 : groundArea_[(size_t)y * size_ + x];
}

const ChunkData* Terrain::chunkAt(int cx, int cz) const {
    if (cx < 0 || cz < 0 || cx >= cps_ || cz >= cps_) return nullptr;
    return chunks_[(size_t)cz * cps_ + cx].get();
}

int Terrain::stream(const std::vector<Vec2>& focuses, int radius, int maxLoads) {
    if (!streamed_ || focuses.empty()) return 0;

    std::vector<std::pair<int, int>> focusChunks;
    for (Vec2 f : focuses) {
        focusChunks.push_back({ (int)std::floor(f.x / CHUNK_TILES), (int)std::floor(f.z / CHUNK_TILES) });
    }
    auto distance = [&](int cx, int cz) {
        int best = INT_MAX;
        for (const auto& f : focusChunks) {
            best = std::min(best, std::max(std::abs(cx - f.first), std::abs(cz - f.second)));
        }
        return best;
    };

    int changed = 0;

    for (size_t i = 0; i < drawn_.size();) {
        const int id = drawn_[i];
        if (distance(id % cps_, id / cps_) > radius + 1) {
            chunks_[(size_t)id].reset();
            drawn_[i] = drawn_.back();
            drawn_.pop_back();
            ++changed;
        } else {
            ++i;
        }
    }

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
        if (slot) continue;
        auto c = std::make_unique<ChunkData>();
        if (!source_->fill(w.cx, w.cz, *c)) *c = emptyChunk(w.cx, w.cz);
        applyEdits(w.cx, w.cz, c->heights);
        c->version = nextVersion_++;
        slot = std::move(c);
        drawn_.push_back(w.cz * cps_ + w.cx);
        ++loaded;
    }
    return changed + loaded;
}

void Terrain::applyEdits(int cx, int cz, std::vector<float>& heights) const {
    if (heightEdits_.empty() || heights.size() < (size_t)CHUNK_CORNERS * CHUNK_CORNERS) return;
    const int x0 = cx * CHUNK_TILES, z0 = cz * CHUNK_TILES;
    for (const auto& [key, h] : heightEdits_) {
        const int z = (int)(key / 1000003LL), x = (int)(key % 1000003LL);
        if (x < x0 || z < z0 || x > x0 + CHUNK_TILES || z > z0 + CHUNK_TILES) continue;
        heights[(size_t)(z - z0) * CHUNK_CORNERS + (x - x0)] = h;
    }
}

// Every chunk held that has this corner (up to four, on chunk edges) takes its height again, and is drawn anew
void Terrain::refreshCorner(int x, int z) {
    const auto it = heightEdits_.find(cornerKey(x, z));
    for (int cz = (z - 1) / CHUNK_TILES; cz <= z / CHUNK_TILES; ++cz) {
        for (int cx = (x - 1) / CHUNK_TILES; cx <= x / CHUNK_TILES; ++cx) {
            if (cx < 0 || cz < 0 || cx >= cps_ || cz >= cps_) continue;
            const int lx = x - cx * CHUNK_TILES, lz = z - cz * CHUNK_TILES;
            if (lx < 0 || lz < 0 || lx > CHUNK_TILES || lz > CHUNK_TILES) continue;
            const size_t i = (size_t)lz * CHUNK_CORNERS + lx;
            const size_t id = (size_t)cz * cps_ + cx;
            float h = 0.0f;
            if (it != heightEdits_.end()) {
                h = it->second;
            } else {
                // An edit taken away: the source's own height again
                ChunkData c;
                if (!source_->fill(cx, cz, c)) c = emptyChunk(cx, cz);
                h = c.heights[i];
            }
            if (ground_[id]) ground_[id]->heights[i] = h;
            if (chunks_[id]) {
                chunks_[id]->heights[i] = h;
                chunks_[id]->version = nextVersion_++;
            }
        }
    }
}

void Terrain::reloadChunk(int cx, int cz) {
    if (cx < 0 || cz < 0 || cx >= cps_ || cz >= cps_) return;
    const size_t id = (size_t)cz * cps_ + cx;
    if (ground_[id]) {
        ground_[id].reset();
        groundHeld_.erase(std::remove(groundHeld_.begin(), groundHeld_.end(), (int)id), groundHeld_.end());
    }
    if (chunks_[id]) {
        auto c = std::make_unique<ChunkData>();
        if (!source_->fill(cx, cz, *c)) *c = emptyChunk(cx, cz);
        applyEdits(cx, cz, c->heights);
        c->version = nextVersion_++;
        chunks_[id] = std::move(c);
    }
}

void Terrain::setCornerHeight(int x, int z, float h) {
    if (x < 0 || z < 0 || x > size_ || z > size_) return;
    heightEdits_[cornerKey(x, z)] = h;
    refreshCorner(x, z);
}

void Terrain::setHeightEdits(const std::unordered_map<long long, float>& edits) {
    std::vector<long long> touched;
    for (const auto& [key, h] : heightEdits_) touched.push_back(key);
    heightEdits_ = edits;
    for (const auto& [key, h] : heightEdits_) touched.push_back(key);
    for (long long key : touched) refreshCorner((int)(key % 1000003LL), (int)(key / 1000003LL));
}

float Terrain::heightAt(float x, float z) const {
    if (size_ <= 0) return 0.0f;
    x = std::clamp(x, 0.0f, (float)size_ - 0.001f);
    z = std::clamp(z, 0.0f, (float)size_ - 0.001f);
    const int x0 = (int)x, z0 = (int)z;
    const float fx = x - x0, fz = z - z0;
    const GroundChunk& c = groundOf(x0 / CHUNK_TILES, z0 / CHUNK_TILES);
    const float* h = c.heights.data() + (size_t)(z0 % CHUNK_TILES) * CHUNK_CORNERS + x0 % CHUNK_TILES;
    const float ha = h[0];
    const float hb = h[1];
    const float hc = h[CHUNK_CORNERS];
    const float hd = h[CHUNK_CORNERS + 1];

    // The mesh splits each tile along the a-d diagonal into two planar triangles.
    // Sampling with the same split keeps units exactly on the drawn surface.
    if (fz >= fx) return ha + fx * (hd - hc) + fz * (hc - ha);
    return ha + fx * (hb - ha) + fz * (hd - hb);
}

bool Terrain::walkable(int x, int y) const {
    if (!inBounds(x, y)) return false;
    const int cx = x / CHUNK_TILES, cz = y / CHUNK_TILES, tx = x % CHUNK_TILES, tz = y % CHUNK_TILES;
    if (const BlockedBits* closed = blockedBits_[(size_t)cz * cps_ + cx].get(); closed && ((closed->rows[tz] >> tx) & 1u)) return false;
    return groundOf(cx, cz).walk[(size_t)tz * CHUNK_TILES + tx] != 0;
}

bool Terrain::tileGround(int x, int y, bool& walkable, float& height) const {
    if (!inBounds(x, y)) return false;
    const GroundChunk& c = groundOf(x / CHUNK_TILES, y / CHUNK_TILES);
    const int tx = x % CHUNK_TILES, tz = y % CHUNK_TILES;
    walkable = c.walk[(size_t)tz * CHUNK_TILES + tx] != 0;
    const float* h = c.heights.data() + (size_t)tz * CHUNK_CORNERS + tx;
    height = (h[0] + h[1] + h[CHUNK_CORNERS] + h[CHUNK_CORNERS + 1]) * 0.25f;
    return true;
}

void Terrain::setBlocked(int x, int y, bool blocked) {
    const long long key = (long long)y * 1000003LL + (long long)x;
    if (blocked) blocked_.insert(key);
    else blocked_.erase(key);
    if (inBounds(x, y)) {
        const int chunkId = (y / CHUNK_TILES) * cps_ + x / CHUNK_TILES;
        std::unique_ptr<BlockedBits>& closed = blockedBits_[(size_t)chunkId];
        if (blocked && !closed) {
            closed = std::make_unique<BlockedBits>();
            blockedChunks_.push_back(chunkId);
        }
        if (closed) {
            const uint64_t bit = uint64_t(1) << (x % CHUNK_TILES);
            if (blocked) closed->rows[y % CHUNK_TILES] |= bit;
            else closed->rows[y % CHUNK_TILES] &= ~bit;
        }
    }
    navTileChanged(x, y);
}

void Terrain::clearBlocked() {
    navStashBlocked();
    blocked_.clear();
    for (const int id : blockedChunks_) blockedBits_[(size_t)id].reset();
    blockedChunks_.clear();
    sightBlocks_.clear();
}

void Terrain::setSightBlock(int x, int y, float top) {
    const long long key = (long long)y * 1000003LL + (long long)x;
    if (top > 0.0f) sightBlocks_[key] = top;
    else sightBlocks_.erase(key);
}

void Terrain::setDeck(int x, int y, bool on) {
    const long long key = (long long)y * 1000003LL + (long long)x;
    if (on) decks_.insert(key);
    else decks_.erase(key);
}

bool Terrain::onDeck(int x, int y) const {
    return !decks_.empty() && decks_.count((long long)y * 1000003LL + (long long)x) > 0;
}

float Terrain::standHeight(float x, float z) const {
    const int tx = (int)std::floor(x), tz = (int)std::floor(z);
    if (onDeck(tx, tz)) return heightAt(tx + 0.5f, tz + 0.5f) + DECK_HEIGHT;
    return heightAt(x, z);
}

bool Terrain::blockedByBuilding(int x, int y) const {
    if (inBounds(x, y)) {
        const BlockedBits* closed = blockedBits_[(size_t)(y / CHUNK_TILES) * cps_ + x / CHUNK_TILES].get();
        return closed && ((closed->rows[y % CHUNK_TILES] >> (x % CHUNK_TILES)) & 1u) != 0;
    }
    return blocked_.count((long long)y * 1000003LL + (long long)x) > 0;
}

bool Terrain::isFree(float x, float z) const {
    const int tx = (int)std::floor(x), tz = (int)std::floor(z);
    // A closed tile is only in the way if it is nearer than the clearance, and a tile beside this one is at least as far off as its
    // side of this tile is: a side further than the clearance (with a little to spare, so the sums below still settle every close
    // call) rules out the three tiles along it. In the middle of a tile only the tile itself is asked about.
    constexpr float REACH = AGENT_CLEARANCE + 0.001f;
    if (!walkable(tx, tz)) return false;   // its own tile, which it is no distance from
    const float fx = x - (float)tx, fz = z - (float)tz;
    const int dx0 = fx < REACH ? -1 : 0, dx1 = 1.0f - fx < REACH ? 1 : 0;
    const int dz0 = fz < REACH ? -1 : 0, dz1 = 1.0f - fz < REACH ? 1 : 0;
    for (int dz = dz0; dz <= dz1; ++dz) {
        for (int dx = dx0; dx <= dx1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            const int nx = tx + dx, nz = tz + dz;
            if (walkable(nx, nz)) continue;
            const float cx = std::clamp(x, (float)nx, (float)nx + 1.0f);
            const float cz = std::clamp(z, (float)nz, (float)nz + 1.0f);
            const float ex = x - cx, ez = z - cz;
            if (ex * ex + ez * ez < AGENT_CLEARANCE * AGENT_CLEARANCE) return false;
        }
    }
    return true;
}

bool Terrain::nearestBigLand(Vec2 from, Vec2& out) const {
    if (groundArea_.empty()) return false;
    const std::vector<int> sizes = areaSizes();
    int most = 0;
    for (int s : sizes) most = std::max(most, s);
    if (most <= 0) return false;
    const int big = std::max(1, most - most / 10);
    bool found = false;
    float bestD2 = 0.0f;
    for (size_t k = 0; k < groundArea_.size(); ++k) {
        const int a = groundArea_[k];
        if (a <= 0 || (size_t)a >= sizes.size() || sizes[(size_t)a] < big) continue;
        const float x = (float)(k % size_) + 0.5f, z = (float)(k / size_) + 0.5f;
        const float d2 = (x - from.x) * (x - from.x) + (z - from.z) * (z - from.z);
        if (!found || d2 < bestD2) {
            found = true;
            bestD2 = d2;
            out = { x, z };
        }
    }
    return found;
}

Vec2 Terrain::snap(Vec2 p) const {
    if (isFree(p.x, p.z)) return p;

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
