#include "terrain_mesh.h"
#include <algorithm>
#include <cmath>

namespace {

// How far a skirt hangs below its edge, plus a little for each step of detail
constexpr float SKIRT_BASE = 8.0f;

// Vegetation pulls the ground toward this canopy color, by up to kCanopyShare at full density.
// These are the world maker's values, so the vegetation reads the same as in a generated world.
constexpr float kCanopyR = 0.20f, kCanopyG = 0.34f, kCanopyB = 0.13f;
constexpr float kCanopyShare = 0.45f;

struct Rgb { float r, g, b; };

// Color for a tile with no ground type: sandy below sea level, green, then gray on the high ground
Rgb bandColor(float h) {
    if (h < -1.33f) return { 0.86f, 0.76f, 0.46f };
    if (h < 1.33f) return { 0.27f, 0.55f, 0.24f };
    return { 0.27f, 0.27f, 0.29f };
}

// Color of a ground type, as the world maker draws it.
// Water and shelf are not stored in worlds; an unknown type gets the default, seabed sand.
Rgb biomeColor(uint8_t b) {
    switch (b) {
        case BIOME_SAND:        return { 0.69f, 0.64f, 0.47f };   // seabed sand
        case BIOME_GRASS:       return { 0.47f, 0.38f, 0.27f };   // loam, under grass
        case BIOME_FOREST:      return { 0.38f, 0.33f, 0.23f };   // forest floor
        case BIOME_DESERT:      return { 0.84f, 0.75f, 0.53f };   // desert sand
        case BIOME_ROCK:        return { 0.43f, 0.42f, 0.40f };   // bedrock
        case BIOME_SNOW:        return { 0.89f, 0.93f, 0.96f };   // ice
        case BIOME_BEACH:       return { 0.87f, 0.81f, 0.63f };
        case BIOME_SILT:        return { 0.36f, 0.34f, 0.31f };
        case BIOME_FROZEN_CLAY: return { 0.59f, 0.61f, 0.62f };
        case BIOME_CLAY:        return { 0.65f, 0.45f, 0.35f };
        case BIOME_GRAVEL:      return { 0.58f, 0.55f, 0.50f };
        case BIOME_ARID_SOIL:   return { 0.70f, 0.58f, 0.41f };
        case BIOME_SCREE:       return { 0.49f, 0.47f, 0.44f };
        case BIOME_FLOODPLAIN:  return { 0.59f, 0.54f, 0.41f };
        case BIOME_CLIFF_ROCK:  return { 0.36f, 0.34f, 0.33f };
        default:                return { 0.69f, 0.64f, 0.47f };
    }
}

// Tiles of the chunk that lie inside the level, across and down
int tilesAcross(const Terrain& t, const ChunkData& c) {
    return std::clamp(t.size() - c.cx * CHUNK_TILES, 0, CHUNK_TILES);
}
int tilesDown(const Terrain& t, const ChunkData& c) {
    return std::clamp(t.size() - c.cz * CHUNK_TILES, 0, CHUNK_TILES);
}

// Height of a corner at a position in the level. A corner just outside this chunk is read from
// the neighboring chunk when that is loaded, so shading matches across the border. Otherwise the
// nearest corner of this chunk is used.
float cornerHeight(const Terrain& t, const ChunkData& c, int gx, int gz) {
    int i = gx - c.cx * CHUNK_TILES, j = gz - c.cz * CHUNK_TILES;
    if (i >= 0 && i <= CHUNK_TILES && j >= 0 && j <= CHUNK_TILES) {
        return c.heights[(size_t)j * CHUNK_CORNERS + i];
    }
    const int ncx = c.cx + (i < 0 ? -1 : (i > CHUNK_TILES ? 1 : 0));
    const int ncz = c.cz + (j < 0 ? -1 : (j > CHUNK_TILES ? 1 : 0));
    if (const ChunkData* n = t.chunkAt(ncx, ncz)) {
        const int li = gx - ncx * CHUNK_TILES, lj = gz - ncz * CHUNK_TILES;
        if (li >= 0 && li <= CHUNK_TILES && lj >= 0 && lj <= CHUNK_TILES) {
            return n->heights[(size_t)lj * CHUNK_CORNERS + li];
        }
    }
    i = std::clamp(i, 0, CHUNK_TILES);
    j = std::clamp(j, 0, CHUNK_TILES);
    return c.heights[(size_t)j * CHUNK_CORNERS + i];
}

} // namespace

