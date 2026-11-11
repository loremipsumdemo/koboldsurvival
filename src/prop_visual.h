#pragma once
#include <cmath>
#include "components.h"
#include "geometry.h"
#include "model.h"

// Where a prop's model is drawn and clicked when it has been adjusted (PropVisual): its usual placement on its tile,
// stretched from the ground up, turned about its middle, then moved. One matrix for both drawing and clicking, so the
// two never part.
inline Mat4 identity4() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

inline Mat4 translation4(float x, float y, float z) {
    Mat4 r = identity4();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

// Right-handed turns about x, y and z, in degrees: y first applied last, so a turn about y spins it where it stands
inline Mat4 rotation4(float rxDeg, float ryDeg, float rzDeg) {
    const float k = 0.01745329252f;
    const float cx = std::cos(rxDeg * k), sx = std::sin(rxDeg * k);
    const float cy = std::cos(ryDeg * k), sy = std::sin(ryDeg * k);
    const float cz = std::cos(rzDeg * k), sz = std::sin(rzDeg * k);
    Mat4 X = identity4(), Y = identity4(), Z = identity4();
    X.m[5] = cx; X.m[6] = sx; X.m[9] = -sx; X.m[10] = cx;
    Y.m[0] = cy; Y.m[2] = -sy; Y.m[8] = sy; Y.m[10] = cy;
    Z.m[0] = cz; Z.m[1] = sz; Z.m[4] = -sz; Z.m[5] = cz;
    return mul(Y, mul(X, Z));
}

// How high above its base the middle of a prop of this size is taken to be: what it turns about, and where the Adjust
// tool's widget sits
inline float propVisualMiddle(float size) { return size * 0.4f; }

inline Mat4 propVisualMatrix(const Model& m, float x, float y, float z, float size, float yaw, const PropVisual& v) {
    const ModelPlacement p = placeModel(m, 0.0f, 0.0f, 0.0f, size, yaw);
    Mat4 base{};
    base.m[0] = p.k * p.cs;
    base.m[2] = -p.k * p.sn;
    base.m[5] = p.k;
    base.m[8] = p.k * p.sn;
    base.m[10] = p.k * p.cs;
    base.m[15] = 1.0f;
    base.m[12] = p.ox;
    base.m[13] = p.oy;
    base.m[14] = p.oz;
    Mat4 stretch = identity4();
    stretch.m[0] = v.sx;
    stretch.m[5] = v.sy;
    stretch.m[10] = v.sz;
    const float mid = propVisualMiddle(size);
    const Mat4 turn = mul(translation4(0.0f, mid, 0.0f), mul(rotation4(v.rx, v.ry, v.rz), translation4(0.0f, -mid, 0.0f)));
    return mul(translation4(x + v.dx, y + v.dy, z + v.dz), mul(turn, mul(stretch, base)));
}
