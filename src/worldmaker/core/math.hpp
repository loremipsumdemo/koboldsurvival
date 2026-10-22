#pragma once

#include <cmath>

namespace wm {


constexpr float kPi = 3.14159265358979f;
constexpr float kDeg2Rad = kPi / 180.0f;

struct V3 {
    float x, y, z;
};

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3& operator+=(V3& a, V3 b) { a = a + b; return a; }

inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline V3 Cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(V3 v) { return std::sqrt(Dot(v, v)); }

inline V3 Normalize(V3 v) {
    const float l = Length(v);
    return l > 0.0f ? v * (1.0f / l) : V3{0.0f, 0.0f, 0.0f};
}

// 4x4 matrix stored column-major, the layout OpenGL expects.
// Element (row, col) is m[col * 4 + row].
struct Mat4 {
    float m[16];
};

inline Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

inline Mat4 Perspective(float fovyDeg, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovyDeg * 0.5f * kDeg2Rad);
    Mat4 r{};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
    return r;
}

inline Mat4 LookAt(V3 eye, V3 target, V3 up) {
    const V3 f = Normalize(target - eye);
    const V3 s = Normalize(Cross(f, up));
    const V3 u = Cross(s, f);
    Mat4 r{};
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -Dot(s, eye);
    r.m[13] = -Dot(u, eye);
    r.m[14] = Dot(f, eye);
    r.m[15] = 1.0f;
    return r;
}

// Orthographic projection for 2D drawing. Pass top < bottom to get y-down screen coordinates.
inline Mat4 Ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
    Mat4 r{};
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (zFar - zNear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zFar + zNear) / (zFar - zNear);
    r.m[15] = 1.0f;
    return r;
}

}  // namespace wm
