#include "render/renderer.hpp"

#include "world/world.hpp"

#include <SDL3/SDL.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wm {


namespace {

// ---------------------------------------------------------------------------
// Shaders (GLSL 3.30 core)
// ---------------------------------------------------------------------------

// Sky: one oversized triangle covers the screen. Each fragment gets a view ray.
const char* const kSkyVS = R"GLSL(
#version 330 core
uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uSkyZ;
out vec3 vDir;
void main() {
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;
    gl_Position = vec4(x, y, uSkyZ, 1.0);
    vDir = uForward + uRight * (x * uTanHalfFov * uAspect) + uUp * (y * uTanHalfFov);
}
)GLSL";

const char* const kSkyFS = R"GLSL(
#version 330 core
in vec3 vDir;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunStrength;
uniform float uStars;
uniform samplerCube uSkybox;
uniform int uUseCube;
uniform vec3 uStarRgb;
uniform float uStarAmount;
uniform vec3 uMoonDir;
uniform vec3 uMoonRgb;
uniform float uMoonStrength;
uniform float uMoonCos;
out vec4 fragColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec3 d = normalize(vDir);
    if (uUseCube != 0) {
        fragColor = vec4(texture(uSkybox, d).rgb, 1.0);
        return;
    }
    float t = clamp(d.y, 0.0, 1.0);
    vec3 col = mix(uSkyHorizon, uSkyTop, pow(t, 0.45));

    vec3 sun = normalize(uSunDir);
    float cosA = dot(d, sun);
    float disc = smoothstep(0.9994, 0.9998, cosA);
    float glow = pow(max(cosA, 0.0), 64.0) * 0.6;
    float vis = smoothstep(-0.05, 0.02, sun.y);
    col += uSunColor * (disc * 2.0 + glow * uSunStrength) * vis;

    vec3 cell = floor(d * 180.0);
    float star = step(0.9965, hash(cell)) * uStars * smoothstep(0.0, 0.3, d.y);
    col += uStarRgb * (star * uStarAmount);

    // Moon: a disc and a soft halo, opposite the sun, up only when the sun is down.
    vec3 moon = normalize(uMoonDir);
    float mc = dot(d, moon);
    float mUp = smoothstep(-0.05, 0.02, moon.y);
    float moonDisc = smoothstep(uMoonCos, uMoonCos + 0.0004, mc);
    float halo = pow(max(mc, 0.0), 300.0) * 0.25;
    col += uMoonRgb * (moonDisc * 1.5 + halo) * uMoonStrength * mUp;

    fragColor = vec4(col, 1.0);
}
)GLSL";

// Lines: per-vertex color, lit by the sun, faded into the horizon color with distance.
const char* const kLineVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform mat4 uViewProj;
uniform vec3 uEye;
out vec3 vCol;
out float vDist;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vCol = aCol;
    vDist = distance(aPos, uEye);
}
)GLSL";

const char* const kLineFS = R"GLSL(
#version 330 core
in vec3 vCol;
in float vDist;
uniform vec3 uLight;
uniform vec3 uFogColor;
uniform float uFog;
out vec4 fragColor;
void main() {
    vec3 lit = vCol * uLight;
    float d = vDist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColor = vec4(mix(lit, uFogColor, f), 1.0);
}
)GLSL";

// 2D: solid color, or the font atlas red channel as alpha when useTex is set.
const char* const kUiVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uProj;
out vec2 vUv;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

const char* const kUiFS = R"GLSL(
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform int uUseTex;
out vec4 fragColor;
void main() {
    float a = (uUseTex != 0) ? texture(uTex, vUv).r : 1.0;
    fragColor = vec4(uColor.rgb, uColor.a * a);
}
)GLSL";



// Map image: an RGB picture drawn into a 2D rectangle.
const char* const kMapVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uProj;
out vec2 vUv;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

const char* const kMapFS = R"GLSL(
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    vec4 c = texture(uTex, vUv);
    fragColor = vec4(c.rgb, c.a * uAlpha);
}
)GLSL";

// Batched 2D: every vertex carries position, texture coordinate with a mode
// (1 = font atlas alpha, 0 = solid), and color.
const char* const kBatchVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aUvMode;
layout(location = 2) in vec4 aCol;
uniform mat4 uProj;
out vec3 vUvMode;
out vec4 vCol;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUvMode = aUvMode;
    vCol = aCol;
}
)GLSL";

