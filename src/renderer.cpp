#include "renderer.h"
#include "terrain_mesh.h"
#include "pak.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr float GLYPH_H = 1.2f;          // world height of unit sprites
constexpr float WAGON_SIZE = 1.5f;       // largest side of the wagon model
constexpr float SKY_SPAN = 0.2f;         // fraction of the panorama visible across the screen
constexpr float TWO_PI = 6.28318530f;

const RGBA WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };

struct V3 { float x, y, z; };
struct Vertex { V3 p; RGBA c; };         // position, then colour

const char* COLOR_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* COLOR_FS = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

const char* GLYPH_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* GLYPH_FS = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uTint;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.1) discard;   // keep only the visible pixels
    FragColor = c * uTint;
}
)";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        SDL_Log("Shader error: %s", log);
    }
    return s;
}

GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// Position (location 0) and colour (location 1), for Vertex
void setupColorLayout(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// Position (location 0) and UV (location 1), 5 floats per vertex
void setupGlyphLayout(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// Coloured triangles rebuilt every frame (selection rings, drag box)
struct Batch {
    std::vector<Vertex> v;

    void tri(V3 a, V3 b, V3 c, RGBA col) {
        v.push_back({ a, col });
        v.push_back({ b, col });
        v.push_back({ c, col });
    }

    void quad(V3 a, V3 b, V3 c, V3 d, RGBA col) {
        tri(a, b, c, col);
        tri(a, c, d, col);
    }
};

// Thin square outline on the ground
void outlineSquare(Batch& b, float cx, float y, float cz, float r, float t, RGBA c) {
    b.quad({ cx - r, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz - r + t }, { cx - r, y, cz - r + t }, c);
    b.quad({ cx - r, y, cz + r - t }, { cx + r, y, cz + r - t }, { cx + r, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx - r, y, cz - r }, { cx - r + t, y, cz - r }, { cx - r + t, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx + r - t, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz + r }, { cx + r - t, y, cz + r }, c);
}

// Renders one character from the font into a texture
TextTex makeGlyphTexture(TTF_Font* font, const char* text, SDL_Color color) {
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), color);
    if (!s) {
        SDL_Log("TTF_RenderText failed: %s", SDL_GetError());
        return {};
    }
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);

    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);

    TextTex result{ t, (float)conv->w / (float)conv->h };
    SDL_DestroySurface(conv);
    return result;
}

// Loads a PNG (or any image SDL_image understands) into a texture
TextTex loadImage(const char* path, bool repeatHorizontally) {
    SDL_Surface* s = pakImage(path);
    if (!s) {
        SDL_Log("Could not load %s: %s", path, SDL_GetError());
        return {};
    }
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (!conv) return {};

    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatHorizontally ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);

    TextTex result{ t, (float)conv->w / (float)conv->h };
    SDL_DestroySurface(conv);
    return result;
}

const char* SKY_VS = R"(
#version 330 core
layout(location=0) in vec2 aNdc;
uniform mat4 uInvVP;
out vec3 vDir;
void main() {
    vec4 p = uInvVP * vec4(aNdc, 1.0, 1.0);
    vDir = p.xyz / p.w;
    gl_Position = vec4(aNdc, 1.0, 1.0);
}
)";

const char* SKY_FS = R"(
#version 330 core
in vec3 vDir;
uniform vec3 uSunDir;     // unit vector towards the sun
uniform float uSunAlpha;  // 0 when the sun is below the horizon, 1 when fully up
uniform vec3 uSunCol;     // colour of the sun's glow and disc
uniform vec3 uMoonDir;    // unit vector towards the moon, opposite the sun
uniform float uMoonAlpha; // 0 when the moon is hidden
uniform vec3 uMoonCol;
uniform float uStarAlpha; // 0 when there are no stars
uniform vec3 uZenith;     // sky colour straight up
uniform vec3 uHorizon;    // sky colour at the horizon
out vec4 FragColor;
void main() {
    vec3 d = normalize(vDir);
    // Gradient: the horizon colour blends up to the zenith colour
    vec3 col = mix(uHorizon, uZenith, pow(clamp(d.y, 0.0, 1.0), 0.6));

    // Stars: one per cell of a fine grid over the sphere, where a hash says so. Upper sky only.
    if (uStarAlpha > 0.0 && d.y > 0.0) {
        vec3 cell = floor(d * 180.0);
        float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        col += vec3(step(0.994, h) * smoothstep(0.0, 0.3, d.y) * uStarAlpha);
    }

    // Moon: a soft halo and a small pale disc
    float m = max(dot(d, uMoonDir), 0.0);
    col += uMoonCol * 0.15 * pow(m, 6.0) * uMoonAlpha;
    col = mix(col, uMoonCol, smoothstep(0.9990, 0.9994, m) * uMoonAlpha);

    // Sun: a wide glow and a hard-edged disc
    float s = max(dot(d, uSunDir), 0.0);
    col += uSunCol * (0.25 * pow(s, 8.0) + 0.5 * pow(s, 128.0)) * uSunAlpha;
    float disc = smoothstep(0.9994, 0.9997, s) * uSunAlpha;
    col = mix(col, uSunCol, disc);
    FragColor = vec4(col, 1.0);
}
)";

} // namespace

