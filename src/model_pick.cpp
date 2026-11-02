// Placing a model in the world, and finding where a ray meets it. Kept apart from model.cpp, which loads models into
// OpenGL, so this needs no GL and the tests can check it.
#include "model.h"
#include <algorithm>
#include <cmath>

ModelPlacement placeModel(const Model& m, float x, float y, float z, float targetSize) {
    ModelPlacement p;
    const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
    if (extent <= 0.0f || targetSize <= 0.0f) return p;
    p.k = targetSize / extent;
    p.ox = x - p.k * (m.minX + m.maxX) * 0.5f;
    p.oy = y - p.k * m.minY;
    p.oz = z - p.k * (m.minZ + m.maxZ) * 0.5f;
    p.ok = true;
    return p;
}

float rayHitModel(const Model& m, const ModelPlacement& p, const float o[3], const float d[3]) {
    if (!p.ok || m.triangles.size() < 3) return -1.0f;
    // Into the model's own space: o' = (o - offset) / k, d' = d / k, so the distance along the ray stays the same
    const float ro[3] = { (o[0] - p.ox) / p.k, (o[1] - p.oy) / p.k, (o[2] - p.oz) / p.k };
    const float rd[3] = { d[0] / p.k, d[1] / p.k, d[2] / p.k };
    // A miss of the model's box needs no triangle tests
    {
        float t0 = -1e30f, t1 = 1e30f;
        const float lo[3] = { m.minX, m.minY, m.minZ }, hi[3] = { m.maxX, m.maxY, m.maxZ };
        for (int a = 0; a < 3; ++a) {
            if (std::fabs(rd[a]) < 1e-12f) {
                if (ro[a] < lo[a] || ro[a] > hi[a]) return -1.0f;
                continue;
            }
            float ta = (lo[a] - ro[a]) / rd[a], tb = (hi[a] - ro[a]) / rd[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return -1.0f;
        }
        if (t1 < 0.0f) return -1.0f;
    }
    // Every triangle, both faces (Moller-Trumbore)
    float best = -1.0f;
    const size_t vcount = m.positions.size() / 3;
    for (size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        const GLuint ia = m.triangles[i], ib = m.triangles[i + 1], ic = m.triangles[i + 2];
        if (ia >= vcount || ib >= vcount || ic >= vcount) continue;
        const float* a = &m.positions[ia * 3];
        const float* b = &m.positions[ib * 3];
        const float* c = &m.positions[ic * 3];
        const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        const float pv[3] = { rd[1] * e2[2] - rd[2] * e2[1], rd[2] * e2[0] - rd[0] * e2[2], rd[0] * e2[1] - rd[1] * e2[0] };
        const float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
        if (std::fabs(det) < 1e-12f) continue;
        const float inv = 1.0f / det;
        const float tv[3] = { ro[0] - a[0], ro[1] - a[1], ro[2] - a[2] };
        const float u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const float qv[3] = { tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0] };
        const float v = (rd[0] * qv[0] + rd[1] * qv[1] + rd[2] * qv[2]) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
        if (t >= 0.0f && (best < 0.0f || t < best)) best = t;
    }
    return best;
}
