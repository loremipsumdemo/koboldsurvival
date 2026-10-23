#include "terrain_mesh.h"
#include <algorithm>
#include <cmath>

namespace {

// How far a skirt hangs below its edge, plus a little for each step of detail
constexpr float SKIRT_BASE = 8.0f;

struct Rgb { float r, g, b; };

// Colour for a tile with no ground type: sandy below sea level, green, then grey on the high ground
Rgb bandColour(float h) {
    if (h < -1.33f) return { 0.86f, 0.76f, 0.46f };
    if (h < 1.33f) return { 0.27f, 0.55f, 0.24f };
    return { 0.27f, 0.27f, 0.29f };
}

// Colour of a ground type. Water and shelf ground is drawn as seabed sand.
Rgb biomeColour(uint8_t b) {
    switch (b) {
    case BIOME_SAND:
    case BIOME_WATER:
    case BIOME_SHELF:       return { 0.85f, 0.80f, 0.55f };
    case BIOME_FOREST:      return { 0.14f, 0.36f, 0.18f };
    case BIOME_DESERT:      return { 0.80f, 0.66f, 0.42f };
    case BIOME_SNOW:        return { 0.92f, 0.94f, 0.98f };
    case BIOME_ROCK:        return { 0.50f, 0.48f, 0.46f };
    case BIOME_BEACH:       return { 0.90f, 0.85f, 0.62f };
    case BIOME_SILT:        return { 0.30f, 0.28f, 0.22f };
    case BIOME_FROZEN_CLAY: return { 0.70f, 0.78f, 0.82f };
    case BIOME_CLAY:        return { 0.62f, 0.45f, 0.34f };
    case BIOME_GRAVEL:      return { 0.56f, 0.55f, 0.52f };
    case BIOME_ARID_SOIL:   return { 0.66f, 0.56f, 0.38f };
    case BIOME_SCREE:       return { 0.42f, 0.41f, 0.40f };
    case BIOME_FLOODPLAIN:  return { 0.28f, 0.46f, 0.26f };
    case BIOME_CLIFF_ROCK:  return { 0.36f, 0.34f, 0.33f };
    default:                return { 0.36f, 0.58f, 0.27f };
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
// the neighbouring chunk when that is loaded, so shading matches across the border. Otherwise the
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
    // The skirts reach down to the lowest step's depth at most
    const float margin = SKIRT_BASE + 2.0f * 4.0f;
    const float x0 = (float)(c.cx * CHUNK_TILES), z0 = (float)(c.cz * CHUNK_TILES);
    return { x0, z0, x0 + a, z0 + b, lo - margin, hi + 1.0f };
}

void buildChunkMesh(const Terrain& t, const ChunkData& c, int step, ChunkMesh& out) {
    out.verts.clear();
    out.idx.clear();
    if (step < 1) step = 1;

    const int a = tilesAcross(t, c), b = tilesDown(t, c);
    const int x0 = c.cx * CHUNK_TILES, z0 = c.cz * CHUNK_TILES;

    // Sample positions along each axis, in chunk corners: every step tiles, plus the far edge
    auto axis = [&](int len) {
        std::vector<int> s;
        for (int v = 0; v < len; v += step) s.push_back(v);
        s.push_back(len);
        return s;
    };
    const std::vector<int> xs = axis(a), zs = axis(b);
    const int nx = (int)xs.size(), nz = (int)zs.size();

    // Directional light used only for slope shading
    const float lx = 0.35f, ly = 0.8f, lz = 0.25f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);

    out.verts.resize((size_t)nx * nz);
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int x = xs[(size_t)i], z = zs[(size_t)j];
            // Neighbouring samples, one step either side, so the slope is smooth across chunk borders
            const int xl = (i == 0) ? x - step : xs[(size_t)i - 1];
            const int xr = (i == nx - 1) ? x + step : xs[(size_t)i + 1];
            const int zu = (j == 0) ? z - step : zs[(size_t)j - 1];
            const int zd = (j == nz - 1) ? z + step : zs[(size_t)j + 1];
            const int gx = x0 + x, gz = z0 + z;
            const float gxSlope = (cornerHeight(t, c, x0 + xr, gz) - cornerHeight(t, c, x0 + xl, gz))
                                / (float)(xr - xl);
            const float gzSlope = (cornerHeight(t, c, gx, z0 + zd) - cornerHeight(t, c, gx, z0 + zu))
                                / (float)(zd - zu);
            const float nl = std::sqrt(gxSlope * gxSlope + 1.0f + gzSlope * gzSlope);
            const float nxv = -gxSlope / nl, nyv = 1.0f / nl, nzv = -gzSlope / nl;
            const float lit = (nxv * lx + nyv * ly + nzv * lz) / ll;
            const float shade = std::clamp(0.45f + 0.55f * lit, 0.4f, 1.0f);

            const float hh = c.heights[(size_t)z * CHUNK_CORNERS + x];
            const int tx = std::min(x, std::max(a - 1, 0)), tz = std::min(z, std::max(b - 1, 0));
            const uint8_t bt = c.biome[(size_t)tz * CHUNK_TILES + tx];
            const Rgb col = (bt == BIOME_NONE) ? bandColour(hh) : biomeColour(bt);
            out.verts[(size_t)j * nx + i] = { (float)gx, hh, (float)gz,
                                              col.r * shade, col.g * shade, col.b * shade, 1.0f };
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

    // Skirts on the edges that border another chunk
    const float drop = SKIRT_BASE + 2.0f * (float)step;
    auto skirt = [&](const std::vector<uint32_t>& edge) {
        std::vector<uint32_t> low(edge.size());
        for (size_t k = 0; k < edge.size(); ++k) {
            TerrainVertex v = out.verts[edge[k]];
            v.y -= drop;
            v.r *= 0.6f;
            v.g *= 0.6f;
            v.b *= 0.6f;
            low[k] = (uint32_t)out.verts.size();
            out.verts.push_back(v);
        }
        for (size_t k = 0; k + 1 < edge.size(); ++k) {
            const uint32_t t0 = edge[k], t1 = edge[k + 1], b0 = low[k], b1 = low[k + 1];
            out.idx.insert(out.idx.end(), { t0, t1, b1, t0, b1, b0 });
        }
    };
    std::vector<uint32_t> edge;
    if (z0 > 0) {                                  // north
        edge.clear();
        for (int i = 0; i < nx; ++i) edge.push_back((uint32_t)i);
        skirt(edge);
    }
    if (z0 + b < t.size()) {                       // south
        edge.clear();
        for (int i = 0; i < nx; ++i) edge.push_back((uint32_t)((nz - 1) * nx + i));
        skirt(edge);
    }
    if (x0 > 0) {                                  // west
        edge.clear();
        for (int j = 0; j < nz; ++j) edge.push_back((uint32_t)(j * nx));
        skirt(edge);
    }
    if (x0 + a < t.size()) {                       // east
        edge.clear();
        for (int j = 0; j < nz; ++j) edge.push_back((uint32_t)(j * nx + nx - 1));
        skirt(edge);
    }
}