bool Renderer::init(TTF_Font* font, TTF_Font* hudFont, TTF_Font* uiFont,
                    const std::function<void(float)>& progress) {
    auto report = [&](float p) { if (progress) progress(p); };
    font_ = font;
    hudFont_ = hudFont;
    uiFont_ = uiFont;

    colorProgram_ = linkProgram(COLOR_VS, COLOR_FS);
    uMVPColor_ = glGetUniformLocation(colorProgram_, "uMVP");

    glyphProgram_ = linkProgram(GLYPH_VS, GLYPH_FS);
    uMVPGlyph_ = glGetUniformLocation(glyphProgram_, "uMVP");
    uTint_ = glGetUniformLocation(glyphProgram_, "uTint");
    glUseProgram(glyphProgram_);
    glUniform1i(glGetUniformLocation(glyphProgram_, "uTex"), 0);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);
    glUseProgram(0);

    glGenVertexArrays(1, &dynVao_);
    glGenBuffers(1, &dynVbo_);
    setupColorLayout(dynVao_, dynVbo_);

    glGenVertexArrays(1, &glyphVao_);
    glGenBuffers(1, &glyphVbo_);
    glBindVertexArray(glyphVao_);
    glBindBuffer(GL_ARRAY_BUFFER, glyphVbo_);
    glBufferData(GL_ARRAY_BUFFER, 4 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
    setupGlyphLayout(glyphVao_, glyphVbo_);

    // Images and models. The overlay loads first, so the loading screen can show it.
    overlay_ = loadImage("data/background/overlay_01.png", false);
    title_ = loadImage("data/ui/title.png", false);
    report(0.05f);
    unitSprites_.reserve(38);
    for (int i = 1; i <= 38; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/unit/%02d.png", i);
        unitSprites_.push_back(loadImage(path, false));
        report(0.05f + 0.75f * (float)i / 38.0f);
    }
    report(0.82f);
    report(0.86f);

    skyProgram_ = linkProgram(SKY_VS, SKY_FS);
    uSkyInvVP_ = glGetUniformLocation(skyProgram_, "uInvVP");
    uSunDir_   = glGetUniformLocation(skyProgram_, "uSunDir");
    uSunAlpha_ = glGetUniformLocation(skyProgram_, "uSunAlpha");
    uZenith_   = glGetUniformLocation(skyProgram_, "uZenith");
    uHorizon_  = glGetUniformLocation(skyProgram_, "uHorizon");
    uSunCol_   = glGetUniformLocation(skyProgram_, "uSunCol");
    uMoonDir_   = glGetUniformLocation(skyProgram_, "uMoonDir");
    uMoonAlpha_ = glGetUniformLocation(skyProgram_, "uMoonAlpha");
    uMoonCol_   = glGetUniformLocation(skyProgram_, "uMoonCol");
    uStarAlpha_ = glGetUniformLocation(skyProgram_, "uStarAlpha");

    const float skyQuad[8] = { -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, 1.0f,  -1.0f, 1.0f };
    glGenVertexArrays(1, &skyVao_);
    glGenBuffers(1, &skyVbo_);
    glBindVertexArray(skyVao_);
    glBindBuffer(GL_ARRAY_BUFFER, skyVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyQuad), skyQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    unitFallback_ = makeGlyphTexture(font, "@", SDL_Color{ 80, 160, 255, 255 });
    report(0.9f);

    if (!loadGLBModel("data/3d/wagon.glb", wagon_)) {
        SDL_Log("Wagon model unavailable, the wagon will not be drawn");
    }
    report(0.97f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return colorProgram_ != 0 && glyphProgram_ != 0;
}

void Renderer::shutdown() {
    TextTex* textures[] = { &overlay_, &unitFallback_ };
    for (TextTex* g : textures) {
        if (g->tex) glDeleteTextures(1, &g->tex);
        g->tex = 0;
    }
    for (TextTex& g : unitSprites_) {
        if (g.tex) glDeleteTextures(1, &g.tex);
    }
    unitSprites_.clear();
    for (HudLine& hl : hudLines_) {
        if (hl.tex.tex) glDeleteTextures(1, &hl.tex.tex);
    }
    hudLines_.clear();
    for (auto& entry : uiText_) {
        if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
    }
    uiText_.clear();
    for (auto& entry : listText_) {
        if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
    }
    listText_.clear();
    if (title_.tex) glDeleteTextures(1, &title_.tex);
    title_.tex = 0;
    destroyModel(wagon_);

    glDeleteProgram(skyProgram_);
    glDeleteBuffers(1, &skyVbo_);
    glDeleteVertexArrays(1, &skyVao_);

    glDeleteProgram(colorProgram_);
    glDeleteProgram(glyphProgram_);
    glDeleteBuffers(1, &dynVbo_);
    glDeleteVertexArrays(1, &dynVao_);
    glDeleteBuffers(1, &glyphVbo_);
    glDeleteVertexArrays(1, &glyphVao_);
    releaseTerrain();
    colorProgram_ = glyphProgram_ = 0;
}

namespace {

// Chunks closer to the eye than these distances, in tiles, are drawn at full, half and quarter detail
constexpr float LOD_NEAR_TILES = 96.0f;
constexpr float LOD_MID_TILES = 224.0f;
// The sea is drawn just under zero, so flat ground at zero is not tinted by it
constexpr float SEA_Y = -0.05f;

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

} // namespace

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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
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

// Draws the loaded chunks in view, each at the detail its distance from the eye needs, then the sea.
// Only a few chunk meshes are built per frame, so a newly loaded area fills in over a few frames.
void Renderer::drawTerrain(const Terrain& t, const Camera& cam) {
    if (t.size() != terrainSize_ || chunks_.empty()) uploadTerrain(t);
    constexpr int MAX_BUILDS_PER_FRAME = 8;
    const FrustumPlanes planes = framePlanes(viewProj_);
    float ex, ey, ez;
    cam.eye(ex, ey, ez);

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
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

    // The sea: a translucent plane over the whole map, so the seabed shows through it
    const float s = (float)t.size();
    Batch sea;
    sea.quad({ 0.0f, SEA_Y, 0.0f }, { s, SEA_Y, 0.0f }, { s, SEA_Y, s }, { 0.0f, SEA_Y, s },
             RGBA{ 0.16f, 0.36f, 0.58f, 0.55f });
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, sea.v.size() * sizeof(Vertex), sea.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sea.v.size());
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::drawQuadVerts(GLuint tex, const float* v, RGBA tint) {
    glUniform4f(uTint_, tint.r, tint.g, tint.b, tint.a);
    glBindVertexArray(glyphVao_);
    glBindBuffer(GL_ARRAY_BUFFER, glyphVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 4 * 5 * sizeof(float), v);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void Renderer::drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint) {
    if (!g.tex) return;

    // Right vector perpendicular to the direction from the sprite to the billboard point
    float dx = billX_ - x, dz = billZ_ - z;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    float rx = dz, rz = -dx;

    float hw = h * g.aspect * 0.5f;
    float hh = h * 0.5f;
    float cy = y + hh;   // the bottom of the sprite sits on the ground

    float v[4 * 5] = {
        x - rx * hw, cy - hh, z - rz * hw, 0.0f, 1.0f,   // bottom-left
        x + rx * hw, cy - hh, z + rz * hw, 1.0f, 1.0f,   // bottom-right
        x + rx * hw, cy + hh, z + rz * hw, 1.0f, 0.0f,   // top-right
        x - rx * hw, cy + hh, z - rz * hw, 0.0f, 0.0f    // top-left
    };
    drawQuadVerts(g.tex, v, tint);
}

void Renderer::drawModel(const Model& m, float x, float y, float z, float targetSize) {
    if (!m.vao) return;
    float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
    if (extent <= 0.0f) return;
    float s = targetSize / extent;
    float cx = (m.minX + m.maxX) * 0.5f;
    float cz = (m.minZ + m.maxZ) * 0.5f;

    // Scale, then move so the centre of the bottom sits on (x, y, z)
    Mat4 model{};
    model.m[0] = s;
    model.m[5] = s;
    model.m[10] = s;
    model.m[15] = 1.0f;
    model.m[12] = x - s * cx;
    model.m[13] = y - s * m.minY;
    model.m[14] = z - s * cz;

    Mat4 mvp = mul(viewProj_, model);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);

    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT,
                       (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);

    // Restore the shared matrix for the sprites that follow
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

// Loading screen: the start image centred and fitted to the window, with a progress bar
// across the bottom 4% of the screen (white fill on black)
void Renderer::drawLoadingScreen(float progress, int winW, int winH, const std::string& label) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    Mat4 pixels = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);

    // Start image, fitted inside the window with its aspect ratio kept
    if (overlay_.tex) {
        float screenAspect = (float)winW / (float)winH;
        float wd, h;
        if (overlay_.aspect > screenAspect) {
            wd = (float)winW;
            h = wd / overlay_.aspect;
        } else {
            h = (float)winH;
            wd = h * overlay_.aspect;
        }
        float x0 = winW * 0.5f - wd * 0.5f, x1 = x0 + wd;
        float y0 = winH * 0.5f - h * 0.5f,  y1 = y0 + h;
        float v[4 * 5] = {
            x0, y1, 0.0f, 0.0f, 1.0f,
            x1, y1, 0.0f, 1.0f, 1.0f,
            x1, y0, 0.0f, 1.0f, 0.0f,
            x0, y0, 0.0f, 0.0f, 0.0f
        };
        glUseProgram(glyphProgram_);
        glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels.m);
        drawQuadVerts(overlay_.tex, v, WHITE);
    }

    // Progress bar: fills left to right across the bottom 4% of the screen
    float barTop = winH * 0.96f;
    float fill = winW * std::clamp(progress, 0.0f, 1.0f);
    Batch bar;
    bar.quad({ 0.0f, barTop, 0.0f }, { fill, barTop, 0.0f },
             { fill, (float)winH, 0.0f }, { 0.0f, (float)winH, 0.0f }, WHITE);
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, bar.v.size() * sizeof(Vertex), bar.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)bar.v.size());
    glBindVertexArray(0);

    // The name of the stage that is running, above the bar
    if (!label.empty()) {
        beginUi(winW, winH);
        drawUiTextCentered(label, winW * 0.5f, winH * 0.90f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        endUi();
    }

    glEnable(GL_DEPTH_TEST);
}

