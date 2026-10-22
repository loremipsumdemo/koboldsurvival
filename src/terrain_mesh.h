#pragma once
#include <cstdint>
#include <vector>
#include "terrain.h"

// The terrain is drawn in square chunks. Each chunk can be built at several levels of detail:
// step 1 uses every tile corner, step 2 every second one, step 4 every fourth one. This file
// has no OpenGL in it, so the meshes can be checked without a window.

constexpr int CHUNK_TILES = 64;        // tiles per chunk side

struct TerrainVertex {
    float x, y, z;
    float r, g, b, a;   // colour, with the lighting already applied
};

struct ChunkMesh {
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;          // triangles
};

// Where a chunk is, in tiles and world units. minY and maxY include the skirts.
struct ChunkBounds {
    float x0, z0, x1, z1;
    float minY, maxY;
};

int chunksPerSide(int size);
ChunkBounds chunkBounds(const Terrain& t, int cx, int cz);

// Builds chunk (cx, cz) at a detail step. Vertical skirts hang from the edges that border
// another chunk, so a coarse chunk and a fine one next to it leave no cracks. Map edges get none.
void buildChunkMesh(const Terrain& t, int cx, int cz, int step, ChunkMesh& out);
