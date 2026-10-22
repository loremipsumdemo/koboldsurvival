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

// Colour of a ground type. Water and shelf are old types: they are shown as seabed sand.
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

} // namespace

int chunksPerSide(int size) {
    return (size + CHUNK_TILES - 1) / CHUNK_TILES;
}

ChunkBounds chunkBounds(const Terrain& t, int cx, int cz) {
    const int size = t.size(), vs = t.vertSize();
    const int x0 = cx * CHUNK_TILES, z0 = cz * CHUNK_TILES;
    const int x1 = std::min(x0 + CHUNK_TILES, size), z1 = std::min(z0 + CHUNK_TILES, size);
    float lo = 0.0f, hi = 0.0f;
    bool first = true;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float h = t.heights()[(size_t)z * vs + x];
            if (first) { lo = hi = h; first = false; }
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    // The skirts reach down to the lowest step's depth at most
    const float margin = SKIRT_BASE + 2.0f * 4.0f;
    return { (float)x0, (float)z0, (float)x1, (float)z1, lo - margin, hi + 1.0f };
}

void buildChunkMesh(const Terrain& t, int cx, int cz, int step, ChunkMesh& out) {
    out.verts.clear();
    out.idx.clear();
    const int size = t.size(), vs = t.vertSize();
    const std::vector<float>& h = t.heights();
    if (step < 1) step = 1;

    const int x0 = cx * CHUNK_TILES, z0 = cz * CHUNK_TILES;
    const int x1 = std::min(x0 + CHUNK_TILES, size), z1 = std::min(z0 + CHUNK_TILES, size);

    // Sample positions along each axis: every step tiles, plus the far edge of the chunk
    auto axis = [&](int a0, int a1) {
        std::vector<int> s;
        for (int a = a0; a < a1; a += step) s.push_back(a);
        s.push_back(a1);
        return s;
    };
    const std::vector<int> xs = axis(x0, x1), zs = axis(z0, z1);
    const int nx = (int)xs.size(), nz = (int)zs.size();
    auto H = [&](int x, int z) {
        return h[(size_t)std::clamp(z, 0, size) * vs + std::clamp(x, 0, size)];
    };

    // Directional light used only for slope shading
    const float lx = 0.35f, ly = 0.8f, lz = 0.25f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);

    out.verts.resize((size_t)nx * nz);
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int x = xs[(size_t)i], z = zs[(size_t)j];
            const int xl = xs[(size_t)std::max(i - 1, 0)], xr = xs[(size_t)std::min(i + 1, nx - 1)];
            const int zu = zs[(size_t)std::max(j - 1, 0)], zd = zs[(size_t)std::min(j + 1, nz - 1)];
            const float gx = (H(xr, z) - H(xl, z)) / (float)std::max(xr - xl, 1);
            const float gz = (H(x, zd) - H(x, zu)) / (float)std::max(zd - zu, 1);
            const float nl = std::sqrt(gx * gx + 1.0f + gz * gz);
            const float nxv = -gx / nl, nyv = 1.0f / nl, nzv = -gz / nl;
            const float lit = (nxv * lx + nyv * ly + nzv * lz) / ll;
            const float shade = std::clamp(0.45f + 0.55f * lit, 0.4f, 1.0f);

            const float hh = H(x, z);
            const uint8_t b = t.biome(std::min(x, size - 1), std::min(z, size - 1));
            const Rgb c = (b == BIOME_NONE) ? bandColour(hh) : biomeColour(b);
            out.verts[(size_t)j * nx + i] = { (float)x, hh, (float)z,
                                              c.r * shade, c.g * shade, c.b * shade, 1.0f };
        }
    }

    // Two triangles per cell, split along a-d to match Terrain::heightAt
    for (int j = 0; j < nz - 1; ++j) {
        for (int i = 0; i < nx - 1; ++i) {
            const uint32_t a = (uint32_t)(j * nx + i), b = a + 1;
            const uint32_t c = a + (uint32_t)nx, d = c + 1;
            out.idx.insert(out.idx.end(), { a, c, d, a, d, b });
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
    if (z0 > 0) {                       // north
        edge.clear();
        for (int i = 0; i < nx; ++i) edge.push_back((uint32_t)i);
        skirt(edge);
    }
    if (z1 < size) {                    // south
        edge.clear();
        for (int i = 0; i < nx; ++i) edge.push_back((uint32_t)((nz - 1) * nx + i));
        skirt(edge);
    }
    if (x0 > 0) {                       // west
        edge.clear();
        for (int j = 0; j < nz; ++j) edge.push_back((uint32_t)(j * nx));
        skirt(edge);
    }
    if (x1 < size) {                    // east
        edge.clear();
        for (int j = 0; j < nz; ++j) edge.push_back((uint32_t)(j * nx + nx - 1));
        skirt(edge);
    }
}