// ---- Menus ----

void Renderer::beginUi(int winW, int winH) {
    uiW_ = winW;
    uiH_ = winH;
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    pixels_ = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
}

void Renderer::endUi() {
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawTitleBackground() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!title_.tex) return;

    // Cover the screen, keeping the aspect ratio and cropping whatever overflows
    float screenAspect = (float)uiW_ / (float)uiH_;
    float wd, h;
    if (title_.aspect > screenAspect) {
        h = (float)uiH_;
        wd = h * title_.aspect;
    } else {
        wd = (float)uiW_;
        h = wd / title_.aspect;
    }
    float x0 = uiW_ * 0.5f - wd * 0.5f, x1 = x0 + wd;
    float y0 = uiH_ * 0.5f - h * 0.5f,  y1 = y0 + h;
    float v[4 * 5] = {
        x0, y1, 0.0f, 0.0f, 1.0f,
        x1, y1, 0.0f, 1.0f, 1.0f,
        x1, y0, 0.0f, 1.0f, 0.0f,
        x0, y0, 0.0f, 0.0f, 0.0f
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(title_.tex, v, WHITE);
}

void Renderer::drawRect(const Rect& r, RGBA c) {
    Batch b;
    b.quad({ r.x, r.y, 0 }, { r.x + r.w, r.y, 0 }, { r.x + r.w, r.y + r.h, 0 }, { r.x, r.y + r.h, 0 }, c);
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, b.v.size() * sizeof(Vertex), b.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b.v.size());
}

