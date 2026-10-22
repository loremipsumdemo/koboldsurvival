// Terrain, sea plane and seabed. Split from renderer.cpp to keep both files readable.
#include "render/renderer.hpp"
#include "render/surface_tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace wm {


namespace {

// ---------------------------------------------------------------------------
// Shaders (GLSL 3.30 core)
// ---------------------------------------------------------------------------

// Terrain: per-vertex colour, lit by the sun, with ambient fill and fog.
const char* const kTerrainVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
uniform mat4 uViewProj;
uniform vec3 uEye;
out vec3 vNrm;
out vec3 vCol;
out float vDist;
out vec2 vXZ;
out vec3 vPos;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vNrm = aNrm;
    vCol = aCol;
    vDist = distance(aPos, uEye);
    vXZ = aPos.xz;
    vPos = aPos;
}
)GLSL";

const char* const kTerrainFS = R"GLSL(
#version 330 core
uniform vec3 uUnderwater;
in vec3 vNrm;
in vec3 vCol;
in float vDist;
in vec2 vXZ;
in vec3 vPos;
uniform sampler2D uTile;
uniform float uSeaY;
uniform vec3 uTint;
uniform vec3 uRockRgb;
uniform float uRockSlope;
uniform float uSlopeView;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform vec3 uFogColour;
uniform float uFog;
out vec4 fragColour;
void main() {
    // Ground detail from the tile: its normal tilts the lighting, and its height
    // darkens the colour a little. The tile repeats every 8 world units.
    vec4 t = texture(uTile, vXZ / 8.0);
    // The fine grain fades out with distance so far ground does not shimmer.
    float grain = 1.0 - smoothstep(40.0, 220.0, vDist);
    vec3 bump = vec3(t.r * 2.0 - 1.0, 0.0, t.g * 2.0 - 1.0) * grain;
    vec3 n = normalize(normalize(vNrm) + bump * 0.35);
    float diff = max(dot(n, normalize(uSunDir)), 0.0);

    // Steep faces turn to bare rock.
    float steep = 1.0 - clamp(normalize(vNrm).y, 0.0, 1.0);
    float rock = smoothstep(uRockSlope, uRockSlope + 0.15, steep);
    vec3 base = mix(vCol, uRockRgb, rock);
    // Steep faces take more of the sky's light, so they read as rock rather than black.
    vec3 lit = base * mix(1.0, 0.85 + 0.3 * t.a, grain) * (uAmbient * (1.0 + 0.8 * steep) + uSunLight * diff);
    if (uSlopeView > 0.5) {
        fragColour = vec4(mix(vec3(0.15, 0.55, 0.2), vec3(0.9, 0.15, 0.1), clamp(steep * 2.5, 0.0, 1.0)), 1.0);
        return;
    }

    // Under the sea, the water between the eye and the ground absorbs light and tints
    // it. The deeper the ground, the more of the water it has been seen through.
    float depth = max(uSeaY - vPos.y, 0.0);
    float absorb = 1.0 - exp(-depth * 0.25);
    vec3 underwater = uUnderwater * (uAmbient + uSunLight * 0.5);
    lit = mix(lit, underwater, absorb) * uTint;

    float d = vDist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColour = vec4(mix(lit, uFogColour, f), 1.0);
}
)GLSL";

// Sea and seabed build their quads in the vertex shader from gl_VertexID,
// so they need no vertex buffer, only an empty VAO.
const char* const kQuadCorners = R"GLSL(
const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
)GLSL";

const char* const kWaterVS = R"GLSL(
#version 330 core
uniform mat4 uViewProj;
uniform vec3 uEye;
uniform vec3 uCentre;
uniform float uHalf;
uniform float uSeaY;
out vec3 vWorld;
)GLSL";

// Seabed beyond the map: a flat plane far below the sea. Seen through the water it
// is just the deep-water tint, so one flat colour is enough.
const char* const kFloorVS = R"GLSL(
#version 330 core
uniform mat4 uViewProj;
uniform vec3 uCentre;
uniform float uHalf;
out vec3 vWorld;
const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 c = kCorner[gl_VertexID];
    vec3 p = vec3(uCentre.x + c.x * uHalf, uCentre.y, uCentre.z + c.y * uHalf);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL";

