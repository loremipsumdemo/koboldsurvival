#include "overview.h"

#include <algorithm>
#include <cmath>

#include "map_file.h"

namespace {

// The picture is at most this many pixels a side, so a large world's picture stays small in memory
constexpr int MAX_PICTURE_SIDE = 2048;

struct Colour {
    float r, g, b;
};

// The colour of a tile's ground. Sea is blue, deepening with the depth below sea level. A tile with
// no type above sea level is a plain olive green. Biome colours match the world map.
Colour TileColour(Biome biome, float height) {
    switch (biome) {
    case Biome::Grass:        return {0.47f, 0.38f, 0.27f};
    case Biome::Sand:         return {0.86f, 0.79f, 0.60f};
    case Biome::Forest:       return {0.38f, 0.33f, 0.23f};
    case Biome::Desert:       return {0.84f, 0.75f, 0.53f};
    case Biome::Snow:         return {0.89f, 0.93f, 0.96f};
    case Biome::Rock:         return {0.43f, 0.42f, 0.40f};
    case Biome::Beach:        return {0.87f, 0.81f, 0.63f};
    case Biome::Silt:         return {0.50f, 0.46f, 0.36f};
    case Biome::FrozenClay:  return {0.59f, 0.61f, 0.62f};
    case Biome::Clay:         return {0.65f, 0.45f, 0.35f};
    case Biome::Gravel:       return {0.58f, 0.55f, 0.50f};
    case Biome::AridSoil:    return {0.70f, 0.58f, 0.41f};
    case Biome::Scree:        return {0.49f, 0.47f, 0.44f};
    case Biome::Floodplain:   return {0.59f, 0.54f, 0.41f};
    case Biome::CliffRock:   return {0.36f, 0.34f, 0.33f};
    default: break;
    }
    if (biome == Biome::None && height > 0.0f) return {0.42f, 0.44f, 0.34f};
    // Sea: shallow water at the shore, deep water further out
    const float depth = std::clamp(-height / 2.5f, 0.0f, 1.0f);
    const Colour shallow{0.24f, 0.48f, 0.62f}, deep{0.05f, 0.16f, 0.30f};
    return {shallow.r + (deep.r - shallow.r) * depth,
            shallow.g + (deep.g - shallow.g) * depth,
            shallow.b + (deep.b - shallow.b) * depth};
}

}  // namespace

void BuildOverview(int size, const ChunkSource& source, OverviewPicture& out, const std::atomic<bool>& cancel) {
    out = OverviewPicture{};
    if (size <= 0) return;
    const int step = std::max(1, (size + MAX_PICTURE_SIDE - 1) / MAX_PICTURE_SIDE);
    const int px = (size + step - 1) / step;
    out.size = size;
    out.step = step;
    out.px = px;
    out.rgba.assign(static_cast<size_t>(px) * px * 4, 0);

    const int cps = (size + CHUNK_TILES - 1) / CHUNK_TILES;
    ChunkData c;
    for (int cz = 0; cz < cps; ++cz) {
        for (int cx = 0; cx < cps; ++cx) {
            if (cancel.load()) return;
            if (!source || !source(cx, cz, c)) c = emptyChunk(cx, cz);
            for (int lz = 0; lz < CHUNK_TILES; ++lz) {
                const int z = cz * CHUNK_TILES + lz;
                if (z >= size) break;
                if (z % step != 0) continue;
                const int row = (size - 1 - z) / step;
                for (int lx = 0; lx < CHUNK_TILES; ++lx) {
                    const int x = cx * CHUNK_TILES + lx;
                    if (x >= size) break;
                    if (x % step != 0) continue;
                    const int col = (size - 1 - x) / step;
                    const uint8_t biome = c.biome[static_cast<size_t>(lz) * CHUNK_TILES + lx];
                    const float height = c.heights[static_cast<size_t>(lz) * CHUNK_CORNERS + lx];
                    const Colour k = TileColour((Biome)biome, height);
                    uint8_t* p = &out.rgba[(static_cast<size_t>(row) * px + col) * 4];
                    p[0] = static_cast<uint8_t>(std::clamp(k.r, 0.0f, 1.0f) * 255.0f);
                    p[1] = static_cast<uint8_t>(std::clamp(k.g, 0.0f, 1.0f) * 255.0f);
                    p[2] = static_cast<uint8_t>(std::clamp(k.b, 0.0f, 1.0f) * 255.0f);
                    p[3] = 255;
                }
            }
        }
    }
}

void OverviewBuilder::stop() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    pending_ = false;
    done_ = false;
}

void OverviewBuilder::start(int size, ChunkSource source, bool background) {
    stop();
    pic_ = OverviewPicture{};
    pending_ = true;
    if (background) {
        thread_ = std::thread([this, size, source]() {
            BuildOverview(size, source, pic_, cancel_);
            done_ = true;
        });
    } else {
        BuildOverview(size, source, pic_, cancel_);
        done_ = true;
    }
}

bool OverviewBuilder::take(OverviewPicture& out) {
    if (!pending_ || !done_.load()) return false;
    if (thread_.joinable()) thread_.join();
    out = std::move(pic_);
    pic_ = OverviewPicture{};
    pending_ = false;
    done_ = false;
    return true;
}