const char* const kBatchFS = R"GLSL(
#version 330 core
in vec3 vUvMode;
in vec4 vCol;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    float a = (vUvMode.z > 0.5) ? texture(uTex, vUvMode.xy).r : 1.0;
    fragColor = vec4(vCol.rgb, vCol.a * a);
}
)GLSL";

void PushVert(std::vector<float>& b, float x, float y, float u, float v, float mode, V3 c, float a) {
    b.insert(b.end(), {x, y, u, v, mode, c.x, c.y, c.z, a});
}

// ---------------------------------------------------------------------------
// Font: TrueType rasterised once into an atlas with stb_truetype
// ---------------------------------------------------------------------------

constexpr int   kAtlasW = 1024;
constexpr int   kAtlasH = 1024;
constexpr int   kFirstChar = 32;
constexpr int   kNumChars = 95;      // space through '~'
constexpr float kFontPixelH = 52.0f;

stbtt_bakedchar gBaked[kNumChars];
GLuint gFontTex = 0;
float gFontAscent = 0.0f;   // top of a line to its baseline, in pixels
float gLineH = 0.0f;        // baseline to baseline, in pixels

int GlyphIndex(unsigned char c) {
    if (c < kFirstChar || c >= kFirstChar + kNumChars) c = '?';
    return c - kFirstChar;
}

