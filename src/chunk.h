#pragma once

#include <cstdint>
#include <vector>

#include "map.h"

// The world is split into square chunks of this many tiles per side. Chunks are the unit of
// loading, drawing and streaming.
constexpr int CHUNK_TILES = 64;
constexpr int CHUNK_CORNERS = CHUNK_TILES + 1;
// Chunks are stored in region files of this many chunks per side (1024 tiles)
constexpr int REGION_CHUNKS = 16;

// One chunk of the world. Its corner heights include the far edges, so a chunk can be drawn
// and walked on without its neighbours.
struct ChunkData {
    int cx = 0, cz = 0;                  // chunk coordinates
    std::vector<float> heights;          // CHUNK_CORNERS x CHUNK_CORNERS, row by row, world units
    std::vector<uint8_t> biome;          // CHUNK_TILES x CHUNK_TILES ground types, BIOME_NONE for none
    std::vector<uint8_t> walk;           // CHUNK_TILES x CHUNK_TILES, 1 walkable, 0 blocked
    uint32_t version = 0;                // changes whenever the chunk is loaded, so drawn copies can be checked
};

// An empty chunk: flat sea level, no ground types, nothing walkable
inline ChunkData emptyChunk(int cx, int cz) {
    ChunkData c;
    c.cx = cx;
    c.cz = cz;
    c.heights.assign((size_t)CHUNK_CORNERS * CHUNK_CORNERS, 0.0f);
    c.biome.assign((size_t)CHUNK_TILES * CHUNK_TILES, BIOME_NONE);
    c.walk.assign((size_t)CHUNK_TILES * CHUNK_TILES, 0);
    return c;
}
