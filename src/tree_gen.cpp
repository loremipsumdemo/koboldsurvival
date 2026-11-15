#include "tree_gen.h"
#include <algorithm>
#include <cmath>
#include "calendar.h"

namespace {
constexpr float PI = 3.14159265358979f;

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(V3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalized(V3 a) {
    const float l = length(a);
    return l > 1e-9f ? a * (1.0f / l) : V3{ 0.0f, 1.0f, 0.0f };
}

struct Q {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
Q mul(Q a, Q b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
V3 rotate(Q q, V3 v) {
    const V3 u{ q.x, q.y, q.z };
    const V3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
// The turn that takes one direction to another (as three.js's setFromUnitVectors)
Q fromUnitVectors(V3 from, V3 to) {
    float r = dot(from, to) + 1.0f;
    Q q;
    if (r < 1e-6f) {
        r = 0.0f;
        if (std::fabs(from.x) > std::fabs(from.z)) q = { -from.y, from.x, 0.0f, r };
        else q = { 0.0f, -from.z, from.y, r };
    } else {
        const V3 c = cross(from, to);
        q = { c.x, c.y, c.z, r };
    }
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return { q.x / l, q.y / l, q.z / l, q.w / l };
}
Q aroundY(float a) { return { 0.0f, std::sin(a * 0.5f), 0.0f, std::cos(a * 0.5f) }; }
// Euler angles applied in the order X, Y, Z (as three.js's default)
Q fromEuler(float x, float y, float z) {
    const float c1 = std::cos(x * 0.5f), c2 = std::cos(y * 0.5f), c3 = std::cos(z * 0.5f);
    const float s1 = std::sin(x * 0.5f), s2 = std::sin(y * 0.5f), s3 = std::sin(z * 0.5f);
    return { s1 * c2 * c3 + c1 * s2 * s3, c1 * s2 * c3 - s1 * c2 * s3, c1 * c2 * s3 + s1 * s2 * c3, c1 * c2 * c3 - s1 * s2 * s3 };
}

// A place and a turn: a limb's own coordinate system inside its parent's
struct Xf {
    Q q;
    V3 p;
};
V3 apply(const Xf& t, V3 v) { return t.p + rotate(t.q, v); }
Xf compose(const Xf& parent, const Xf& child) { return { mul(parent.q, child.q), parent.p + rotate(parent.q, child.p) }; }

uint32_t hashOf(uint32_t seed, int n) {
    uint32_t x = seed ^ ((uint32_t)(n + 1) * 0x9e3779b1u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// The palette: five woods, then each season's leaves, then three blossoms
const uint32_t WOODS[5] = { 0x865232, 0x965b39, 0xa96a44, 0x76472e, 0xb3774b };
const uint32_t SPRING[6] = { 0x98bb53, 0xb8d478, 0x85b95d, 0xcce09c, 0xedd3ce, 0xf6bfc6 };
const uint32_t SUMMER[5] = { 0x5d8737, 0x709d44, 0x86a84d, 0x4c782f, 0x9cb65e };
const uint32_t AUTUMN[6] = { 0xc98237, 0xe4a44b, 0xa9492c, 0xd46a33, 0xe9bb55, 0x9b4830 };
const uint32_t WINTER[5] = { 0xc0d4d8, 0xe8edf0, 0xaac6cf, 0xf4f4ef, 0x8eaaaf };
const uint32_t BLOSSOM[3] = { 0xedd3ce, 0xf6bfc6, 0xf4d8dd };
constexpr int ROW_WOOD = 0, ROW_SPRING = 5, ROW_SUMMER = 11, ROW_AUTUMN = 16, ROW_WINTER = 22, ROW_BLOSSOM = 27;

enum class Season { Spring, Summer, Autumn, Winter };
struct SeasonState {
    Season key = Season::Summer;
    float fullness = 1.0f, bloom = 0.0f;
};

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// The season of a day of a 365-day year, with canopy fullness continuous across the boundaries
SeasonState seasonOf(int day) {
    SeasonState s;
    if (day >= 60 && day <= 151) {
        const float t = (float)(day - 60) / 92.0f;
        s = { Season::Spring, lerpf(0.12f, 0.93f, t), 1.0f - t };
    } else if (day >= 152 && day <= 243) {
        s = { Season::Summer, lerpf(0.93f, 1.0f, (float)(day - 152) / 92.0f), 0.0f };
    } else if (day >= 244 && day <= 334) {
        s = { Season::Autumn, lerpf(0.99f, 0.13f, (float)(day - 244) / 91.0f), 0.0f };
    } else {
        s = { Season::Winter, 0.08f, 0.0f };
    }
    return s;
}

struct Genes {
    int primaryBranches = 3;
    float branchRotation = 0.0f, branchSpread = 0.0f, branchLength = 1.0f;
};

struct Tip {
    Xf limb;
    float length;
    int n;
};

struct Builder {
    uint32_t seed;
    TreeMesh& mesh;
    int detail;
    std::vector<Tip> tips;

    float rand(int n) const { return (float)hashOf(seed, n) / 4294967296.0f; }

    // A smooth shade along the palette's strip for a face looking this way, as the props' models have it
    static float shadeU(V3 n, float y) {
        float light = 0.5f * (n.y + 1.0f) * 0.65f + 0.35f * std::max(0.0f, n.x * 0.6f + n.z * 0.8f);
        light -= 0.35f * std::max(0.0f, 1.0f - y / 0.3f);
        light = std::clamp(light, 0.0f, 1.0f);
        return (light * (float)(TREE_PALETTE_SHADES - 1) + 0.5f) / (float)TREE_PALETTE_SHADES;
    }

    void face(const V3* pts, int count, int row, V3 awayFrom) {
        V3 n = normalized(cross(pts[1] - pts[0], pts[2] - pts[0]));
        V3 mid{};
        for (int i = 0; i < count; ++i) mid = mid + pts[i];
        mid = mid * (1.0f / (float)count);
        const bool flip = dot(n, mid - awayFrom) < 0.0f;
        if (flip) n = n * -1.0f;
        const uint32_t base = (uint32_t)(mesh.verts.size() / 5);
        const float v = ((float)row + 0.5f) / (float)TREE_PALETTE_ROWS;
        for (int i = 0; i < count; ++i) {
            const V3& p = pts[i];
            mesh.verts.insert(mesh.verts.end(), { p.x, p.y, p.z, shadeU(n, p.y), v });
        }
        for (int k = 1; k + 1 < count; ++k) {
            if (flip) mesh.indices.insert(mesh.indices.end(), { base, base + (uint32_t)k + 1, base + (uint32_t)k });
            else mesh.indices.insert(mesh.indices.end(), { base, base + (uint32_t)k, base + (uint32_t)k + 1 });
        }
    }

    // A five-sided limb from its origin up its own y by `len`, wide at the foot and narrow at the top. Its ends are not capped: a joint, or
    // a puff of leaves, hides them.
    void limb(const Xf& at, float len, float r0, float r1, int row) {
        constexpr int SIDES = 5;
        V3 bottom[SIDES], top[SIDES];
        for (int i = 0; i < SIDES; ++i) {
            const float a = 2.0f * PI * (float)i / (float)SIDES;
            bottom[i] = apply(at, V3{ std::sin(a) * r0, 0.0f, std::cos(a) * r0 });
            top[i] = apply(at, V3{ std::sin(a) * r1, len, std::cos(a) * r1 });
        }
        const V3 axis = apply(at, V3{ 0.0f, len * 0.5f, 0.0f });
        for (int i = 0; i < SIDES; ++i) {
            const int j = (i + 1) % SIDES;
            const V3 quad[4] = { bottom[i], bottom[j], top[j], top[i] };
            face(quad, 4, row, axis);
        }
    }

    int woodRow(int n) const { return ROW_WOOD + (int)std::floor(rand(n) * 5.0f); }

    // Every limb has its own coordinate system, and every child begins at its attachment point in its parent's
    void branch(V3 start, V3 direction, float len, float radius, int depth, int n, const Xf& parent) {
        Xf self{ fromUnitVectors(V3{ 0, 1, 0 }, normalized(direction)), start };
        const Xf world = compose(parent, self);
        const bool tip = depth == 0 || (detail >= 1 && depth == 1);
        limb(world, len, radius, radius * (depth ? 0.58f : 0.17f), woodRow(n));
        if (tip) {
            tips.push_back({ world, len, n });
            return;
        }
        for (int k = 0; k < 2; ++k) {
            const int id = n * 3 + k + 7;
            const float attachment = len * (0.52f + rand(id + 12) * 0.25f);
            const float angle = (float)k * PI + rand(id + 17) * 0.65f;
            const float radial = 0.30f + rand(id + 4) * 0.22f;
            const float vertical = 0.78f + rand(id + 5) * 0.14f;
            const V3 dir = normalized(V3{ std::cos(angle) * radial, vertical, std::sin(angle) * radial });
            branch(V3{ 0.0f, attachment, 0.0f }, dir, len * (0.57f + rand(id + 21) * 0.12f), radius * 0.58f, depth - 1, id, world);
        }
    }

    void wood(const Genes& g) {
        const float height = 3.85f * (0.88f + g.branchLength * 0.22f), sc = height / 3.85f;
        const V3 raw[5] = { { 0, 0, 0 }, { 0.035f, 1, 0.025f }, { -0.03f, 2.05f, 0.03f }, { 0.015f, 3.12f, -0.02f }, { -0.03f, 3.85f, 0.04f } };
        V3 points[5];
        for (int i = 0; i < 5; ++i) points[i] = raw[i] * sc;
        for (int j = 0; j < 4; ++j) {
            const V3 delta = points[j + 1] - points[j];
            const float len = length(delta);
            if (len < 0.035f) continue;
            const Xf at{ fromUnitVectors(V3{ 0, 1, 0 }, normalized(delta)), points[j] };
            limb(at, len, 0.33f - (float)j * 0.05f, 0.27f - (float)j * 0.048f, woodRow(100 + j));
        }
        for (int i = 0; i < g.primaryBranches; ++i) {
            const float t = (float)i / (float)(g.primaryBranches - 1);
            const int n = 200 + i * 83;
            const float y = (1.7f + t * 1.45f + (rand(n) - 0.5f) * 0.18f) * sc;
            const float azimuth = g.branchRotation + (float)i * 2.39996323f + (rand(n + 1) - 0.5f) * 0.28f;
            V3 anchor = points[0];
            for (int j = 0; j < 4; ++j) {
                if (y >= points[j].y && y <= points[j + 1].y) {
                    anchor = points[j] + (points[j + 1] - points[j]) * ((y - points[j].y) / (points[j + 1].y - points[j].y));
                    break;
                }
            }
            const Xf pivot{ aroundY(azimuth), anchor };
            const V3 dir = normalized(V3{ std::sin(g.branchSpread), std::cos(g.branchSpread), 0.0f });
            const float len = (1.6f + (rand(n + 4) - 0.5f) * 0.52f) * g.branchLength;
            branch(V3{ 0, 0, 0 }, dir, len, 0.17f - t * 0.045f, 2, n, pivot);
        }
        branch(points[4], normalized(V3{ 0.16f, 0.98f, 0.1f }), 0.72f * g.branchLength, 0.078f, 1, 810, Xf{});
    }

    // One puff of leaves: a small low-poly ball, squashed and turned at random, in the crown of a tip
    void puff(const Xf& anchor, V3 pos, float size, const SeasonState& st, int n) {
        const uint32_t* colors = nullptr;
        int count = 0, rowBase = 0;
        switch (st.key) {
        case Season::Spring: colors = SPRING; count = 6; rowBase = ROW_SPRING; break;
        case Season::Summer: colors = SUMMER; count = 5; rowBase = ROW_SUMMER; break;
        case Season::Autumn: colors = AUTUMN; count = 6; rowBase = ROW_AUTUMN; break;
        case Season::Winter: colors = WINTER; count = 5; rowBase = ROW_WINTER; break;
        }
        (void)colors;
        int row = rowBase + (int)std::floor(rand(n + 1) * (float)count);
        if (st.key == Season::Spring && rand(n + 18) < st.bloom * 0.45f) row = ROW_BLOSSOM + (int)std::floor(rand(n + 19) * 3.0f);
        const float sx = size * (0.88f + rand(n + 2) * 0.28f), sy = size * (0.78f + rand(n + 3) * 0.55f), sz = size * (0.85f + rand(n + 4) * 0.3f);
        const Xf local{ fromEuler(rand(n + 5) * 1.8f, rand(n + 6) * 6.28f, rand(n + 7) * 1.2f), pos };
        const Xf world = compose(anchor, local);
        static const float T = 1.61803399f;
        static const V3 corners[12] = { { -1, T, 0 }, { 1, T, 0 }, { -1, -T, 0 }, { 1, -T, 0 }, { 0, -1, T }, { 0, 1, T },
                                        { 0, -1, -T }, { 0, 1, -T }, { T, 0, -1 }, { T, 0, 1 }, { -T, 0, -1 }, { -T, 0, 1 } };
        static const int faces[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
                                          { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 },
                                          { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
        V3 pts[12];
        for (int i = 0; i < 12; ++i) {
            const V3 c = normalized(corners[i]);
            pts[i] = apply(world, V3{ c.x * sx, c.y * sy, c.z * sz });
        }
        const V3 middle = world.p;
        for (const auto& f : faces) {
            const V3 tri[3] = { pts[f[0]], pts[f[1]], pts[f[2]] };
            face(tri, 3, row, middle);
        }
    }

    void foliage(const SeasonState& st) {
        const float fullness = st.fullness;
        const bool winter = st.key == Season::Winter;
        const float density = fullness * (winter ? 0.33f : st.key == Season::Spring ? 0.88f : 1.0f);
        for (const Tip& tip : tips) {
            const int n = tip.n;
            if (rand(n + 887) > density) continue;
            const Xf anchor = compose(tip.limb, Xf{ Q{}, V3{ 0.0f, tip.length, 0.0f } });
            const float base = (0.21f + 0.36f * fullness) * (winter ? 0.62f : 1.0f) * (detail >= 1 ? 1.5f : 1.0f);
            const V3 center{ 0.0f, 0.07f + 0.11f * rand(n + 15), 0.0f };
            puff(anchor, center, base * (0.8f + 0.38f * rand(n + 16)), st, n + 93);
            if (detail >= 1) continue;
            if (fullness > 0.25f && rand(n + 33) < fullness * (winter ? 0.18f : 0.92f)) {
                const float a = rand(n + 34) * PI * 2.0f;
                const V3 offset{ std::cos(a) * base * 0.67f, base * (rand(n + 35) - 0.1f), std::sin(a) * base * 0.67f };
                puff(anchor, center + offset, base * (0.65f + 0.30f * rand(n + 36)), st, n + 144);
            }
            if (fullness > 0.70f && !winter && rand(n + 57) < fullness - 0.45f) {
                puff(anchor, center + V3{ -base * 0.46f, base * 0.32f, base * 0.43f }, base * 0.62f, st, n + 177);
            }
        }
    }
};
}

TreeMesh generateTree(int variant, int stage, int detail) {
    variant = std::clamp(variant, 0, TREE_VARIANTS - 1);
    stage = std::clamp(stage, 0, TREE_STAGES - 1);
    TreeMesh mesh;
    const uint32_t seed = (uint32_t)(138937u * 977u + (uint32_t)variant * 104729u);
    Builder b{ seed, mesh, detail, {} };
    Genes g;
    g.primaryBranches = 3 + (int)std::floor(b.rand(1) * 4.0f);
    g.branchRotation = b.rand(2) * 2.0f * PI;
    g.branchSpread = (22.0f + b.rand(3) * 52.0f) * PI / 180.0f;
    g.branchLength = 0.60f + b.rand(4) * 0.85f;
    b.wood(g);
    // The middle of the stage's span of days
    const int day = 1 + (int)((stage + 0.5f) * (float)TREE_YEAR_DAYS / (float)TREE_STAGES);
    b.foliage(seasonOf(day));
    return mesh;
}

std::vector<uint8_t> treePalette() {
    std::vector<uint32_t> colors;
    for (uint32_t c : WOODS) colors.push_back(c);
    for (uint32_t c : SPRING) colors.push_back(c);
    for (uint32_t c : SUMMER) colors.push_back(c);
    for (uint32_t c : AUTUMN) colors.push_back(c);
    for (uint32_t c : WINTER) colors.push_back(c);
    for (uint32_t c : BLOSSOM) colors.push_back(c);
    std::vector<uint8_t> px((size_t)TREE_PALETTE_ROWS * TREE_PALETTE_SHADES * 4, 255);
    for (int row = 0; row < TREE_PALETTE_ROWS && row < (int)colors.size(); ++row) {
        for (int i = 0; i < TREE_PALETTE_SHADES; ++i) {
            const float k = 0.5f + 0.65f * (float)i / (float)(TREE_PALETTE_SHADES - 1);
            const uint32_t c = colors[(size_t)row];
            const size_t at = ((size_t)row * TREE_PALETTE_SHADES + i) * 4;
            px[at] = (uint8_t)std::min(255.0f, (float)((c >> 16) & 255) * k);
            px[at + 1] = (uint8_t)std::min(255.0f, (float)((c >> 8) & 255) * k);
            px[at + 2] = (uint8_t)std::min(255.0f, (float)(c & 255) * k);
            px[at + 3] = 255;
        }
    }
    return px;
}

int treeStageOfDay(int dayOfYear) {
    // The game's year has 360 days; the seasons are laid out over 365
    const float d365 = 1.0f + (float)(std::clamp(dayOfYear, 1, DAYS_PER_MONTH * MONTHS_PER_YEAR) - 1) * 364.0f / (float)(DAYS_PER_MONTH * MONTHS_PER_YEAR - 1);
    return std::clamp((int)((d365 - 1.0f) * (float)TREE_STAGES / (float)TREE_YEAR_DAYS), 0, TREE_STAGES - 1);
}

int treeStageOfClock(double clockSeconds) {
    const long long d = dayIndexOf(clockSeconds);
    const int inYear = (int)(((d % (DAYS_PER_MONTH * MONTHS_PER_YEAR)) + DAYS_PER_MONTH * MONTHS_PER_YEAR) % (DAYS_PER_MONTH * MONTHS_PER_YEAR));
    return treeStageOfDay(inYear + 1);
}

int treeSeasonOfStage(int stage) {
    const int day = 1 + (int)((float)(std::clamp(stage, 0, TREE_STAGES - 1) + 0.5f) * (float)TREE_YEAR_DAYS / (float)TREE_STAGES);
    const SeasonState s = seasonOf(day);
    return s.key == Season::Spring ? 0 : s.key == Season::Summer ? 1 : s.key == Season::Autumn ? 2 : 3;
}
