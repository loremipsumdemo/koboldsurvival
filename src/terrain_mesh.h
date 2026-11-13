#pragma once
#include <cstdint>
#include <vector>
#include "chunk.h"
#include "terrain.h"

struct TerrainVertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b;
};

struct ChunkMesh {
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;
};

struct ChunkBounds {
    float x0, z0, x1, z1;
    float minY, maxY;
};

ChunkBounds chunkBounds(const Terrain& t, const ChunkData& c);
// The color the chunk's mesh gives the tile at (tx, tz) in it, at height h, as bytes
void chunkTileColor(const ChunkData& c, int tx, int tz, float h, uint8_t out[3]);

void buildChunkMesh(const Terrain& t, const ChunkData& c, int step, ChunkMesh& out,
                    const int* neighbourSteps = nullptr);
