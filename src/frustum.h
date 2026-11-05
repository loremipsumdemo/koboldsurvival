#pragma once
// The camera's view as six planes, for leaving out what is off the screen before it is drawn
#include "geometry.h"
#include <cmath>

// The six frustum planes of a column-major view-projection matrix, as (a, b, c, d) with a*x+b*y+c*z+d >= 0 inside.
// Each is scaled so (a, b, c) has length 1, which makes a*x+b*y+c*z+d the distance from the plane.
struct FrustumPlanes {
    float p[6][4];
};

inline FrustumPlanes framePlanes(const Mat4& m) {
    // Element (row r, column c) is m[c * 4 + r]
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

// True unless the ball at (x, y, z) of radius r is entirely outside one of the planes
inline bool sphereVisible(const FrustumPlanes& f, float x, float y, float z, float r) {
    for (const auto& p : f.p) {
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < -r) return false;
    }
    return true;
}