const char* const kFloorFS = R"GLSL(
#version 330 core
uniform vec3 uUnderwater;
in vec3 vWorld;
uniform vec3 uEye;
uniform vec3 uAmbient;
uniform vec3 uSunLight;
uniform vec3 uFogColour;
uniform float uFog;
out vec4 fragColour;
void main() {
    vec3 lit = uUnderwater * (uAmbient + uSunLight * 0.5);
    float d = distance(vWorld, uEye) * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColour = vec4(mix(lit, uFogColour, f), 1.0);
}
)GLSL";

const char* const kWaterFS = R"GLSL(
#version 330 core
in vec3 vWorld;
uniform vec3 uEye;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform vec3 uColour;
uniform vec3 uFogColour;
uniform float uFog;
uniform float uTime;
uniform float uOpacity;
uniform float uPattern;
uniform float uSpeed;
uniform float uFadeStart;
uniform float uFadeEnd;
uniform sampler2D uTile;
out vec4 fragColour;
void main() {
    // Two drifting copies of the tile, at different scales and directions, so the
    // surface shows no obvious repeat. Nothing here reflects the sky: the light only
    // changes the shading and the bright caustic bands.
    vec2 p = vWorld.xz;
    float tm = uTime * uSpeed;
    vec4 t1 = texture(uTile, p / 10.0 + vec2(tm * 0.020, tm * 0.013));
    vec4 t2 = texture(uTile, p / 6.0 - vec2(tm * 0.017, tm * 0.025));
    vec4 t = mix(t1, t2, 0.5);
    vec3 nt = t.rgb * 2.0 - 1.0;
    vec3 n = normalize(vec3(nt.x * 1.4, 1.0, nt.y * 1.4));
    vec3 sun = normalize(uSunDir);
    float ndl = max(dot(n, sun), 0.0);

    // Caustic-like light play: thin bright bands along the height ridges, brightest
    // when the sun is high.
    float ridge = 1.0 - abs(2.0 * t.a - 1.0);
    float caustic = pow(ridge, 4.0) * max(sun.y, 0.0) * uPattern;

    vec3 base = uColour * (0.85 + 0.3 * t.a);
    vec3 lit = base * (uAmbient + uSunLight * ndl * 0.6) + uSunLight * caustic * 0.35;

    // Fairly see-through, a little more opaque where the ripples are high.
    float alpha = clamp((0.42 + 0.10 * t.a) * uOpacity, 0.0, 1.0);

    // Sun glint: a bright spot where the sun's reflection points at the eye. Only the
    // sun is reflected, never the sky, so the surface stays non-reflective.
    vec3 viewDir = normalize(uEye - vWorld);
    vec3 halfV = normalize(sun + viewDir);
    float glint = pow(max(dot(n, halfV), 0.0), 180.0) * max(sun.y, 0.0);
    lit += uSunLight * glint * 2.0 * uPattern;

    float dist = distance(vWorld, uEye);
    float d = dist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);

    // Far out the sea fades into the horizon colour and becomes opaque, so the edge
    // of the drawn water is never visible and the sea looks endless.
    float far = smoothstep(uFadeStart, uFadeEnd, dist);
    vec3 col = mix(mix(lit, uFogColour, f), uFogColour, far);
    fragColour = vec4(col, mix(alpha, 1.0, far));
}
)GLSL";

// ---------------------------------------------------------------------------
// Mesh helpers
// ---------------------------------------------------------------------------

constexpr int   kLodStep = 4;          // coarse mesh uses every 4th tile
constexpr float kSkirtDrop = 16.0f;    // world units the skirt hangs below the edge
constexpr float kChunkReach = 48.0f;   // half a chunk's diagonal plus margin
// The sea is drawn this far below sea level. Terrain exactly at sea level then sits
// just above the water, so the two never share a surface and do not flicker.
constexpr float kWaterDrop = 0.05f;
// Pushes the water back in the depth buffer, so where land and water are at the
// same height (the shoreline, far away) the land wins instead of flickering.
constexpr float kWaterBiasSlope = 1.0f;
constexpr float kWaterBiasUnits = 4.0f;
// The seabed beyond the map sits this far below the lowest point of the map.
constexpr float kFloorMargin = 2.0f;

// Vertex layout: position (3), normal (3), colour (3).
constexpr int kFloatsPerVertex = 9;

// Grid vertex index of the k-th vertex along edge e (0 = j=0, 1 = i=cells, 2 = j=cells, 3 = i=0).
unsigned EdgeGrid(int e, int k, int cells) {
    const unsigned V = static_cast<unsigned>(cells + 1);
    const unsigned kk = static_cast<unsigned>(k);
    const unsigned c = static_cast<unsigned>(cells);
    switch (e) {
        case 0:  return kk;
        case 1:  return kk * V + c;
        case 2:  return c * V + kk;
        default: return kk * V;
    }
}

