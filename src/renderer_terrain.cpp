// Terrain, sea and seabed. The ground and the sea are shaded as the world tool shades them: the same
// shaders, the same surface tiles and the same look, which the world folder stores. Split from renderer.cpp
// to keep both files readable.
#include "renderer.h"
#include "render/surface_pixels.hpp"
#include "theme/theme.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Shaders (GLSL 3.30 core), ported from the world tool. The names match the world tool's, so the two
// can be compared line by line. Heights in the game are scaled by heightScale, so the ground takes
// a depth scale to measure the water in the world's units.
// ---------------------------------------------------------------------------

const char* const kGroundVS = R"GLSL(
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

const char* const kGroundFS = R"GLSL(
#version 330 core
uniform sampler2D uTile;
uniform float uSeaY;
uniform float uDepthScale;
uniform vec3 uRockRgb;
uniform float uRockSlope;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform vec3 uUnderwater;
uniform vec3 uFogColor;
uniform float uFog;
in vec3 vNrm;
in vec3 vCol;
in float vDist;
in vec2 vXZ;
in vec3 vPos;
out vec4 fragColor;
void main() {
    // Ground detail from the tile: its normal tilts the lighting, and its height
    // darkens the color a little. The tile repeats every 8 world units.
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

    // Under the sea, the water between the eye and the ground absorbs light and tints
    // it. The deeper the ground, the more of the water it has been seen through.
    float depth = max(uSeaY - vPos.y, 0.0) * uDepthScale;
    float absorb = 1.0 - exp(-depth * 0.25);
    vec3 underwater = uUnderwater * (uAmbient + uSunLight * 0.5);
    lit = mix(lit, underwater, absorb);

    float d = vDist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColor = vec4(mix(lit, uFogColor, f), 1.0);
}
)GLSL";

// Sea and seabed build their quads in the vertex shader from gl_VertexID, so they need no
// vertex buffer, only an empty VAO.
const char* const kSeaVS = R"GLSL(
#version 330 core
uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uHalf;
out vec3 vWorld;
const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 c = kCorner[gl_VertexID];
    vec3 p = vec3(uCenter.x + c.x * uHalf, uCenter.y, uCenter.z + c.y * uHalf);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL";

const char* const kSeaFS = R"GLSL(
#version 330 core
in vec3 vWorld;
uniform vec3 uEye;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform vec3 uColor;
uniform vec3 uFogColor;
uniform float uFog;
uniform float uTime;
uniform float uOpacity;
uniform float uPattern;
uniform float uSpeed;
uniform float uFadeStart;
uniform float uFadeEnd;
uniform sampler2D uTile;
out vec4 fragColor;
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

    vec3 base = uColor * (0.85 + 0.3 * t.a);
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

    // Far out the sea fades into the horizon color and becomes opaque, so the edge
    // of the drawn water is never visible and the sea looks endless.
    float far = smoothstep(uFadeStart, uFadeEnd, dist);
    vec3 col = mix(mix(lit, uFogColor, f), uFogColor, far);
    fragColor = vec4(col, mix(alpha, 1.0, far));
}
)GLSL";

// Seabed beyond the map: a flat plane at the floor. Seen through the water it is just the
// underwater tint, so one flat color is enough.
const char* const kFloorVS = R"GLSL(
#version 330 core
uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uHalf;
out vec3 vWorld;
const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 c = kCorner[gl_VertexID];
    vec3 p = vec3(uCenter.x + c.x * uHalf, uCenter.y, uCenter.z + c.y * uHalf);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL";