bool InitFont(const std::vector<unsigned char>& fontBytes, std::string& err) {
    if (fontBytes.empty()) {
        err = "the font is missing from data.pak";
        return false;
    }
    const unsigned char* ttf = fontBytes.data();

    std::vector<unsigned char> atlas(static_cast<std::size_t>(kAtlasW) * kAtlasH, 0);
    const int rows = stbtt_BakeFontBitmap(ttf, 0, kFontPixelH, atlas.data(),
                                          kAtlasW, kAtlasH, kFirstChar, kNumChars, gBaked);
    if (rows <= 0) {
        err = "font atlas too small for the font";
        return false;
    }

    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf, 0);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    const float scale = stbtt_ScaleForPixelHeight(&info, kFontPixelH);
    gFontAscent = asc * scale;
    gLineH = (asc - desc + gap) * scale;

    glGenTextures(1, &gFontTex);
    glBindTexture(GL_TEXTURE_2D, gFontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

void AppendGrid(std::vector<LineVertex>& out, V3 c, float radius, int major, V3 minorCol, V3 majorCol) {
    const int x0 = static_cast<int>(std::floor(c.x - radius));
    const int x1 = static_cast<int>(std::ceil(c.x + radius));
    const int z0 = static_cast<int>(std::floor(c.z - radius));
    const int z1 = static_cast<int>(std::ceil(c.z + radius));
    const float fx0 = static_cast<float>(x0), fx1 = static_cast<float>(x1);
    const float fz0 = static_cast<float>(z0), fz1 = static_cast<float>(z1);

    auto push = [&out](float x, float z, V3 col) {
        out.push_back({x, 0.0f, z, col.x, col.y, col.z});
    };

    for (int x = x0; x <= x1; ++x) {
        const V3 col = (x % major == 0) ? majorCol : minorCol;
        push(static_cast<float>(x), fz0, col);
        push(static_cast<float>(x), fz1, col);
    }
    for (int z = z0; z <= z1; ++z) {
        const V3 col = (z % major == 0) ? majorCol : minorCol;
        push(fx0, static_cast<float>(z), col);
        push(fx1, static_cast<float>(z), col);
    }
}

void AppendWireBox(std::vector<LineVertex>& out, V3 c, V3 s, V3 col) {
    const V3 h = s * 0.5f;
    V3 p[8];
    for (int i = 0; i < 8; ++i) {
        p[i] = c + V3{(i & 1) ? h.x : -h.x,
                      (i & 2) ? h.y : -h.y,
                      (i & 4) ? h.z : -h.z};
    }
    static const int edges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},
        {0, 2}, {1, 3}, {4, 6}, {5, 7},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (const auto& e : edges) {
        for (int k = 0; k < 2; ++k) {
            const V3& q = p[e[k]];
            out.push_back({q.x, q.y, q.z, col.x, col.y, col.z});
        }
    }
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

bool Renderer::Init(const std::vector<unsigned char>& fontBytes, std::string& err) {
    if (!LoadGL(err)) return false;

    skyProg_ = BuildProgram(kSkyVS, kSkyFS, err);
    if (!skyProg_) return false;
    lineProg_ = BuildProgram(kLineVS, kLineFS, err);
    if (!lineProg_) return false;
    uiProg_ = BuildProgram(kUiVS, kUiFS, err);
    if (!uiProg_) return false;

    if (!InitTerrain(err)) return false;
    if (!InitGrid(err)) return false;

    sky_.forward = glGetUniformLocation(skyProg_, "uForward");
    sky_.right = glGetUniformLocation(skyProg_, "uRight");
    sky_.up = glGetUniformLocation(skyProg_, "uUp");
    sky_.tanHalfFov = glGetUniformLocation(skyProg_, "uTanHalfFov");
    sky_.aspect = glGetUniformLocation(skyProg_, "uAspect");
    sky_.top = glGetUniformLocation(skyProg_, "uSkyTop");
    sky_.horizon = glGetUniformLocation(skyProg_, "uSkyHorizon");
    sky_.sunDir = glGetUniformLocation(skyProg_, "uSunDir");
    sky_.sunColor = glGetUniformLocation(skyProg_, "uSunColor");
    sky_.sunStrength = glGetUniformLocation(skyProg_, "uSunStrength");
    sky_.stars = glGetUniformLocation(skyProg_, "uStars");
    sky_.starRgb = glGetUniformLocation(skyProg_, "uStarRgb");
    sky_.starAmount = glGetUniformLocation(skyProg_, "uStarAmount");
    sky_.moonDir = glGetUniformLocation(skyProg_, "uMoonDir");
    sky_.moonRgb = glGetUniformLocation(skyProg_, "uMoonRgb");
    sky_.moonStrength = glGetUniformLocation(skyProg_, "uMoonStrength");
    sky_.moonCos = glGetUniformLocation(skyProg_, "uMoonCos");
    sky_.depth = glGetUniformLocation(skyProg_, "uSkyZ");
    sky_.cube = glGetUniformLocation(skyProg_, "uSkybox");
    sky_.useCube = glGetUniformLocation(skyProg_, "uUseCube");
    glUseProgram(skyProg_);
    glUniform1i(sky_.cube, 0);   // the skybox, on texture unit 0

    batchProg_ = BuildProgram(kBatchVS, kBatchFS, err);
    if (!batchProg_) return false;
    batchProj_ = glGetUniformLocation(batchProg_, "uProj");
    batchTex_ = glGetUniformLocation(batchProg_, "uTex");
    glGenVertexArrays(1, &batchVao_);
    glGenBuffers(1, &batchVbo_);
    glBindVertexArray(batchVao_);
    glBindBuffer(GL_ARRAY_BUFFER, batchVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<const void*>(5 * sizeof(float)));
    glBindVertexArray(0);


    line_.viewProj = glGetUniformLocation(lineProg_, "uViewProj");
    line_.eye = glGetUniformLocation(lineProg_, "uEye");
    line_.light = glGetUniformLocation(lineProg_, "uLight");
    line_.fogColor = glGetUniformLocation(lineProg_, "uFogColor");
    line_.fog = glGetUniformLocation(lineProg_, "uFog");

    ui_.proj = glGetUniformLocation(uiProg_, "uProj");
    ui_.color = glGetUniformLocation(uiProg_, "uColor");
    ui_.useTex = glGetUniformLocation(uiProg_, "uUseTex");
    ui_.tex = glGetUniformLocation(uiProg_, "uTex");

    // The sky draws from gl_VertexID, so its VAO has no attributes.
    glGenVertexArrays(1, &skyVao_);

    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          reinterpret_cast<const void*>(3 * sizeof(float)));

    glGenVertexArrays(1, &uiVao_);
    glGenBuffers(1, &uiVbo_);
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glBindVertexArray(0);

    if (!InitFont(fontBytes, err)) return false;

    glEnable(GL_DEPTH_TEST);
    return true;
}

bool Renderer::LoadSkybox(const std::string& folder, std::string& err) {
    return skybox_.Load(folder, err);
}