// Index list for a mesh with `cells` quads per side: the grid, then a skirt
// around the four edges that hides cracks where chunks use different detail.
std::vector<GLuint> MeshIndices(int cells) {
    const GLuint V = static_cast<GLuint>(cells + 1);
    const GLuint base = V * V;
    std::vector<GLuint> idx;
    idx.reserve(static_cast<std::size_t>(cells) * cells * 6 + static_cast<std::size_t>(cells) * 4 * 6);

    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const GLuint v00 = static_cast<GLuint>(j) * V + static_cast<GLuint>(i);
            const GLuint v10 = v00 + 1;
            const GLuint v01 = v00 + V;
            const GLuint v11 = v01 + 1;
            idx.insert(idx.end(), {v00, v01, v10, v10, v01, v11});
        }
    }

    for (int e = 0; e < 4; ++e) {
        for (int k = 0; k < cells; ++k) {
            const GLuint a = EdgeGrid(e, k, cells);
            const GLuint b = EdgeGrid(e, k + 1, cells);
            const GLuint sa = base + static_cast<GLuint>(e) * V + static_cast<GLuint>(k);
            const GLuint sb = sa + 1;
            idx.insert(idx.end(), {a, b, sb, a, sb, sa});
        }
    }
    return idx;
}

// Vertices for one chunk sampled every `step` tiles, plus the skirt vertices.
// Skirts on the map's border (bit e of `border` set) drop all the way to floorY, so
// the map's edge becomes a wall down to the seabed beyond it, with no gap.
std::vector<float> MeshVertices(const World& w, int ox, int oz, int step, int cells,
                                float floorY, unsigned border) {
    const int n = w.size;
    const int V = cells + 1;

    auto tileIndex = [n](int tx, int tz) {
        const int cx = std::clamp(tx, 0, n - 1);
        const int cz = std::clamp(tz, 0, n - 1);
        return static_cast<std::size_t>(cz) * static_cast<std::size_t>(n) + static_cast<std::size_t>(cx);
    };

    const std::size_t grid = static_cast<std::size_t>(V) * V;
    std::vector<float> out((grid + static_cast<std::size_t>(4) * V) * kFloatsPerVertex);

    for (int j = 0; j < V; ++j) {
        for (int i = 0; i < V; ++i) {
            const int tx = ox + i * step;
            const int tz = oz + j * step;
            const float wx = static_cast<float>(tx) - n * 0.5f;
            const float wz = static_cast<float>(tz) - n * 0.5f;
            const float y = w.height[tileIndex(tx, tz)];

            // Wide stencil: differences over two steps each way, so steep faces get smooth normals.
            const float span = 4.0f * static_cast<float>(step);
            const float dhdx = (w.height[tileIndex(tx + 2 * step, tz)] - w.height[tileIndex(tx - 2 * step, tz)]) / span;
            const float dhdz = (w.height[tileIndex(tx, tz + 2 * step)] - w.height[tileIndex(tx, tz - 2 * step)]) / span;
            const V3 nrm = Normalize(V3{-dhdx, 1.0f, -dhdz});
            V3 col = BiomeColour(static_cast<Biome>(w.biome[tileIndex(tx, tz)]));
            if (w.veg.size() == w.height.size()) {
                // Vegetation pulls the ground toward a green-brown canopy colour.
                const float v = std::clamp(w.veg[tileIndex(tx, tz)], 0.0f, 1.0f) * 0.45f;
                col = V3{col.x + (0.20f - col.x) * v, col.y + (0.34f - col.y) * v, col.z + (0.13f - col.z) * v};
            }

            float* p = out.data() + (static_cast<std::size_t>(j) * V + i) * kFloatsPerVertex;
            p[0] = wx;    p[1] = y;     p[2] = wz;
            p[3] = nrm.x; p[4] = nrm.y; p[5] = nrm.z;
            p[6] = col.x; p[7] = col.y; p[8] = col.z;
        }
    }

    // Skirt: copy each edge vertex and drop it.
    for (int e = 0; e < 4; ++e) {
        for (int k = 0; k < V; ++k) {
            const std::size_t g = EdgeGrid(e, k, cells);
            float* src = out.data() + g * kFloatsPerVertex;
            float* dst = out.data() + (grid + static_cast<std::size_t>(e) * V + k) * kFloatsPerVertex;
            std::copy(src, src + kFloatsPerVertex, dst);
            dst[1] = ((border >> e) & 1u) ? floorY : src[1] - kSkirtDrop;
        }
    }
    return out;
}

