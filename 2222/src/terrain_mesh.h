#pragma once
#include <cstdint>
#include <vector>
#include "chunk.h"
#include "terrain.h"

// The terrain is drawn in square chunks. Each chunk can be built at several levels of detail:
// step 1 uses every corner, step 2 every second one, step 4 every fourth one. This file has no
// OpenGL in it, so the meshes can be checked without a window.

struct TerrainVertex {
    float x, y, z;      // position
    float nx, ny, nz;   // normal, in the world's units, so slopes shade and turn to rock the same way in every world
    float r, g, b;      // ground color, with vegetation, before lighting
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

ChunkBounds chunkBounds(const Terrain& t, const ChunkData& c);

// Builds a chunk at a detail step. Vertical skirts hang from every edge: short ones where the edge borders
// another chunk, so a coarse chunk and a fine one next to it leave no cracks, and walls down to the seabed
// on the map's border.
void buildChunkMesh(const Terrain& t, const ChunkData& c, int step, ChunkMesh& out);