void Renderer::Shutdown() {
    skybox_.Unload();
    glDeleteTextures(1, &gFontTex);
    glDeleteBuffers(1, &lineVbo_);
    glDeleteBuffers(1, &uiVbo_);
    glDeleteVertexArrays(1, &skyVao_);
    glDeleteVertexArrays(1, &lineVao_);
    glDeleteVertexArrays(1, &uiVao_);
    glDeleteProgram(skyProg_);
    glDeleteProgram(lineProg_);
    glDeleteProgram(uiProg_);
    if (mapTex_) glDeleteTextures(1, &mapTex_);
    if (mapVbo_) glDeleteBuffers(1, &mapVbo_);
    if (mapVao_) glDeleteVertexArrays(1, &mapVao_);
    if (mapProg_) glDeleteProgram(mapProg_);
    mapTex_ = mapVbo_ = mapVao_ = 0;
    mapProg_ = 0;
    gFontTex = 0;
    lineVbo_ = uiVbo_ = 0;
    skyVao_ = lineVao_ = uiVao_ = 0;
    ShutdownTerrain();

    glDeleteBuffers(1, &batchVbo_);
    glDeleteVertexArrays(1, &batchVao_);
    glDeleteProgram(batchProg_);
    glDeleteProgram(gridProg_);
    batchVbo_ = batchVao_ = batchProg_ = gridProg_ = 0;

    gpu_.Shutdown();
    skyProg_ = lineProg_ = uiProg_ = 0;
}

void Renderer::BeginFrame(int w, int h, const SkyState& sky) {
    gpu_.NewFrame();
    glViewport(0, 0, w, h);
    glClearColor(sky.horizon.x, sky.horizon.y, sky.horizon.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::DrawSky(const Camera& cam, const SkyState& sky) {
    // Without skyLast the sky is drawn first with depth off. With it, the sky sits
    // at the far depth, so the depth test drops it wherever terrain is in front.
    if (gOpt.skyLast) glEnable(GL_DEPTH_TEST);
    else              glDisable(GL_DEPTH_TEST);
    glUseProgram(skyProg_);

    glUniform3f(sky_.forward, cam.fwd.x, cam.fwd.y, cam.fwd.z);
    glUniform3f(sky_.right, cam.right.x, cam.right.y, cam.right.z);
    glUniform3f(sky_.up, cam.up.x, cam.up.y, cam.up.z);
    glUniform1f(sky_.tanHalfFov, std::tan(cam.fovy * 0.5f * kDeg2Rad));
    glUniform1f(sky_.aspect, cam.aspect);

    glUniform3f(sky_.top, sky.top.x, sky.top.y, sky.top.z);
    glUniform3f(sky_.horizon, sky.horizon.x, sky.horizon.y, sky.horizon.z);
    glUniform3f(sky_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(sky_.sunColor, sky.sunColor.x, sky.sunColor.y, sky.sunColor.z);
    glUniform1f(sky_.sunStrength, sky.sunStrength);
    glUniform1f(sky_.stars, sky.stars);
    glUniform3f(sky_.starRgb, sky.starRgb.x, sky.starRgb.y, sky.starRgb.z);
    glUniform1f(sky_.starAmount, sky.starAmount);
    glUniform3f(sky_.moonDir, sky.moonDir.x, sky.moonDir.y, sky.moonDir.z);
    glUniform3f(sky_.moonRgb, sky.moonRgb.x, sky.moonRgb.y, sky.moonRgb.z);
    glUniform1f(sky_.moonStrength, sky.moonStrength);
    glUniform1f(sky_.moonCos, sky.moonCos);
    glUniform1f(sky_.depth, gOpt.skyLast ? 0.9999f : 0.0f);

    const bool cube = UsingSkybox();
    glUniform1i(sky_.useCube, cube ? 1 : 0);
    if (cube) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skybox_.texture());
    }

    glBindVertexArray(skyVao_);
    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_DEPTH_TEST);
}