void UploadMesh(GLuint& vao, GLuint& vbo, const std::vector<float>& verts, GLuint ibo) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(float)));
    glBindVertexArray(0);
}

// Six planes of the view frustum, from the combined view-projection matrix.
// Each plane is a*x + b*y + c*z + d >= 0 for points inside.
struct Plane { float a, b, c, d; };

void ExtractFrustum(const Mat4& m, Plane out[6]) {
    // Column-major: element (row r, column c) is m[c*4 + r].
    auto make = [&m](int row, float s) {
        return Plane{m.m[0 * 4 + 3] + s * m.m[0 * 4 + row],
                     m.m[1 * 4 + 3] + s * m.m[1 * 4 + row],
                     m.m[2 * 4 + 3] + s * m.m[2 * 4 + row],
                     m.m[3 * 4 + 3] + s * m.m[3 * 4 + row]};
    };
    out[0] = make(0, 1.0f);    // left
    out[1] = make(0, -1.0f);   // right
    out[2] = make(1, 1.0f);    // bottom
    out[3] = make(1, -1.0f);   // top
    out[4] = make(2, 1.0f);    // near
    out[5] = make(2, -1.0f);   // far
}

// True unless the box lies entirely outside one of the planes.
bool BoxInFrustum(const Plane* pl, float x0, float x1, float y0, float y1, float z0, float z1) {
    for (int i = 0; i < 6; ++i) {
        const Plane& p = pl[i];
        // The corner furthest along the plane normal decides.
        const float px = p.a >= 0.0f ? x1 : x0;
        const float py = p.b >= 0.0f ? y1 : y0;
        const float pz = p.c >= 0.0f ? z1 : z0;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) return false;
    }
    return true;
}

GLuint UploadIndices(const std::vector<GLuint>& idx) {
    GLuint ibo = 0;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    return ibo;
}

}  // namespace

// ---------------------------------------------------------------------------
// Setup and teardown
// ---------------------------------------------------------------------------