Renderer::HudLine& Renderer::uiLine(const std::string& s) {
    auto it = uiText_.find(s);
    if (it != uiText_.end()) return it->second;

    HudLine hl;
    hl.text = s;
    int tw = 0, th = 0;
    TTF_GetStringSize(uiFont_, s.c_str(), s.size(), &tw, &th);
    if (s.find_first_not_of(' ') == std::string::npos) {
        // SDL_ttf fails on text with no width, so blank text gets no texture but keeps its line height
        TTF_GetStringSize(uiFont_, "Ag", 2, &tw, &th);
        hl.w = 0.0f;
        hl.h = (float)th;
        return uiText_[s] = hl;
    }
    hl.tex = makeGlyphTexture(uiFont_, s.c_str(), SDL_Color{ 255, 255, 255, 255 });
    hl.w = (float)tw;
    hl.h = (float)th;
    return uiText_[s] = hl;
}

float Renderer::uiTextWidth(const std::string& s) {
    return uiLine(s).w;
}

void Renderer::drawUiText(const std::string& s, float x, float y, RGBA tint) {
    HudLine& hl = uiLine(s);
    if (!hl.tex.tex) return;
    float v[4 * 5] = {
        x,         y + hl.h, 0.0f, 0.0f, 1.0f,
        x + hl.w,  y + hl.h, 0.0f, 1.0f, 1.0f,
        x + hl.w,  y,        0.0f, 1.0f, 0.0f,
        x,         y,        0.0f, 0.0f, 0.0f
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(hl.tex.tex, v, tint);
}

void Renderer::drawUiTextCentered(const std::string& s, float cx, float y, RGBA tint) {
    drawUiText(s, cx - uiTextWidth(s) * 0.5f, y, tint);
}

void Renderer::setListFont(TTF_Font* font) {
    // Textures made with the old font would be stale, so drop them
    for (auto& entry : listText_) {
        if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
    }
    listText_.clear();
    listFont_ = font;
}

Renderer::HudLine& Renderer::listLine(const std::string& s) {
    auto it = listText_.find(s);
    if (it != listText_.end()) return it->second;

    HudLine hl;
    hl.text = s;
    int tw = 0, th = 0;
    TTF_GetStringSize(listFont_, s.c_str(), s.size(), &tw, &th);
    if (s.find_first_not_of(' ') == std::string::npos) {
        // SDL_ttf fails on text with no width, so blank text gets no texture but keeps its line height
        TTF_GetStringSize(listFont_, "Ag", 2, &tw, &th);
        hl.w = 0.0f;
        hl.h = (float)th;
        return listText_[s] = hl;
    }
    hl.tex = makeGlyphTexture(listFont_, s.c_str(), SDL_Color{ 255, 255, 255, 255 });
    hl.w = (float)tw;
    hl.h = (float)th;
    return listText_[s] = hl;
}

float Renderer::listTextHeight(const std::string& s) {
    return listLine(s).h;
}

void Renderer::drawHoldButton(const Rect& r, const std::string& label, float fill) {
    drawRect(r, RGBA{ 0.14f, 0.17f, 0.22f, 0.90f });
    if (fill > 0.0f) drawRect(Rect{ r.x, r.y, r.w * fill, r.h }, RGBA{ 0.75f, 0.20f, 0.20f, 0.95f });
    float textH = uiLine(label).h;
    drawUiText(label, r.x + (r.w - uiTextWidth(label)) * 0.5f, r.y + (r.h - textH) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

void Renderer::drawListButton(const Rect& r, const std::string& label, bool hovered) {
    drawRect(r, hovered ? RGBA{ 0.60f, 0.18f, 0.18f, 0.95f } : RGBA{ 0.34f, 0.12f, 0.12f, 0.90f });
    const float tw = listLine(label).w;
    const float th = listTextHeight(label);
    drawListText(label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

void Renderer::drawListText(const std::string& s, float x, float y, RGBA tint) {
    if (!listFont_) return;
    HudLine& hl = listLine(s);
    if (!hl.tex.tex) return;
    float v[4 * 5] = {
        x,         y + hl.h, 0.0f, 0.0f, 1.0f,
        x + hl.w,  y + hl.h, 0.0f, 1.0f, 1.0f,
        x + hl.w,  y,        0.0f, 1.0f, 0.0f,
        x,         y,        0.0f, 0.0f, 0.0f
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(hl.tex.tex, v, tint);
}

void Renderer::drawButton(const Rect& r, const std::string& label, bool enabled, bool hovered) {
    RGBA bg = !enabled ? RGBA{ 0.10f, 0.10f, 0.12f, 0.70f }
            : hovered  ? RGBA{ 0.25f, 0.34f, 0.50f, 0.95f }
                       : RGBA{ 0.14f, 0.17f, 0.22f, 0.90f };
    drawRect(r, bg);

    RGBA fg = enabled ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.5f, 0.5f, 0.5f, 1.0f };
    float textH = uiLine(label).h;
    drawUiText(label, r.x + (r.w - uiTextWidth(label)) * 0.5f, r.y + (r.h - textH) * 0.5f, fg);
}

// The sprite for a unit, or the fallback symbol if its image is missing
const TextTex& Renderer::unitGlyph(World& w, Entity e) const {
    const UnitSprite* us = w.get<UnitSprite>(e);
    int idx = us ? us->id - 1 : 0;
    const bool inRange = idx >= 0 && idx < (int)unitSprites_.size();
    return (inRange && unitSprites_[idx].tex) ? unitSprites_[idx] : unitFallback_;
}

// Projects each unit's billboard to the screen and keeps those under the pixel.
// The test uses the same size and aspect that drawGlyph uses.
std::vector<Entity> Renderer::unitsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH) {
    std::vector<Entity> out;
    Mat4 vp = cam.viewProj((float)winW / (float)winH);

    auto project = [&](float x, float y, float z, float& sx, float& sy) {
        float c[4];
        for (int i = 0; i < 4; ++i) {
            c[i] = vp.m[0 * 4 + i] * x + vp.m[1 * 4 + i] * y + vp.m[2 * 4 + i] * z + vp.m[3 * 4 + i];
        }
        if (c[3] <= 0.0f) return false;
        sx = (c[0] / c[3] * 0.5f + 0.5f) * (float)winW;
        sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * (float)winH;
        return true;
    };

    for (Entity u : w.view<Unit, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        float bottomX, bottomY, topX, topY, midX, midY;
        if (!project(tr->x, tr->y, tr->z, bottomX, bottomY)) continue;
        if (!project(tr->x, tr->y + GLYPH_H, tr->z, topX, topY)) continue;
        if (!project(tr->x, tr->y + GLYPH_H * 0.5f, tr->z, midX, midY)) continue;

        float heightPx = std::fabs(bottomY - topY);
        float halfW = heightPx * 0.5f * unitGlyph(w, u).aspect;
        float top = std::min(bottomY, topY), bottom = std::max(bottomY, topY);
        if (mx >= midX - halfW && mx <= midX + halfW && my >= top && my <= bottom) {
            out.push_back(u);
        }
    }
    return out;
}

void Renderer::drawHud(const std::vector<std::string>& lines, int winW, int winH) {
    if (!hudFont_) return;

    // Rebuild only the lines whose text changed
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == hudLines_.size()) hudLines_.emplace_back();
        HudLine& hl = hudLines_[i];
        if (hl.tex.tex && hl.text == lines[i]) continue;
        if (hl.tex.tex) glDeleteTextures(1, &hl.tex.tex);

        int tw = 0, th = 0;
        TTF_GetStringSize(hudFont_, lines[i].c_str(), lines[i].size(), &tw, &th);
        hl.text = lines[i];
        hl.tex = makeGlyphTexture(hudFont_, lines[i].c_str(), SDL_Color{ 255, 255, 255, 255 });
        hl.w = (float)tw;
        hl.h = (float)th;
    }
    // Drop lines that are no longer shown
    while (hudLines_.size() > lines.size()) {
        if (hudLines_.back().tex.tex) glDeleteTextures(1, &hudLines_.back().tex.tex);
        hudLines_.pop_back();
    }
    if (lines.empty()) return;

    Mat4 pixels = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
    const float margin = 12.0f, pad = 6.0f, gap = 2.0f;

    glDisable(GL_DEPTH_TEST);

    // Dark panels behind the text
    Batch panels;
    float y = margin;
    for (const HudLine& hl : hudLines_) {
        float x0 = margin, x1 = margin + hl.w + pad * 2.0f;
        float y0 = y, y1 = y + hl.h + pad * 2.0f;
        panels.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 },
                    RGBA{ 0.0f, 0.0f, 0.0f, 0.55f });
        y = y1 + gap;
    }
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, panels.v.size() * sizeof(Vertex), panels.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)panels.v.size());

    // Text on top of each panel
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels.m);
    y = margin;
    for (const HudLine& hl : hudLines_) {
        float x0 = margin + pad, x1 = x0 + hl.w;
        float y0 = y + pad, y1 = y0 + hl.h;
        float v[4 * 5] = {
            x0, y1, 0.0f, 0.0f, 1.0f,
            x1, y1, 0.0f, 1.0f, 1.0f,
            x1, y0, 0.0f, 1.0f, 0.0f,
            x0, y0, 0.0f, 0.0f, 0.0f
        };
        drawQuadVerts(hl.tex.tex, v, WHITE);
        y = y1 + pad + gap;
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH) {
    if (alpha <= 0.0f) return;

    // Sits on the ground at the exact point the camera orbits around
    const float cx = cam.focusX, cz = cam.focusZ;
    const float cy = t.heightAt(cx, cz) + 0.03f;
    const float outer = 0.5f, inner = 0.44f;     // about one tile across
    const int segments = 48;
    const float twoPi = 6.2831853f;

    Batch ring;
    RGBA col{ 0.95f, 0.95f, 1.0f, 0.9f * alpha };
    for (int i = 0; i < segments; ++i) {
        float a0 = twoPi * i / segments, a1 = twoPi * (i + 1) / segments;
        V3 o0{ cx + outer * std::cos(a0), cy, cz + outer * std::sin(a0) };
        V3 o1{ cx + outer * std::cos(a1), cy, cz + outer * std::sin(a1) };
        V3 i0{ cx + inner * std::cos(a0), cy, cz + inner * std::sin(a0) };
        V3 i1{ cx + inner * std::cos(a1), cy, cz + inner * std::sin(a1) };
        ring.quad(o0, o1, i1, i0, col);
    }

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, cam.viewProj((float)winW / (float)winH).m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, ring.v.size() * sizeof(Vertex), ring.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ring.v.size());
}