void Renderer::DrawLines(const Camera& cam, const SkyState& sky, const std::vector<LineVertex>& lines) {
    if (lines.empty()) return;

    glEnable(GL_DEPTH_TEST);
    glUseProgram(lineProg_);
    glUniformMatrix4fv(line_.viewProj, 1, GL_FALSE, cam.viewProj.m);
    glUniform3f(line_.eye, cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform3f(line_.light, sky.light.x, sky.light.y, sky.light.z);
    glUniform3f(line_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(line_.fog, sky.fog);

    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(LineVertex), lines.data(), GL_STREAM_DRAW);
    ++gStats.drawCalls;
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
}

void Renderer::BeginUI(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    uiProj_ = Ortho(0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, -1.0f, 1.0f);
    batching_ = gOpt.batchUi;
    batch_.clear();
}

void Renderer::EnsureMap() {
    if (mapProg_) return;
    std::string err;
    mapProg_ = BuildProgram(kMapVS, kMapFS, err);
    if (!mapProg_) return;
    mapProjLoc_ = glGetUniformLocation(mapProg_, "uProj");
    mapAlphaLoc_ = glGetUniformLocation(mapProg_, "uAlpha");
    mapTexLoc_ = glGetUniformLocation(mapProg_, "uTex");
    glGenVertexArrays(1, &mapVao_);
    glGenBuffers(1, &mapVbo_);
    glBindVertexArray(mapVao_);
    glBindBuffer(GL_ARRAY_BUFFER, mapVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    glGenTextures(1, &mapTex_);
}

void Renderer::UploadMap(const uint8_t* rgba, int side) {
    EnsureMap();
    if (!mapTex_ || !rgba || side <= 0) return;
    glBindTexture(GL_TEXTURE_2D, mapTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::DrawMap(float x, float y, float w, float h, float u0, float v0, float u1, float v1, float alpha) {
    if (!mapProg_ || !mapTex_) return;
    const float v[24] = {
        x,     y,     u0, v0,
        x + w, y,     u1, v0,
        x + w, y + h, u1, v1,
        x,     y,     u0, v0,
        x + w, y + h, u1, v1,
        x,     y + h, u0, v1,
    };
    glBindVertexArray(mapVao_);
    glBindBuffer(GL_ARRAY_BUFFER, mapVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    glUseProgram(mapProg_);
    glUniformMatrix4fv(mapProjLoc_, 1, GL_FALSE, uiProj_.m);
    glUniform1f(mapAlphaLoc_, alpha);
    glUniform1i(mapTexLoc_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mapTex_);
    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Renderer::EndUI() {
    if (batching_) FlushBatch();
    batching_ = false;
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::DrawUI(const float* verts, std::size_t floats, bool useTex, V3 c, float a) {
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, floats * sizeof(float), verts, GL_STREAM_DRAW);

    glUseProgram(uiProg_);
    glUniformMatrix4fv(ui_.proj, 1, GL_FALSE, uiProj_.m);
    glUniform4f(ui_.color, c.x, c.y, c.z, a);
    glUniform1i(ui_.useTex, useTex ? 1 : 0);
    glUniform1i(ui_.tex, 0);
    glBindTexture(GL_TEXTURE_2D, gFontTex);

    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(floats / 4));
}

void Renderer::FlushBatch() {
    if (batch_.empty()) return;

    glBindVertexArray(batchVao_);
    glBindBuffer(GL_ARRAY_BUFFER, batchVbo_);
    glBufferData(GL_ARRAY_BUFFER, batch_.size() * sizeof(float), batch_.data(), GL_STREAM_DRAW);

    glUseProgram(batchProg_);
    glUniformMatrix4fv(batchProj_, 1, GL_FALSE, uiProj_.m);
    glUniform1i(batchTex_, 0);
    glBindTexture(GL_TEXTURE_2D, gFontTex);

    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch_.size() / 9));
    batch_.clear();
}

void Renderer::Rect(float x, float y, float w, float h, V3 c, float a) {
    if (batching_) {
        PushVert(batch_, x,     y,     0, 0, 0, c, a);
        PushVert(batch_, x + w, y,     0, 0, 0, c, a);
        PushVert(batch_, x + w, y + h, 0, 0, 0, c, a);
        PushVert(batch_, x,     y,     0, 0, 0, c, a);
        PushVert(batch_, x + w, y + h, 0, 0, 0, c, a);
        PushVert(batch_, x,     y + h, 0, 0, 0, c, a);
        return;
    }
    const float v[24] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     0.0f, 0.0f,
        x + w, y + h, 0.0f, 0.0f,
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 0.0f, 0.0f,
        x,     y + h, 0.0f, 0.0f,
    };
    DrawUI(v, 24, false, c, a);
}

void Renderer::TextAlpha(float x, float top, const std::string& s, V3 c, float alpha) {
    if (s.empty()) return;

    if (batching_) {
        float cx = x;
        float cy = top + gFontAscent;
        for (unsigned char ch : s) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(gBaked, kAtlasW, kAtlasH, GlyphIndex(ch), &cx, &cy, &q, 1);
            PushVert(batch_, q.x0, q.y0, q.s0, q.t0, 1, c, alpha);
            PushVert(batch_, q.x1, q.y0, q.s1, q.t0, 1, c, alpha);
            PushVert(batch_, q.x1, q.y1, q.s1, q.t1, 1, c, alpha);
            PushVert(batch_, q.x0, q.y0, q.s0, q.t0, 1, c, alpha);
            PushVert(batch_, q.x1, q.y1, q.s1, q.t1, 1, c, alpha);
            PushVert(batch_, q.x0, q.y1, q.s0, q.t1, 1, c, alpha);
        }
        return;
    }

    std::vector<float> v;
    v.reserve(s.size() * 24);

    float cx = x;
    float cy = top + gFontAscent;   // baseline
    for (unsigned char ch : s) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(gBaked, kAtlasW, kAtlasH, GlyphIndex(ch), &cx, &cy, &q, 1);
        const float quad[24] = {
            q.x0, q.y0, q.s0, q.t0,
            q.x1, q.y0, q.s1, q.t0,
            q.x1, q.y1, q.s1, q.t1,
            q.x0, q.y0, q.s0, q.t0,
            q.x1, q.y1, q.s1, q.t1,
            q.x0, q.y1, q.s0, q.t1,
        };
        v.insert(v.end(), quad, quad + 24);
    }
    DrawUI(v.data(), v.size(), true, c, alpha);
}