const char* const kFloorFS = R"GLSL(
#version 330 core
in vec3 vWorld;
uniform vec3 uEye;
uniform vec3 uAmbient;
uniform vec3 uSunLight;
uniform vec3 uUnderwater;
uniform vec3 uFogColor;
uniform float uFog;
out vec4 fragColor;
void main() {
    vec3 lit = uUnderwater * (uAmbient + uSunLight * 0.5);
    float d = distance(vWorld, uEye) * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColor = vec4(mix(lit, uFogColor, f), 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Constants and helpers
// ---------------------------------------------------------------------------

constexpr float LOD_NEAR_TILES = 96.0f;   // chunks closer than this are drawn at full detail
constexpr float LOD_MID_TILES = 224.0f;   // closer than this, at half detail; beyond, a quarter
// The sea sits this far below sea level, in the world's units, so terrain exactly at sea level
// stays just above it.
constexpr float kWaterDrop = 0.05f;
// Pushes the sea back in the depth buffer, so where land and sea meet, the land wins. The slope term
// stays zero: on the sea's huge grazing quad it grew until the sea sank behind the seabed and the far
// fog, leaving a dark band at the horizon. The units term alone is enough for the shore.
constexpr float kWaterBiasSlope = 0.0f;
constexpr float kWaterBiasUnits = 4.0f;
// The far plane of the game's camera (camera.h)
constexpr float kFarPlane = 4000.0f;
// Chunk meshes built per frame, so a newly loaded area fills in over a few frames
constexpr int MAX_BUILDS_PER_FRAME = 8;

// The six frustum planes of a column-major view-projection matrix, as (a, b, c, d) with a*x+b*y+c*z+d >= 0 inside
struct FrustumPlanes {
    float p[6][4];
};

FrustumPlanes framePlanes(const Mat4& m) {
    // Element (row r, column c) is m[c * 4 + r]
    auto el = [&](int r, int c) { return m.m[c * 4 + r]; };
    FrustumPlanes f{};
    for (int k = 0; k < 3; ++k) {
        for (int s = 0; s < 2; ++s) {
            const float sign = s == 0 ? 1.0f : -1.0f;
            for (int c = 0; c < 4; ++c) f.p[2 * k + s][c] = el(3, c) + sign * el(k, c);
        }
    }
    return f;
}

// True unless the chunk's box is entirely outside one of the planes
bool chunkVisible(const FrustumPlanes& f, const ChunkBounds& b) {
    for (const auto& p : f.p) {
        const float px = p[0] >= 0.0f ? b.x1 : b.x0;
        const float py = p[1] >= 0.0f ? b.maxY : b.minY;
        const float pz = p[2] >= 0.0f ? b.z1 : b.z0;
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f) return false;
    }
    return true;
}

// Compiles and links one shader program. Returns 0 and reports the log if it fails.
GLuint buildProgram(const char* vsSrc, const char* fsSrc) {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "terrain shader failed to compile: %s\n", log);
            glDeleteShader(s);
            return GLuint{ 0 };
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
    const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fsSrc) : 0;
    if (!fs) {
        if (vs) glDeleteShader(vs);
        return 0;
    }
    const GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "terrain shader failed to link: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// One surface tile as a repeating, mipmapped texture
GLuint makeSurfaceTexture(const std::vector<uint8_t>& pixels) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wm::SURFACE_TILE_SIZE, wm::SURFACE_TILE_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Seconds since the game started, for the water's motion
float SecondsSinceStart() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

// ---------------------------------------------------------------------------
// Setup and teardown
// ---------------------------------------------------------------------------

bool Renderer::initTerrainGl() {
    groundShader_.prog = buildProgram(kGroundVS, kGroundFS);
    if (!groundShader_.prog) return false;
    GLuint g = groundShader_.prog;
    groundShader_.viewProj = glGetUniformLocation(g, "uViewProj");
    groundShader_.eye = glGetUniformLocation(g, "uEye");
    groundShader_.seaY = glGetUniformLocation(g, "uSeaY");
    groundShader_.depthScale = glGetUniformLocation(g, "uDepthScale");
    groundShader_.rockRgb = glGetUniformLocation(g, "uRockRgb");
    groundShader_.rockSlope = glGetUniformLocation(g, "uRockSlope");
    groundShader_.sunDir = glGetUniformLocation(g, "uSunDir");
    groundShader_.sunLight = glGetUniformLocation(g, "uSunLight");
    groundShader_.ambient = glGetUniformLocation(g, "uAmbient");
    groundShader_.underwater = glGetUniformLocation(g, "uUnderwater");
    groundShader_.fogColor = glGetUniformLocation(g, "uFogColor");
    groundShader_.fog = glGetUniformLocation(g, "uFog");
    groundShader_.tile = glGetUniformLocation(g, "uTile");

    seaShader_.prog = buildProgram(kSeaVS, kSeaFS);
    if (!seaShader_.prog) return false;
    GLuint s = seaShader_.prog;
    seaShader_.viewProj = glGetUniformLocation(s, "uViewProj");
    seaShader_.center = glGetUniformLocation(s, "uCenter");
    seaShader_.half = glGetUniformLocation(s, "uHalf");
    seaShader_.eye = glGetUniformLocation(s, "uEye");
    seaShader_.sunDir = glGetUniformLocation(s, "uSunDir");
    seaShader_.sunLight = glGetUniformLocation(s, "uSunLight");
    seaShader_.ambient = glGetUniformLocation(s, "uAmbient");
    seaShader_.color = glGetUniformLocation(s, "uColor");
    seaShader_.fogColor = glGetUniformLocation(s, "uFogColor");
    seaShader_.fog = glGetUniformLocation(s, "uFog");
    seaShader_.time = glGetUniformLocation(s, "uTime");
    seaShader_.opacity = glGetUniformLocation(s, "uOpacity");
    seaShader_.pattern = glGetUniformLocation(s, "uPattern");
    seaShader_.speed = glGetUniformLocation(s, "uSpeed");
    seaShader_.fadeStart = glGetUniformLocation(s, "uFadeStart");
    seaShader_.fadeEnd = glGetUniformLocation(s, "uFadeEnd");
    seaShader_.tile = glGetUniformLocation(s, "uTile");

    floorShader_.prog = buildProgram(kFloorVS, kFloorFS);
    if (!floorShader_.prog) return false;
    GLuint f = floorShader_.prog;
    floorShader_.viewProj = glGetUniformLocation(f, "uViewProj");
    floorShader_.center = glGetUniformLocation(f, "uCenter");
    floorShader_.half = glGetUniformLocation(f, "uHalf");
    floorShader_.eye = glGetUniformLocation(f, "uEye");
    floorShader_.ambient = glGetUniformLocation(f, "uAmbient");
    floorShader_.sunLight = glGetUniformLocation(f, "uSunLight");
    floorShader_.underwater = glGetUniformLocation(f, "uUnderwater");
    floorShader_.fogColor = glGetUniformLocation(f, "uFogColor");
    floorShader_.fog = glGetUniformLocation(f, "uFog");

    glGenVertexArrays(1, &emptyVao_);
    return true;
}

