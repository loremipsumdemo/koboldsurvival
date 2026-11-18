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
// The same for a tile of this ground type and vegetation (a map that makes its far ground without chunks gives it the colors the chunks have)
void biomeGroundColor(Biome b, uint8_t veg, float h, uint8_t out[3]);

void buildChunkMesh(const Terrain& t, const ChunkData& c, int step, ChunkMesh& out,
                    const int* neighbourSteps = nullptr);

// The coarse far ground (ChunkSource::farField, farFieldWindow) as a mesh, set a little below where the ground really is, since what is near
// is drawn over it. Its triangles go into the index buffer a patch at a time, FAR_PATCH_CELLS square cells to a patch: a patch is one run of
// the buffer with the box it fills, so what is out of view or out of range can be left out of the draw, and patches next to each other in
// the buffer can be drawn together.
constexpr int FAR_PATCH_CELLS = 32;
// A window of the far ground (Renderer) is this many cells across, in patches
constexpr int FAR_WINDOW_CELLS = 1024;
struct FarPatch {
    int first = 0, count = 0;   // the run of the index buffer
    float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f, minY = 0.0f, maxY = 0.0f;
};
struct FarMesh {
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;
    std::vector<FarPatch> patches;
};
// heights and rgb are res by res corners, spacing tiles apart, the first at tile (originX, originZ). clipTo above 0 leaves out the cells that
// reach past a map that many tiles across (a window of the far ground over the edge of the world); 0 leaves none out. waterY is the height of
// the water over the ground (or of the line the ground is cut off at, over a void): ground above it is kept above it, and ground under it is
// sunk only a little. A very low number (std::numeric_limits<float>::lowest()) is for no such line. Works on any thread.
FarMesh buildFarMesh(const std::vector<float>& heights, const std::vector<uint8_t>& rgb, int res, float spacing, float originX, float originZ,
                     float heightScale, int clipTo, float waterY);
