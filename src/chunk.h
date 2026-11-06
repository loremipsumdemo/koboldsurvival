#pragma once

#include <cstdint>
#include <vector>

#include "map_file.h"

constexpr int CHUNK_TILES = 64;
constexpr int CHUNK_CORNERS = CHUNK_TILES + 1;
constexpr int REGION_CHUNKS = 16;

struct ChunkData {
    int cx = 0, cz = 0;
    std::vector<float> heights;
    std::vector<uint8_t> biome;
    std::vector<uint8_t> veg;
    std::vector<uint8_t> walk;
    uint32_t version = 0;
};

inline ChunkData emptyChunk(int cx, int cz) {
    ChunkData c;
    c.cx = cx;
    c.cz = cz;
    c.heights.assign((size_t)CHUNK_CORNERS * CHUNK_CORNERS, 0.0f);
    c.biome.assign((size_t)CHUNK_TILES * CHUNK_TILES, (uint8_t)Biome::None);
    c.veg.assign((size_t)CHUNK_TILES * CHUNK_TILES, 0);
    c.walk.assign((size_t)CHUNK_TILES * CHUNK_TILES, 0);
    return c;
}