bool Renderer::InitTerrain(std::string& err) {
    terrainProg_ = BuildProgram(kTerrainVS, kTerrainFS, err);
    if (!terrainProg_) return false;
    terr_.viewProj = glGetUniformLocation(terrainProg_, "uViewProj");
    terr_.eye = glGetUniformLocation(terrainProg_, "uEye");
    terr_.sunDir = glGetUniformLocation(terrainProg_, "uSunDir");
    terr_.sunLight = glGetUniformLocation(terrainProg_, "uSunLight");
    terr_.ambient = glGetUniformLocation(terrainProg_, "uAmbient");
    terr_.fogColour = glGetUniformLocation(terrainProg_, "uFogColour");
    terr_.fog = glGetUniformLocation(terrainProg_, "uFog");
    terr_.tile = glGetUniformLocation(terrainProg_, "uTile");
    terr_.seaY = glGetUniformLocation(terrainProg_, "uSeaY");
    terr_.tint = glGetUniformLocation(terrainProg_, "uTint");
    terr_.rockRgb = glGetUniformLocation(terrainProg_, "uRockRgb");
    terr_.rockSlope = glGetUniformLocation(terrainProg_, "uRockSlope");
    terr_.slopeView = glGetUniformLocation(terrainProg_, "uSlopeView");
    terr_.underwater = glGetUniformLocation(terrainProg_, "uUnderwater");

    // Water: the vertex shader's corner table is appended to the source.
    waterProg_ = BuildProgram((std::string(kWaterVS) + kQuadCorners + R"GLSL(
void main() {
    vec2 c = kCorner[gl_VertexID];
    vec3 p = vec3(uCentre.x + c.x * uHalf, uSeaY, uCentre.z + c.y * uHalf);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL").c_str(), kWaterFS, err);
    if (!waterProg_) return false;
    water_.viewProj = glGetUniformLocation(waterProg_, "uViewProj");
    water_.eye = glGetUniformLocation(waterProg_, "uEye");
    water_.centre = glGetUniformLocation(waterProg_, "uCentre");
    water_.half = glGetUniformLocation(waterProg_, "uHalf");
    water_.seaY = glGetUniformLocation(waterProg_, "uSeaY");
    water_.sunDir = glGetUniformLocation(waterProg_, "uSunDir");
    water_.sunLight = glGetUniformLocation(waterProg_, "uSunLight");
    water_.ambient = glGetUniformLocation(waterProg_, "uAmbient");
    water_.colour = glGetUniformLocation(waterProg_, "uColour");
    water_.horizon = glGetUniformLocation(waterProg_, "uFogColour");
    water_.fog = glGetUniformLocation(waterProg_, "uFog");
    water_.time = glGetUniformLocation(waterProg_, "uTime");
    water_.tile = glGetUniformLocation(waterProg_, "uTile");
    water_.fadeStart = glGetUniformLocation(waterProg_, "uFadeStart");
    water_.fadeEnd = glGetUniformLocation(waterProg_, "uFadeEnd");
    water_.opacity = glGetUniformLocation(waterProg_, "uOpacity");
    water_.pattern = glGetUniformLocation(waterProg_, "uPattern");
    water_.speed = glGetUniformLocation(waterProg_, "uSpeed");

    floorProg_ = BuildProgram(kFloorVS, kFloorFS, err);
    if (!floorProg_) return false;
    floor_.viewProj = glGetUniformLocation(floorProg_, "uViewProj");
    floor_.eye = glGetUniformLocation(floorProg_, "uEye");
    floor_.centre = glGetUniformLocation(floorProg_, "uCentre");
    floor_.half = glGetUniformLocation(floorProg_, "uHalf");
    floor_.ambient = glGetUniformLocation(floorProg_, "uAmbient");
    floor_.sunLight = glGetUniformLocation(floorProg_, "uSunLight");
    floor_.fogColour = glGetUniformLocation(floorProg_, "uFogColour");
    floor_.underwater = glGetUniformLocation(floorProg_, "uUnderwater");
    floor_.fog = glGetUniformLocation(floorProg_, "uFog");

    groundTex_ = CreateSurfaceTile(1234u, 12, true);   // sharp grain for the ground
    waterTex_ = CreateSurfaceTile(777u, 6, false);     // smooth rounded ripples for the sea
    if (!groundTex_ || !waterTex_) {
        err = "could not create surface textures";
        return false;
    }

    glGenVertexArrays(1, &emptyVao_);
    return true;
}

void Renderer::ShutdownTerrain() {
    ClearTerrain();
    glDeleteBuffers(1, &terrainIbo_);
    glDeleteBuffers(1, &terrainIboCoarse_);
    glDeleteProgram(terrainProg_);
    glDeleteProgram(waterProg_);
    glDeleteProgram(floorProg_);
    floorProg_ = 0;
    glDeleteTextures(1, &groundTex_);
    glDeleteTextures(1, &waterTex_);
    groundTex_ = waterTex_ = 0;
    glDeleteVertexArrays(1, &emptyVao_);
    terrainIbo_ = terrainIboCoarse_ = 0;
    terrainProg_ = waterProg_ = 0;
    emptyVao_ = 0;
    terrainIndexFull_ = terrainIndexCoarse_ = 0;
}

void Renderer::ClearTerrain() {
    for (TerrainChunk& ch : chunks_) {
        glDeleteBuffers(1, &ch.vboFull);
        glDeleteVertexArrays(1, &ch.vaoFull);
        glDeleteBuffers(1, &ch.vboCoarse);
        glDeleteVertexArrays(1, &ch.vaoCoarse);
    }
    chunks_.clear();
    glDeleteBuffers(1, &apronVbo_);
    glDeleteBuffers(1, &apronIbo_);
    glDeleteVertexArrays(1, &apronVao_);
    apronVao_ = apronVbo_ = apronIbo_ = 0;
    apronCount_ = 0;
}

// ---------------------------------------------------------------------------
// Seabed apron. The seabed continues outward from the map's edge: it starts at the
// edge's own heights and colours and sinks to the floor over kApronWidth. It uses
// the terrain shader, so the edge has no visible seam, and the flat floor takes
// over where the apron ends.
// ---------------------------------------------------------------------------

constexpr float kApronWidth = 128.0f;
constexpr float kApronStep = 4.0f;   // along the edge; the same spacing as the coarse mesh
constexpr float kApronRadii[] = {0.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f};

struct ApronMesh {
    std::vector<float> verts;
    std::vector<GLuint> idx;
};

ApronMesh BuildApronMesh(const World& w, float floorY) {
    const int n = w.size;
    const float lo = -n * 0.5f;        // world x (and z) of the first tile
    const float hi = n * 0.5f - 1.0f;  // ... of the last tile
    constexpr int R = static_cast<int>(sizeof(kApronRadii) / sizeof(kApronRadii[0]));

    // Positions along each side, from one corner extension to the other.
    std::vector<float> ts;
    for (float t = lo - kApronWidth; t < hi + kApronWidth; t += kApronStep) ts.push_back(t);
    ts.push_back(hi);
    ts.push_back(hi + kApronWidth);
    std::sort(ts.begin(), ts.end());
    ts.erase(std::unique(ts.begin(), ts.end()), ts.end());
    const int T = static_cast<int>(ts.size());

    auto tileIndex = [n](float x, float z) {
        const int tx = std::clamp(static_cast<int>(std::lround(x + n * 0.5f)), 0, n - 1);
        const int tz = std::clamp(static_cast<int>(std::lround(z + n * 0.5f)), 0, n - 1);
        return static_cast<std::size_t>(tz) * static_cast<std::size_t>(n) + static_cast<std::size_t>(tx);
    };

    // Height and colour of the apron at (x, z): the edge's own values, sinking outward.
    auto surface = [&](float x, float z, float& y, V3& col) {
        const float cx = std::clamp(x, lo, hi);
        const float cz = std::clamp(z, lo, hi);
        const float dx = x - cx;
        const float dz = z - cz;
        const float dOut = std::sqrt(dx * dx + dz * dz);
        const std::size_t idx = tileIndex(cx, cz);
        const float edgeY = w.height[idx];
        const float k = std::clamp(dOut / kApronWidth, 0.0f, 1.0f);
        y = edgeY + (floorY - edgeY) * (k * k * (3.0f - 2.0f * k));
        col = BiomeColour(static_cast<Biome>(w.biome[idx]));
    };

    ApronMesh m;
    for (int e = 0; e < 4; ++e) {   // north, south, west, east
        std::vector<V3> pos(static_cast<std::size_t>(T) * R);
        std::vector<V3> col(static_cast<std::size_t>(T) * R);
        for (int ti = 0; ti < T; ++ti) {
            for (int ri = 0; ri < R; ++ri) {
                const float t = ts[ti];
                const float r = kApronRadii[ri];
                float x = t, z = t;
                switch (e) {
                    case 0: z = lo - r; break;
                    case 1: z = hi + r; break;
                    case 2: x = lo - r; break;
                    default: x = hi + r; break;
                }
                float y = 0.0f;
                V3 c{};
                surface(x, z, y, c);
                const std::size_t g = static_cast<std::size_t>(ti) * R + ri;
                pos[g] = {x, y, z};
                col[g] = c;
            }
        }

        const GLuint base = static_cast<GLuint>(m.verts.size() / kFloatsPerVertex);
        auto P = [&](int ti, int ri) {
            ti = std::clamp(ti, 0, T - 1);
            ri = std::clamp(ri, 0, R - 1);
            return pos[static_cast<std::size_t>(ti) * R + ri];
        };
        for (int ti = 0; ti < T; ++ti) {
            for (int ri = 0; ri < R; ++ri) {
                // Normal from the surface's slope along the edge and outward.
                V3 nrm = Normalize(Cross(P(ti + 1, ri) - P(ti - 1, ri), P(ti, ri + 1) - P(ti, ri - 1)));
                if (nrm.y < 0.0f) nrm = nrm * -1.0f;
                const std::size_t g = static_cast<std::size_t>(ti) * R + ri;
                const V3 p = pos[g];
                const V3 c = col[g];
                m.verts.insert(m.verts.end(), {p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, c.x, c.y, c.z});
            }
        }
        for (int ti = 0; ti + 1 < T; ++ti) {
            for (int ri = 0; ri + 1 < R; ++ri) {
                const GLuint a = base + static_cast<GLuint>(ti * R + ri);
                const GLuint b = base + static_cast<GLuint>((ti + 1) * R + ri);
                const GLuint c = base + static_cast<GLuint>((ti + 1) * R + ri + 1);
                const GLuint d = base + static_cast<GLuint>(ti * R + ri + 1);
                m.idx.insert(m.idx.end(), {a, b, c, a, c, d});
            }
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------

void Renderer::BuildTerrain(const World& w) {
    ClearTerrain();
    seaY_ = w.seaLevel;

    constexpr int C = kChunkTiles;
    const int n = w.size;
    if (n < C || n % C != 0 || w.height.empty()) return;

    // Index buffers depend only on the mesh size, so rebuild them each time.
    glDeleteBuffers(1, &terrainIbo_);
    glDeleteBuffers(1, &terrainIboCoarse_);
    const std::vector<GLuint> fullIdx = MeshIndices(C);
    const std::vector<GLuint> coarseIdx = MeshIndices(C / kLodStep);
    terrainIbo_ = UploadIndices(fullIdx);
    terrainIboCoarse_ = UploadIndices(coarseIdx);
    terrainIndexFull_ = static_cast<GLsizei>(fullIdx.size());
    terrainIndexCoarse_ = static_cast<GLsizei>(coarseIdx.size());

    // The seabed beyond the map lies below the lowest point of the map, so the map's
    // border walls can drop to it and nothing sits between them and the floor.
    floorY_ = *std::min_element(w.height.begin(), w.height.end()) - kFloorMargin;

    const int perSide = n / C;
    chunks_.reserve(static_cast<std::size_t>(perSide) * perSide);

    for (int cz = 0; cz < perSide; ++cz) {
        for (int cx = 0; cx < perSide; ++cx) {
            const int ox = cx * C;
            const int oz = cz * C;

            TerrainChunk ch;
            // Which edges of this chunk lie on the map's border (bit order as EdgeGrid).
            unsigned border = 0;
            if (cz == 0)            border |= 1u << 0;
            if (cx == perSide - 1)  border |= 1u << 1;
            if (cz == perSide - 1)  border |= 1u << 2;
            if (cx == 0)            border |= 1u << 3;

            const std::vector<float> full = MeshVertices(w, ox, oz, 1, C, floorY_, border);
            UploadMesh(ch.vaoFull, ch.vboFull, full, terrainIbo_);

            // Height range for frustum culling. Index 1 of each vertex is y.
            ch.minY = ch.maxY = full.empty() ? 0.0f : full[1];
            for (std::size_t i = 1; i < full.size(); i += kFloatsPerVertex) {
                ch.minY = std::min(ch.minY, full[i]);
                ch.maxY = std::max(ch.maxY, full[i]);
            }

            const std::vector<float> coarse = MeshVertices(w, ox, oz, kLodStep, C / kLodStep, floorY_, border);
            UploadMesh(ch.vaoCoarse, ch.vboCoarse, coarse, terrainIboCoarse_);

            ch.cx = static_cast<float>(ox) + C * 0.5f - n * 0.5f;
            ch.cz = static_cast<float>(oz) + C * 0.5f - n * 0.5f;
            chunks_.push_back(ch);
        }
    }

    const ApronMesh apron = BuildApronMesh(w, floorY_);
    glGenBuffers(1, &apronIbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, apronIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, apron.idx.size() * sizeof(GLuint), apron.idx.data(), GL_STATIC_DRAW);
    UploadMesh(apronVao_, apronVbo_, apron.verts, apronIbo_);
    apronCount_ = static_cast<GLsizei>(apron.idx.size());
    apronMin_ = -n * 0.5f - kApronWidth;
    apronMax_ = n * 0.5f - 1.0f + kApronWidth;
}

void Renderer::DrawTerrain(const Camera& cam, const SkyState& sky, float viewDist, float lodDist) {
    if (chunks_.empty()) return;

    const float reach = viewDist + kChunkReach;
    const float reach2 = reach * reach;
    const float lod2 = lodDist * lodDist;

    Plane frustum[6];
    if (gOpt.cullChunks) ExtractFrustum(cam.viewProj, frustum);

    glEnable(GL_DEPTH_TEST);
    glUseProgram(terrainProg_);
    glUniformMatrix4fv(terr_.viewProj, 1, GL_FALSE, cam.viewProj.m);
    glUniform3f(terr_.eye, cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform3f(terr_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(terr_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(terr_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(terr_.fogColour, sky.fogColour.x, sky.fogColour.y, sky.fogColour.z);
    glUniform3f(terr_.underwater, sky.underwaterRgb.x, sky.underwaterRgb.y, sky.underwaterRgb.z);
    glUniform1f(terr_.fog, sky.fog);
    glUniform3f(terr_.tint, 1.0f, 1.0f, 1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, gDebug.wireframe ? GL_LINE : GL_FILL);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, groundTex_);
    glUniform1i(terr_.tile, 0);
    glUniform1f(terr_.seaY, seaY_);
    glUniform3f(terr_.rockRgb, sky.rockRgb.x, sky.rockRgb.y, sky.rockRgb.z);
    glUniform1f(terr_.rockSlope, sky.rockSlope);
    glUniform1f(terr_.slopeView, gDebug.slopeView ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);

    constexpr float kHalf = kChunkTiles * 0.5f;
    for (const TerrainChunk& ch : chunks_) {
        const float dx = ch.cx - cam.eye.x;
        const float dz = ch.cz - cam.eye.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 > reach2) continue;

        if (gOpt.cullChunks &&
            !BoxInFrustum(frustum, ch.cx - kHalf, ch.cx + kHalf, ch.minY, ch.maxY,
                          ch.cz - kHalf, ch.cz + kHalf)) {
            continue;
        }

        const bool isNear = d2 < lod2;
        if (gDebug.lodTint) {
            // Debug: full detail in green, the coarse mesh in orange.
            if (isNear) glUniform3f(terr_.tint, 0.55f, 1.0f, 0.55f);
            else        glUniform3f(terr_.tint, 1.0f, 0.65f, 0.4f);
        }
        glBindVertexArray(isNear ? ch.vaoFull : ch.vaoCoarse);
        ++gStats.chunks;
        ++gStats.drawCalls;
        gStats.triangles += (isNear ? terrainIndexFull_ : terrainIndexCoarse_) / 3;
        glDrawElements(GL_TRIANGLES, isNear ? terrainIndexFull_ : terrainIndexCoarse_, GL_UNSIGNED_INT, nullptr);
    }
    // The apron, if the eye is close enough to see it.
    if (apronCount_ > 0) {
        const float dx = std::max({apronMin_ - cam.eye.x, 0.0f, cam.eye.x - apronMax_});
        const float dz = std::max({apronMin_ - cam.eye.z, 0.0f, cam.eye.z - apronMax_});
        if (dx * dx + dz * dz <= reach2) {
            glUniform3f(terr_.tint, 1.0f, 1.0f, 1.0f);
            glBindVertexArray(apronVao_);
            ++gStats.drawCalls;
            gStats.triangles += apronCount_ / 3;
            glDrawElements(GL_TRIANGLES, apronCount_, GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// ---------------------------------------------------------------------------
// Sea and seabed
// ---------------------------------------------------------------------------

void Renderer::DrawFloor(const Camera& cam, const SkyState& sky) {
    glEnable(GL_DEPTH_TEST);
    glUseProgram(floorProg_);
    glUniformMatrix4fv(floor_.viewProj, 1, GL_FALSE, cam.viewProj.m);
    glUniform3f(floor_.eye, cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform3f(floor_.centre, cam.eye.x, floorY_, cam.eye.z);
    glUniform1f(floor_.half, cam.farPlane * 1.2f);
    glUniform3f(floor_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(floor_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(floor_.fogColour, sky.fogColour.x, sky.fogColour.y, sky.fogColour.z);
    glUniform3f(floor_.underwater, sky.underwaterRgb.x, sky.underwaterRgb.y, sky.underwaterRgb.z);
    glUniform1f(floor_.fog, sky.fog);

    glBindVertexArray(emptyVao_);
    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Renderer::DrawWater(const Camera& cam, const SkyState& sky, float seaY, float viewDist) {
    const float surfaceY = seaY - kWaterDrop;

    // The sea is drawn past the far plane, and fades to the horizon colour before the
    // far plane clips it, so no edge of the water can be seen.
    const float fadeEnd = std::min(viewDist, cam.farPlane * 0.9f);
    const float fadeStart = fadeEnd * 0.6f;

    glEnable(GL_DEPTH_TEST);
    glUseProgram(waterProg_);
    glUniformMatrix4fv(water_.viewProj, 1, GL_FALSE, cam.viewProj.m);
    glUniform3f(water_.eye, cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform3f(water_.centre, cam.eye.x, surfaceY, cam.eye.z);
    glUniform1f(water_.half, cam.farPlane * 1.2f);
    glUniform1f(water_.seaY, surfaceY);
    glUniform1f(water_.fadeStart, fadeStart);
    glUniform1f(water_.fadeEnd, fadeEnd);
    glUniform3f(water_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(water_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(water_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(water_.colour, sky.waterRgb.x, sky.waterRgb.y, sky.waterRgb.z);
    glUniform3f(water_.horizon, sky.fogColour.x, sky.fogColour.y, sky.fogColour.z);
    glUniform1f(water_.fog, sky.fog);
    glUniform1f(water_.time, sky.waterTime);
    glUniform1f(water_.opacity, sky.waterOpacity);
    glUniform1f(water_.pattern, sky.waterPattern);
    glUniform1f(water_.speed, sky.waterSpeed);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, waterTex_);
    glUniform1i(water_.tile, 0);

    // Water is see-through, so it blends over the seabed and does not write depth.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(kWaterBiasSlope, kWaterBiasUnits);
    glBindVertexArray(emptyVao_);
    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}  // namespace wm