void Renderer::drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha,
                                 int winW, int winH) {
    if (alpha <= 0.0f) return;

    // An eight-faced crystal: a point at the top and bottom, with a ring of four corners between.
    // It floats a little above the ground, so it reads as a marker rather than part of the terrain.
    const float gy = t.heightAt(x, z);
    const float cy = gy + 0.55f, halfH = 0.45f, r = 0.22f;
    const V3 top{ x, cy + halfH, z };
    const V3 bottom{ x, cy - halfH, z };
    const V3 corner[4] = { V3{ x + r, cy, z }, V3{ x, cy, z + r }, V3{ x - r, cy, z }, V3{ x, cy, z - r } };

    Batch crystal;
    for (int i = 0; i < 4; ++i) {
        const V3& a = corner[i];
        const V3& b = corner[(i + 1) % 4];
        // Alternate brightness between faces so the facets read as separate planes
        const float shade = (i % 2 == 0) ? 1.0f : 0.8f;
        RGBA upper{ 0.6f * shade, 0.95f * shade, 1.0f, 0.95f * alpha };
        RGBA lower{ 0.3f * shade, 0.65f * shade, 0.95f, 0.95f * alpha };
        crystal.tri(top, a, b, upper);
        crystal.tri(bottom, b, a, lower);
    }

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, cam.viewProj((float)winW / (float)winH).m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, crystal.v.size() * sizeof(Vertex), crystal.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)crystal.v.size());
}