void Renderer::releaseTerrainGl() {
    releaseTerrain();
    glDeleteProgram(groundShader_.prog);
    glDeleteProgram(seaShader_.prog);
    glDeleteProgram(floorShader_.prog);
    glDeleteTextures(1, &groundTex_);
    glDeleteTextures(1, &seaTex_);
    glDeleteVertexArrays(1, &emptyVao_);
    groundShader_ = TerrainShader{};
    seaShader_ = SeaShader{};
    floorShader_ = FloorShader{};
    groundTex_ = seaTex_ = 0;
    emptyVao_ = 0;
    surfaceVersion_ = 0;
}

void Renderer::uploadSurfaceTiles(const Terrain& t) {
    glDeleteTextures(1, &groundTex_);
    glDeleteTextures(1, &seaTex_);
    groundTex_ = makeSurfaceTexture(t.surface().ground);
    seaTex_ = makeSurfaceTexture(t.surface().water);
    surfaceVersion_ = t.surfaceVersion();
}

// ---------------------------------------------------------------------------
// Chunks
// ---------------------------------------------------------------------------

void Renderer::releaseChunk(TerrainChunk& c) {
    if (c.ebo) glDeleteBuffers(1, &c.ebo);
    if (c.vbo) glDeleteBuffers(1, &c.vbo);
    if (c.vao) glDeleteVertexArrays(1, &c.vao);
    c = TerrainChunk{};
}

void Renderer::releaseTerrain() {
    for (TerrainChunk& c : chunks_) releaseChunk(c);
    chunks_.clear();
    terrainSize_ = 0;
    chunksPerSide_ = 0;
}

void Renderer::uploadTerrain(const Terrain& t) {
    releaseTerrain();
    terrainSize_ = t.size();
    chunksPerSide_ = t.chunksPerSide();
    // Slots start empty. Each chunk gets its buffers the first time it is built.
    chunks_.resize((size_t)chunksPerSide_ * chunksPerSide_);
}