void Renderer::TextScaled(float x, float top, const std::string& s, V3 c, float alpha, float scale) {
    if (s.empty()) return;
    const float ox = x;
    const float oy = top + gFontAscent;   // baseline in the unscaled layout
    const float ny = top + gFontAscent * scale;   // baseline once scaled, so the line keeps its top
    float cx = x;
    float cy = oy;

    if (batching_) {
        for (unsigned char ch : s) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(gBaked, kAtlasW, kAtlasH, GlyphIndex(ch), &cx, &cy, &q, 1);
            const float x0 = ox + (q.x0 - ox) * scale, x1 = ox + (q.x1 - ox) * scale;
            const float y0 = ny + (q.y0 - oy) * scale, y1 = ny + (q.y1 - oy) * scale;
            PushVert(batch_, x0, y0, q.s0, q.t0, 1, c, alpha);
            PushVert(batch_, x1, y0, q.s1, q.t0, 1, c, alpha);
            PushVert(batch_, x1, y1, q.s1, q.t1, 1, c, alpha);
            PushVert(batch_, x0, y0, q.s0, q.t0, 1, c, alpha);
            PushVert(batch_, x1, y1, q.s1, q.t1, 1, c, alpha);
            PushVert(batch_, x0, y1, q.s0, q.t1, 1, c, alpha);
        }
        return;
    }

    std::vector<float> v;
    v.reserve(s.size() * 24);
    for (unsigned char ch : s) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(gBaked, kAtlasW, kAtlasH, GlyphIndex(ch), &cx, &cy, &q, 1);
        const float x0 = ox + (q.x0 - ox) * scale, x1 = ox + (q.x1 - ox) * scale;
        const float y0 = ny + (q.y0 - oy) * scale, y1 = ny + (q.y1 - oy) * scale;
        const float quad[24] = {
            x0, y0, q.s0, q.t0,
            x1, y0, q.s1, q.t0,
            x1, y1, q.s1, q.t1,
            x0, y0, q.s0, q.t0,
            x1, y1, q.s1, q.t1,
            x0, y1, q.s0, q.t1,
        };
        v.insert(v.end(), quad, quad + 24);
    }
    DrawUI(v.data(), v.size(), true, c, alpha);
}

void Renderer::Text(float x, float top, const std::string& s, V3 c) {
    TextAlpha(x, top, s, c, 1.0f);
}

float Renderer::MeasureText(const std::string& s) const {
    if (gOpt.cacheText) {
        const auto hit = widthCache_.find(s);
        if (hit != widthCache_.end()) return hit->second;
    }
    float w = 0.0f;
    for (unsigned char ch : s) w += gBaked[GlyphIndex(ch)].xadvance;
    if (gOpt.cacheText) {
        if (widthCache_.size() > 4096) widthCache_.clear();
        widthCache_.emplace(s, w);
    }
    return w;
}

float Renderer::LineHeight() const {
    return gLineH;
}

}  // namespace wm