void Renderer::drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                         float overlayAlpha, const std::vector<std::string>& hud, int winW, int winH) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.55f, 0.7f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    viewProj_ = cam.viewProj((float)winW / (float)winH);
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    // Sprites turn to face a point 200 units behind the camera, along its view axis
    constexpr float BILLBOARD_DISTANCE = 200.0f;
    const float cp = std::cos(cam.pitch);
    billX_ = ex + BILLBOARD_DISTANCE * std::sin(cam.yaw) * cp;
    billZ_ = ez + BILLBOARD_DISTANCE * std::cos(cam.yaw) * cp;

    // 0. Sky: a gradient with a sun, sampled by view direction so it stays fixed while the camera moves
    {
        // View matrix with the eye at the origin, so the sky only follows rotation
        Mat4 proj = perspective(0.9f, (float)winW / (float)winH, 0.1f, 4000.0f);
        Mat4 viewRot = lookAt(0.0f, 0.0f, 0.0f,
                              cam.focusX - ex, cam.focusY - ey, cam.focusZ - ez,
                              0.0f, 1.0f, 0.0f);
        float invSky[16];
        invert4x4(mul(proj, viewRot).m, invSky);

        // Sun, moon, stars and sky colours come from the dev tools settings and the time of day.
        // The sun rises at 06:00, peaks at noon and sets at 18:00. The moon sits opposite the sun.
        const float ang = (dayMinutes_ / 1440.0f - 0.25f) * 2.0f * 3.14159265f;
        const float elev = std::sin(ang);
        float sunDir[3] = { std::cos(ang) * 0.4f, elev, std::cos(ang) * 0.9f };
        const float sunLen = std::sqrt(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] + sunDir[2] * sunDir[2]);
        for (float& c : sunDir) c /= sunLen;
        const float moonDir[3] = { -sunDir[0], -sunDir[1], -sunDir[2] };

        auto smooth = [](float e0, float e1, float x) {
            float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        auto lerp3 = [](const float* p, const float* q, float t, float* out) {
            for (int i = 0; i < 3; ++i) out[i] = p[i] + (q[i] - p[i]) * t;
        };
        const float day = smooth(-0.1f, 0.2f, elev);
        const float dusk = 1.0f - smooth(0.0f, 0.4f, std::fabs(elev));
        const float sunAlpha = smooth(-0.05f, 0.05f, elev);
        const float moonAlpha = sky_.moon ? (1.0f - sunAlpha) : 0.0f;
        const float starAlpha = sky_.stars ? (1.0f - day) * sky_.starBrightness : 0.0f;

        const auto& C = sky_.colors;
        float zen[3], hor[3], horDusk[3], sunC[3];
        lerp3(C[CT_NIGHT_ZENITH].data(), C[CT_DAY_ZENITH].data(), day, zen);
        lerp3(C[CT_NIGHT_HORIZON].data(), C[CT_DAY_HORIZON].data(), day, hor);
        lerp3(hor, C[CT_DUSK_HORIZON].data(), dusk * 0.8f, horDusk);
        lerp3(C[CT_DUSK_SUN].data(), C[CT_SUN].data(), day, sunC);
        for (float& c : sunC) c *= sky_.sunBrightness;

        glUseProgram(skyProgram_);
        glUniformMatrix4fv(uSkyInvVP_, 1, GL_FALSE, invSky);
        glUniform3fv(uSunDir_, 1, sunDir);
        glUniform1f(uSunAlpha_, sunAlpha);
        glUniform3fv(uSunCol_, 1, sunC);
        glUniform3fv(uMoonDir_, 1, moonDir);
        glUniform1f(uMoonAlpha_, moonAlpha);
        glUniform3fv(uMoonCol_, 1, C[CT_MOON].data());
        glUniform1f(uStarAlpha_, starAlpha);
        glUniform3fv(uZenith_, 1, zen);
        glUniform3fv(uHorizon_, 1, horDusk);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(skyVao_);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    // 1. Terrain
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    drawTerrain(t, cam);

    // 2. Sprites and models, each standing on the terrain at its entity's height
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);

    for (Entity e : w.view<Wagon, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        drawModel(wagon_, tr->x, tr->y, tr->z, WAGON_SIZE);
    }

    for (Entity e : w.view<Unit, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        const UnitSprite* us = w.get<UnitSprite>(e);
        int idx = us ? us->id - 1 : 0;
        const bool inRange = idx >= 0 && idx < (int)unitSprites_.size();
        const TextTex& g = (inRange && unitSprites_[idx].tex) ? unitSprites_[idx] : unitFallback_;
        RGBA tint = WHITE;
        if (w.has<Hostile>(e)) tint = RGBA{ 1.0f, 0.3f, 0.3f, 1.0f };
        else if (w.has<Selected>(e)) tint = RGBA{ 1.0f, 1.0f, 0.6f, 1.0f };
        drawGlyph(g, tr->x, tr->y, tr->z, GLYPH_H, tint);
    }

    // 3. Colour overlays: selection rings and the drag box
    auto flush = [&](const Batch& batch, const Mat4& m) {
        glUseProgram(colorProgram_);
        glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, m.m);
        glBindVertexArray(dynVao_);
        glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
        glBufferData(GL_ARRAY_BUFFER, batch.v.size() * sizeof(Vertex), batch.v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.v.size());
    };

    Batch rings;
    for (Entity e : w.view<Unit, Transform, Selected>()) {
        const Transform* tr = w.get<Transform>(e);
        outlineSquare(rings, tr->x, tr->y + 0.03f, tr->z, 0.6f, 0.07f, RGBA{ 0.3f, 1.0f, 0.3f, 1.0f });
    }
    flush(rings, viewProj_);

    if (sel.dragging) {
        Batch box;
        float x0 = std::min(sel.x0, sel.x1), x1 = std::max(sel.x0, sel.x1);
        float y0 = std::min(sel.y0, sel.y1), y1 = std::max(sel.y0, sel.y1);
        RGBA fill{ 0.3f, 1.0f, 0.3f, 0.15f };
        RGBA line{ 0.3f, 1.0f, 0.3f, 0.9f };
        float th = 2.0f;

        box.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, fill);
        box.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y0 + th, 0 }, { x0, y0 + th, 0 }, line);
        box.quad({ x0, y1 - th, 0 }, { x1, y1 - th, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, line);
        box.quad({ x0, y0, 0 }, { x0 + th, y0, 0 }, { x0 + th, y1, 0 }, { x0, y1, 0 }, line);
        box.quad({ x1 - th, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x1 - th, y1, 0 }, line);

        glDisable(GL_DEPTH_TEST);
        flush(box, ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f));
        glEnable(GL_DEPTH_TEST);
    }

    // 4. Level-start image: centred on screen, fading as overlayAlpha drops
    if (overlayAlpha > 0.0f && overlay_.tex) {
        glUseProgram(glyphProgram_);
        Mat4 pixels = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels.m);
        glDisable(GL_DEPTH_TEST);

        // Fit inside the screen while keeping the image's aspect ratio
        float screenAspect = (float)winW / (float)winH;
        float wd, h;
        if (overlay_.aspect > screenAspect) {
            wd = (float)winW;
            h = wd / overlay_.aspect;
        } else {
            h = (float)winH;
            wd = h * overlay_.aspect;
        }
        float x0 = winW * 0.5f - wd * 0.5f, x1 = x0 + wd;
        float y0 = winH * 0.5f - h * 0.5f,  y1 = y0 + h;
        float v[4 * 5] = {
            x0, y1, 0.0f, 0.0f, 1.0f,
            x1, y1, 0.0f, 1.0f, 1.0f,
            x1, y0, 0.0f, 1.0f, 0.0f,
            x0, y0, 0.0f, 0.0f, 0.0f
        };
        drawQuadVerts(overlay_.tex, v, RGBA{ 1.0f, 1.0f, 1.0f, overlayAlpha });

        glEnable(GL_DEPTH_TEST);
    }

    drawHud(hud, winW, winH);
    glBindVertexArray(0);
}

