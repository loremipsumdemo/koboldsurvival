#pragma once
#include "geometry.h"
#include <cmath>

struct FrustumPlanes {
    float p[6][4];
};

inline FrustumPlanes framePlanes(const Mat4& m) {
    auto el = [&](int r, int c) { return m.m[c * 4 + r]; };
    FrustumPlanes f{};
    for (int k = 0; k < 3; ++k) {
        for (int s = 0; s < 2; ++s) {
            const float sign = s == 0 ? 1.0f : -1.0f;
            float* p = f.p[2 * k + s];
            for (int c = 0; c < 4; ++c) p[c] = el(3, c) + sign * el(k, c);
            const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (len > 1e-12f) {
                for (int c = 0; c < 4; ++c) p[c] /= len;
            }
        }
    }
    return f;
}

// Whether any of the box from (x0, y0, z0) to (x1, y1, z1) may be in view: each plane is tested at the box corner farthest
// along it
inline bool boxVisible(const FrustumPlanes& f, float x0, float y0, float z0, float x1, float y1, float z1) {
    for (const auto& p : f.p) {
        const float px = p[0] >= 0.0f ? x1 : x0, py = p[1] >= 0.0f ? y1 : y0, pz = p[2] >= 0.0f ? z1 : z0;
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f) return false;
    }
    return true;
}

inline bool sphereVisible(const FrustumPlanes& f, float x, float y, float z, float r) {
    for (const auto& p : f.p) {
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < -r) return false;
    }
    return true;
}