ChunkBounds chunkBounds(const Terrain& t, const ChunkData& c) {
    const int a = tilesAcross(t, c), b = tilesDown(t, c);
    float lo = 0.0f, hi = 0.0f;
    bool first = true;
    for (int j = 0; j <= b; ++j) {
        for (int i = 0; i <= a; ++i) {
            const float h = c.heights[(size_t)j * CHUNK_CORNERS + i];
            if (first) { lo = hi = h; first = false; }
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    // The skirts reach down to the lowest step's depth at most, or to the seabed on the map's border
    const float margin = SKIRT_BASE + 2.0f * 4.0f;
    const float x0 = (float)(c.cx * CHUNK_TILES), z0 = (float)(c.cz * CHUNK_TILES);
    const float low = std::min(lo - margin, t.floorY());
    return { x0, z0, x0 + a, z0 + b, low, hi + 1.0f };
}

// A corner on this chunk's edge, at position v along it, when the neighbor across that edge is drawn
// coarser (every S corners, and at the far end). The coarse edge runs straight between its samples, so
// the corner is moved onto that line. Otherwise the two edges would not meet and would leave cracks.
// alongZ: the edge runs along z at x = fixed; otherwise along x at z = fixed. len: the edge's length.
float onCoarseEdge(const ChunkData& c, bool alongZ, int fixed, int v, int S, int len, float h) {
    if (v % S == 0 || v >= len) return h;
    const int v0 = (v / S) * S;
    const int v1 = std::min(v0 + S, len);
    auto at = [&](int k) {
        return alongZ ? c.heights[(size_t)k * CHUNK_CORNERS + fixed] : c.heights[(size_t)fixed * CHUNK_CORNERS + k];
    };
    const float f = (float)(v - v0) / (float)(v1 - v0);
    return at(v0) + f * (at(v1) - at(v0));
}

void buildChunkMesh(const Terrain& t, const ChunkData& c, int step, ChunkMesh& out, const int* nb) {
    out.verts.clear();
    out.idx.clear();
    if (step < 1) step = 1;

    const int a = tilesAcross(t, c), b = tilesDown(t, c);
    const int x0 = c.cx * CHUNK_TILES, z0 = c.cz * CHUNK_TILES;
    const float hs = t.heightScale();

    // Sample positions along each axis, in chunk corners: every step tiles, plus the far edge
    auto axis = [&](int len) {
        std::vector<int> s;
        for (int v = 0; v < len; v += step) s.push_back(v);
        s.push_back(len);
        return s;
    };
    const std::vector<int> xs = axis(a), zs = axis(b);
    const int nx = (int)xs.size(), nz = (int)zs.size();

    out.verts.resize((size_t)nx * nz);
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int x = xs[(size_t)i], z = zs[(size_t)j];
            const int gx = x0 + x, gz = z0 + z;

            // Slope across two steps either way, over four steps of ground, as the world maker takes it. Heights
            // are in world units, so no scale is needed.
            const float dhdx = (cornerHeight(t, c, gx + 2 * step, gz) - cornerHeight(t, c, gx - 2 * step, gz))
                             / (4.0f * step) / hs;
            const float dhdz = (cornerHeight(t, c, gx, gz + 2 * step) - cornerHeight(t, c, gx, gz - 2 * step))
                             / (4.0f * step) / hs;
            const float nl = std::sqrt(dhdx * dhdx + 1.0f + dhdz * dhdz);
            const float nxv = -dhdx / nl, nyv = 1.0f / nl, nzv = -dhdz / nl;

            float hh = c.heights[(size_t)z * CHUNK_CORNERS + x];
            // Edge corners next to a coarser neighbor move onto that neighbor's edge
            if (nb) {
                if (x == 0 && nb[0] > step) hh = onCoarseEdge(c, true, 0, z, nb[0], b, hh);
                if (x == a && nb[1] > step) hh = onCoarseEdge(c, true, a, z, nb[1], b, hh);
                if (z == 0 && nb[2] > step) hh = onCoarseEdge(c, false, 0, x, nb[2], a, hh);
                if (z == b && nb[3] > step) hh = onCoarseEdge(c, false, b, x, nb[3], a, hh);
            }
            const int tx = std::min(x, std::max(a - 1, 0)), tz = std::min(z, std::max(b - 1, 0));
            const size_t ti = (size_t)tz * CHUNK_TILES + tx;
            const uint8_t bt = c.biome[ti];
            Rgb col = (bt == BIOME_NONE) ? bandColor(hh) : biomeColor(bt);
            if (c.veg.size() == (size_t)CHUNK_TILES * CHUNK_TILES) {
                const float v = c.veg[ti] / 255.0f * kCanopyShare;
                col = { col.r + (kCanopyR - col.r) * v, col.g + (kCanopyG - col.g) * v, col.b + (kCanopyB - col.b) * v };
            }
            out.verts[(size_t)j * nx + i] = { (float)gx, hh, (float)gz, nxv, nyv, nzv, col.r, col.g, col.b };
        }
    }

    // Two triangles per cell, split along a-d to match Terrain::heightAt
    for (int j = 0; j < nz - 1; ++j) {
        for (int i = 0; i < nx - 1; ++i) {
            const uint32_t p = (uint32_t)(j * nx + i), q = p + 1;
            const uint32_t r = p + (uint32_t)nx, s = r + 1;
            out.idx.insert(out.idx.end(), { p, r, s, p, s, q });
        }
    }

    // Skirts on every edge. An edge that borders another chunk hangs a short way; an edge on the map's
    // border hangs down to the seabed, so the map has a wall at its edge.
    const float drop = SKIRT_BASE + 2.0f * (float)step;
    const float floorY = t.floorY();
    auto skirt = [&](const std::vector<uint32_t>& edge, bool border) {
        std::vector<uint32_t> low(edge.size());
        for (size_t k = 0; k < edge.size(); ++k) {
            TerrainVertex v = out.verts[edge[k]];
            v.y = border ? floorY : v.y - drop;
            low[k] = (uint32_t)out.verts.size();
            out.verts.push_back(v);
        }
        for (size_t k = 0; k + 1 < edge.size(); ++k) {
            const uint32_t t0 = edge[k], t1 = edge[k + 1], b0 = low[k], b1 = low[k + 1];
            out.idx.insert(out.idx.end(), { t0, t1, b1, t0, b1, b0 });
        }
    };
    const bool northBorder = z0 == 0, southBorder = z0 + b >= t.size();
    const bool westBorder = x0 == 0, eastBorder = x0 + a >= t.size();
    std::vector<uint32_t> edge;
    edge.clear();                                  // north
    for (int i = 0; i < nx; ++i) edge.push_back((uint32_t)i);
    skirt(edge, northBorder);
    edge.clear();                                  // south
    for (int i = 0; i < nx; ++i) edge.push_back((uint32_t)((nz - 1) * nx + i));
    skirt(edge, southBorder);
    edge.clear();                                  // west
    for (int j = 0; j < nz; ++j) edge.push_back((uint32_t)(j * nx));
    skirt(edge, westBorder);
    edge.clear();                                  // east
    for (int j = 0; j < nz; ++j) edge.push_back((uint32_t)(j * nx + nx - 1));
    skirt(edge, eastBorder);
}