// ---- In-game panels ----

void Renderer::drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units) {
    drawRect(L.panel, RGBA{ 0.05f, 0.05f, 0.08f, 0.85f });

    for (const UnitCell& c : L.cells) {
        const Entity e = units[c.index];
        const bool selected = w.has<Selected>(e);
        const Rect frame{ c.rect.x - 2.0f, c.rect.y - 2.0f, c.rect.w + 4.0f, c.rect.h + 4.0f };
        drawRect(frame, selected ? RGBA{ 1.0f, 0.95f, 0.4f, 1.0f } : RGBA{ 0.25f, 0.25f, 0.3f, 1.0f });
        drawRect(Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, RGBA{ 0.1f, 0.1f, 0.14f, 1.0f });

        // Portrait fitted inside the square, keeping its shape
        const TextTex& g = unitGlyph(w, e);
        if (g.tex) {
            const float sz = c.rect.w;
            float pw = sz, ph = sz;
            if (g.aspect > 1.0f) ph = sz / g.aspect;
            else pw = sz * g.aspect;
            const float x = c.rect.x + (sz - pw) * 0.5f, y = c.rect.y + (sz - ph) * 0.5f;
            const float v[4 * 5] = {
                x,      y + ph, 0.0f, 0.0f, 1.0f,
                x + pw, y + ph, 0.0f, 1.0f, 1.0f,
                x + pw, y,      0.0f, 1.0f, 0.0f,
                x,      y,      0.0f, 0.0f, 0.0f
            };
            glUseProgram(glyphProgram_);
            glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
            drawQuadVerts(g.tex, v, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        }

        // Name under the portrait, shortened so it fits the cell
        const Name* nm = w.get<Name>(e);
        std::string label = nm ? nm->text : std::string();
        if (label.size() > 11) label = label.substr(0, 10) + ".";
        drawListText(label, c.rect.x, c.rect.y + c.rect.w + 2.0f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    if (L.scrollable()) {
        drawRect(L.track, RGBA{ 0.18f, 0.18f, 0.22f, 1.0f });
        drawRect(L.thumb, RGBA{ 0.7f, 0.7f, 0.8f, 1.0f });
    }
}

void Renderer::drawInventorySidebar(const SidebarLayout& L, const Inventory& inv, const std::string& unitName) {
    const float s = uiH_ / 720.0f;
    drawRect(L.panel, RGBA{ 0.06f, 0.06f, 0.09f, 0.92f });
    drawUiText("Inventory", L.panel.x + 14.0f * s, 10.0f * s, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    drawListText(unitName, L.panel.x + 14.0f * s, 38.0f * s, RGBA{ 0.8f, 0.8f, 0.8f, 1.0f });

    for (size_t i = 0; i < L.slots.size() && i < inv.items.size(); ++i) {
        const Rect& r = L.slots[i];
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, RGBA{ 0.3f, 0.3f, 0.36f, 1.0f });
        drawRect(r, RGBA{ 0.1f, 0.1f, 0.13f, 1.0f });
        // An occupied slot shows a marker. Item art will replace this later.
        if (inv.items[i] != 0) {
            drawRect(Rect{ r.x + r.w * 0.25f, r.y + r.h * 0.25f, r.w * 0.5f, r.h * 0.5f }, RGBA{ 0.9f, 0.8f, 0.3f, 1.0f });
        }
    }
}