// Builds a chunk's mesh at a detail step and uploads it into the chunk's own buffers
void Renderer::buildChunk(const Terrain& t, const ChunkData& c, int id, int step) {
    TerrainChunk& slot = chunks_[(size_t)id];
    if (!slot.vao) {
        glGenVertexArrays(1, &slot.vao);
        glGenBuffers(1, &slot.vbo);
        glGenBuffers(1, &slot.ebo);
        glBindVertexArray(slot.vao);
        glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
        const GLsizei stride = sizeof(TerrainVertex);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.ebo);
        glBindVertexArray(0);
    }
    buildChunkMesh(t, c, step, chunkScratch_);
    glBindVertexArray(slot.vao);
    glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
    glBufferData(GL_ARRAY_BUFFER, chunkScratch_.verts.size() * sizeof(TerrainVertex),
                 chunkScratch_.verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunkScratch_.idx.size() * sizeof(uint32_t),
                 chunkScratch_.idx.data(), GL_DYNAMIC_DRAW);
    slot.indexCount = (GLsizei)chunkScratch_.idx.size();
    slot.step = step;
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Draws the seabed, the loaded chunks in view at the detail their distance needs, then the sea.
void Renderer::drawTerrain(const Terrain& t, const Camera& cam) {
    if (t.size() != terrainSize_ || chunks_.empty()) uploadTerrain(t);
    if (t.surfaceVersion() != surfaceVersion_) uploadSurfaceTiles(t);

    // The light and look the ground and sea are shaded with: the world's look, lit by the sun the sky shows
    const wm::SkyState sky = wm::ComputeSkyForSun(t.surface().look, wm::V3{ skySun_[0], skySun_[1], skySun_[2] });
    const float hs = t.heightScale();
    const float seaY = 0.0f;                        // sea level, in stored units
    const float waterY = seaY - kWaterDrop * hs;    // the sea's surface, just under sea level
    float ex, ey, ez;
    cam.eye(ex, ey, ez);

    glEnable(GL_DEPTH_TEST);

    // Seabed beyond the map, seen through the water
    glUseProgram(floorShader_.prog);
    glUniformMatrix4fv(floorShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(floorShader_.eye, ex, ey, ez);
    glUniform3f(floorShader_.center, ex, t.floorY(), ez);
    glUniform1f(floorShader_.half, kFarPlane * 1.2f);
    glUniform3f(floorShader_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(floorShader_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(floorShader_.underwater, sky.underwaterRgb.x, sky.underwaterRgb.y, sky.underwaterRgb.z);
    glUniform3f(floorShader_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(floorShader_.fog, sky.fog);
    // The seabed writes no depth. It lies under the sea, and the sea's polygon offset, which grows on
    // a grazing quad this large, would push the sea back behind it at mid distances, so the seabed
    // showed through as a dark band at the horizon. Terrain is always above the seabed and draws after it.
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);

    // Ground chunks
    glUseProgram(groundShader_.prog);
    glUniformMatrix4fv(groundShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(groundShader_.eye, ex, ey, ez);
    glUniform3f(groundShader_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(groundShader_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(groundShader_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(groundShader_.underwater, sky.underwaterRgb.x, sky.underwaterRgb.y, sky.underwaterRgb.z);
    glUniform3f(groundShader_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(groundShader_.fog, sky.fog);
    glUniform3f(groundShader_.rockRgb, sky.rockRgb.x, sky.rockRgb.y, sky.rockRgb.z);
    glUniform1f(groundShader_.rockSlope, sky.rockSlope);
    glUniform1f(groundShader_.seaY, seaY);
    glUniform1f(groundShader_.depthScale, 1.0f / hs);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, groundTex_);
    glUniform1i(groundShader_.tile, 0);

    const FrustumPlanes planes = framePlanes(viewProj_);
    int builds = 0;
    for (int id = 0; id < (int)chunks_.size(); ++id) {
        TerrainChunk& slot = chunks_[(size_t)id];
        const ChunkData* c = t.chunkAt(id % chunksPerSide_, id / chunksPerSide_);
        if (!c) {
            // Not loaded: free its buffers. They are made again if the chunk comes back.
            if (slot.vao) releaseChunk(slot);
            continue;
        }
        if (slot.version != c->version) {
            slot.version = c->version;
            slot.bounds = chunkBounds(t, *c);
            slot.step = 0;
        }
        if (!chunkVisible(planes, slot.bounds)) continue;

        const float mx = 0.5f * (slot.bounds.x0 + slot.bounds.x1);
        const float my = 0.5f * (slot.bounds.minY + slot.bounds.maxY);
        const float mz = 0.5f * (slot.bounds.z0 + slot.bounds.z1);
        const float dx = mx - ex, dy = my - ey, dz = mz - ez;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const int step = dist < LOD_NEAR_TILES ? 1 : (dist < LOD_MID_TILES ? 2 : 4);
        if (slot.step != step && builds < MAX_BUILDS_PER_FRAME) {
            buildChunk(t, *c, id, step);
            ++builds;
        }
        if (slot.step == 0) continue;   // not built yet, drawn once it is
        glBindVertexArray(slot.vao);
        glDrawElements(GL_TRIANGLES, slot.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);

    // The sea: a translucent plane over everything, so the seabed shows through it
    const float fadeEnd = kFarPlane * 0.9f;
    const float fadeStart = fadeEnd * 0.6f;
    glUseProgram(seaShader_.prog);
    glUniformMatrix4fv(seaShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(seaShader_.center, ex, waterY, ez);
    glUniform1f(seaShader_.half, kFarPlane * 1.2f);
    glUniform3f(seaShader_.eye, ex, ey, ez);
    glUniform3f(seaShader_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(seaShader_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(seaShader_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(seaShader_.color, sky.waterRgb.x, sky.waterRgb.y, sky.waterRgb.z);
    glUniform3f(seaShader_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(seaShader_.fog, sky.fog);
    glUniform1f(seaShader_.time, SecondsSinceStart());
    glUniform1f(seaShader_.opacity, sky.waterOpacity);
    glUniform1f(seaShader_.pattern, sky.waterPattern);
    glUniform1f(seaShader_.speed, sky.waterSpeed);
    glUniform1f(seaShader_.fadeStart, fadeStart);
    glUniform1f(seaShader_.fadeEnd, fadeEnd);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, seaTex_);
    glUniform1i(seaShader_.tile, 0);

    // The sea is see-through, so it blends over the seabed and does not write depth.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(kWaterBiasSlope, kWaterBiasUnits);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
