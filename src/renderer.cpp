#include "renderer.h"
#include "terrain_mesh.h"
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "pak.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float GLYPH_H = 1.2f;          // world height of unit sprites
constexpr float ITEM_GLYPH_H = 0.32f;    // world height of an item sprite on the ground
constexpr float WAGON_SIZE = 1.5f;       // largest side of the wagon model
constexpr float SKY_SPAN = 0.2f;         // fraction of the panorama visible across the screen
constexpr float TWO_PI = 6.28318530f;

const RGBA WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };
const RGBA ITEM_PINK{ 1.0f, 0.45f, 0.75f, 1.0f };

// Name box over an item on the ground: dark red-brown, as in Kenshi
const RGBA LABEL_FILL{ 0.36f, 0.12f, 0.08f, 0.5f };
const RGBA LABEL_EDGE{ 0.62f, 0.30f, 0.20f, 0.5f };
const RGBA LABEL_TEXT{ 1.0f, 0.93f, 0.86f, 1.0f };

// World point to pixel coordinates. False if it is behind the camera.
bool projectPoint(const Mat4& vp, float x, float y, float z, int W, int H, float& sx, float& sy) {
    float c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = vp.m[0 * 4 + i] * x + vp.m[1 * 4 + i] * y + vp.m[2 * 4 + i] * z + vp.m[3 * 4 + i];
    }
    if (c[3] <= 0.0f) return false;
    sx = (c[0] / c[3] * 0.5f + 0.5f) * (float)W;
    sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * (float)H;
    return true;
}

// Whether the chunk under (x, z) is loaded. Items on unloaded ground are not drawn.
bool groundLoaded(const Terrain& t, float x, float z) {
    const int cx = (int)std::floor(x / CHUNK_TILES), cz = (int)std::floor(z / CHUNK_TILES);
    return t.chunkAt(cx, cz) != nullptr;
}

// Where an item's sprite is on screen: the centre x, the top and bottom, and half its width
struct ItemOnScreen {
    float cx = 0.0f, top = 0.0f, bottom = 0.0f, halfW = 0.0f;
};

// aspect is the width over the height of the item's picture (1 for the pink quad)
bool itemOnScreen(const Mat4& vp, int W, int H, const Transform& tr, float aspect, ItemOnScreen& out) {
    float bx, by, tx, ty, mx, my;
    if (!projectPoint(vp, tr.x, tr.y, tr.z, W, H, bx, by)) return false;
    if (!projectPoint(vp, tr.x, tr.y + ITEM_GLYPH_H, tr.z, W, H, tx, ty)) return false;
    if (!projectPoint(vp, tr.x, tr.y + ITEM_GLYPH_H * 0.5f, tr.z, W, H, mx, my)) return false;
    out.cx = mx;
    out.top = std::min(by, ty);
    out.bottom = std::max(by, ty);
    out.halfW = (out.bottom - out.top) * 0.5f * aspect;
    return true;
}

// The name box sits just above the item's sprite, centred on it
Rect labelBox(const ItemOnScreen& os, float textW, float textH, float s) {
    const float padX = 7.0f * s, padY = 3.0f * s, gapY = 4.0f * s;
    const float w = textW + 2.0f * padX, h = textH + 2.0f * padY;
    return Rect{ os.cx - w * 0.5f, os.top - gapY - h, w, h };
}

// Colour that marks an item's kind in the inventory
RGBA categoryColor(int item) {
    const ItemDef* d = findItem(item);
    if (!d) return RGBA{ 0.5f, 0.5f, 0.5f, 1.0f };
    if (d->typeId == "consumable") return RGBA{ 0.30f, 0.65f, 0.35f, 1.0f };
    if (d->typeId == "weapon")     return RGBA{ 0.75f, 0.30f, 0.28f, 1.0f };
    if (d->typeId == "armor")      return RGBA{ 0.30f, 0.45f, 0.78f, 1.0f };
    if (d->typeId == "accessory")  return RGBA{ 0.85f, 0.72f, 0.30f, 1.0f };
    return RGBA{ 0.55f, 0.46f, 0.34f, 1.0f };   // plain items
}

// The name of an item for display
std::string itemName(int item) {
    const ItemDef* d = findItem(item);
    return d ? d->name : std::string("Unknown item");
}

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

// Like loadImage, but a file that is not packed gives an empty texture with no error logged
TextTex loadOptionalImage(const char* path) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(path, bytes, size)) return {};
    return loadImage(path, false);
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

    // Images and models
    title_ = loadImage("data/ui/title.png", false);
    itemNone_ = loadOptionalImage("data/item/none.png");
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
    {
        // One white texel: items are drawn as this, tinted pink, until they have art
        const unsigned char texel[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &white_.tex);
        glBindTexture(GL_TEXTURE_2D, white_.tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
        white_.aspect = 1.0f;
    }
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
    TextTex* textures[] = { &unitFallback_, &white_, &itemNone_ };
    for (TextTex* g : textures) {
        if (g->tex) glDeleteTextures(1, &g->tex);
        g->tex = 0;
    }
    for (TextTex& g : unitSprites_) {
        if (g.tex) glDeleteTextures(1, &g.tex);
    }
    unitSprites_.clear();
    for (auto& entry : itemPics_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    itemPics_.clear();
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
    TextFont* statsFonts[] = { &statsTitle_, &statsRow_ };
    for (TextFont* f : statsFonts) {
        for (auto& entry : f->lines) {
            if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
        }
        f->lines.clear();
    }
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

// Loading screen: black, with a progress bar across the bottom 4% of the screen (white fill)
void Renderer::drawLoadingScreen(float progress, int winW, int winH, const std::string& label) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    Mat4 pixels = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);

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
    // The UI backgrounds are translucent, so blending must be on. The sea pass in drawTerrain leaves it off.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pixels_ = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
}

void Renderer::endUi() {
    glBindVertexArray(0);
    glDisable(GL_BLEND);   // the world pass draws with blending off, as it did before the UI
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

float Renderer::uiTextHeight(const std::string& s) {
    return uiLine(s).h;
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

// Stats popout fonts: each one keeps its own cache of text textures
void Renderer::setStatsFonts(TTF_Font* title, TTF_Font* rows) {
    // Textures made with the old fonts would be stale, so drop them
    TextFont* fonts[] = { &statsTitle_, &statsRow_ };
    for (TextFont* f : fonts) {
        for (auto& entry : f->lines) {
            if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
        }
        f->lines.clear();
    }
    statsTitle_.font = title;
    statsRow_.font = rows;
}

Renderer::HudLine& Renderer::fontLine(TextFont& f, const std::string& s) {
    auto it = f.lines.find(s);
    if (it != f.lines.end()) return it->second;

    HudLine hl;
    hl.text = s;
    hl.w = 0.0f;
    hl.h = 0.0f;
    int tw = 0, th = 0;
    if (!f.font) return f.lines[s] = hl;
    if (s.find_first_not_of(' ') == std::string::npos) {
        // SDL_ttf fails on text with no width, so blank text gets no texture but keeps its line height
        TTF_GetStringSize(f.font, "Ag", 2, &tw, &th);
        hl.h = (float)th;
        return f.lines[s] = hl;
    }
    TTF_GetStringSize(f.font, s.c_str(), s.size(), &tw, &th);
    hl.tex = makeGlyphTexture(f.font, s.c_str(), SDL_Color{ 255, 255, 255, 255 });
    hl.w = (float)tw;
    hl.h = (float)th;
    return f.lines[s] = hl;
}

void Renderer::drawFontText(TextFont& f, const std::string& s, float x, float y, RGBA tint) {
    HudLine& hl = fontLine(f, s);
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

float Renderer::fontTextWidth(TextFont& f, const std::string& s) {
    return fontLine(f, s).w;
}

float Renderer::fontTextHeight(TextFont& f, const std::string& s) {
    return fontLine(f, s).h;
}

float Renderer::listTextHeight(const std::string& s) {
    return listLine(s).h;
}

float Renderer::listTextWidth(const std::string& s) {
    return listLine(s).w;
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
                    RGBA{ 0.0f, 0.0f, 0.0f, 0.5f });
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
                         const std::vector<std::string>& hud, int winW, int winH) {
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

    drawGroundItems(w, t);

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

    drawHud(hud, winW, winH);
    glBindVertexArray(0);
}

// ---- In-game panels ----

namespace {

// The colour of an item's name by tier: grey at tier 1, rising to gold at tier 6. Tier 0 stays white.
RGBA tierTint(int tier) {
    if (tier <= 0) return RGBA{ 1.0f, 1.0f, 1.0f, 1.0f };
    const float t = std::clamp((tier - 1) / 5.0f, 0.0f, 1.0f);
    const RGBA grey{ 0.62f, 0.62f, 0.66f, 1.0f }, gold{ 1.0f, 0.84f, 0.32f, 1.0f };
    return RGBA{ grey.r + (gold.r - grey.r) * t, grey.g + (gold.g - grey.g) * t,
                 grey.b + (gold.b - grey.b) * t, 1.0f };
}

// 1234567 as "1,234,567"
std::string withCommas(long long n) {
    const std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return n < 0 ? "-" + out : out;
}

} // namespace

// Shortens text to fit maxW. Whole words are kept where possible, so a name is cut at a word and
// "..." is added. Text with no room for one word is cut by character.
std::string Renderer::fitText(const std::string& text, float maxW, bool ui) {
    auto width = [&](const std::string& t) { return ui ? uiTextWidth(t) : listTextWidth(t); };
    if (width(text) <= maxW) return text;

    std::vector<std::string> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        const size_t start = i;
        while (i < text.size() && text[i] != ' ') ++i;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    for (size_t n = words.size(); n > 0; --n) {
        std::string joined;
        for (size_t k = 0; k < n; ++k) joined += (k ? " " : "") + words[k];
        if (width(joined + "...") <= maxW) return joined + "...";
    }

    std::string t = text;
    while (!t.empty() && width(t + "...") > maxW) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "...";
}

// A quad with one colour at the top and another at the bottom
void Renderer::drawRectGradient(const Rect& r, RGBA top, RGBA bottom) {
    const float x0 = r.x, x1 = r.x + r.w, y0 = r.y, y1 = r.y + r.h;
    const Vertex v[6] = {
        { { x0, y0, 0.0f }, top }, { { x1, y0, 0.0f }, top }, { { x1, y1, 0.0f }, bottom },
        { { x0, y0, 0.0f }, top }, { { x1, y1, 0.0f }, bottom }, { { x0, y1, 0.0f }, bottom },
    };
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Every digit takes the width of the widest digit, so numbers that change don't shift sideways.
// Other characters keep their own width.
float Renderer::digitCellWidth() {
    float cell = 0.0f;
    for (char ch = '0'; ch <= '9'; ++ch) cell = std::max(cell, uiTextWidth(std::string(1, ch)));
    return cell;
}

float Renderer::clockTextWidth(const std::string& s) {
    const float cell = digitCellWidth();
    float total = 0.0f;
    for (char ch : s) {
        total += (ch >= '0' && ch <= '9') ? cell : uiTextWidth(std::string(1, ch));
    }
    return total;
}

void Renderer::drawClockText(const std::string& s, float x, float y, RGBA tint) {
    const float cell = digitCellWidth();
    for (char ch : s) {
        const std::string one(1, ch);
        const float cw = uiTextWidth(one);
        if (ch >= '0' && ch <= '9') {
            drawUiText(one, x + (cell - cw) * 0.5f, y, tint);
            x += cell;
        } else {
            drawUiText(one, x, y, tint);
            x += cw;
        }
    }
}

// The tooltip for an item: its name in its tier's colour, then its stats that are not zero
void Renderer::drawItemTooltip(int item, int count, float mx, float my) {
    const ItemDef* d = findItem(item);
    if (!d) return;
    const float s = uiH_ / 720.0f;
    const RGBA grey{ 0.80f, 0.80f, 0.84f, 1.0f };

    std::vector<std::pair<std::string, RGBA>> lines;
    std::string title = d->name;
    if (count > 1) title += " x" + std::to_string(count);
    lines.push_back({ title, tierTint(d->tier) });
    if (d->attackValue > 0) lines.push_back({ "Attack " + std::to_string(d->attackValue), grey });
    if (d->defenseValue > 0) lines.push_back({ "Defense " + std::to_string(d->defenseValue), grey });
    char weight[32];
    std::snprintf(weight, sizeof(weight), "%.1f", d->weight);
    lines.push_back({ std::string("Weight ") + weight + (count > 1 ? " each" : ""), grey });
    if (d->maxDurability > 0) {
        lines.push_back({ "Durability " + std::to_string(d->currentDurability) + " / " +
                          std::to_string(d->maxDurability), grey });
    }

    const float lineH = listTextHeight("Ag"), gap = 2.0f * s, pad = 7.0f * s;
    float textW = 0.0f;
    for (const auto& l : lines) textW = std::max(textW, listTextWidth(l.first));
    const float bw = textW + 2.0f * pad;
    const float bh = lines.size() * lineH + (lines.size() - 1) * gap + 2.0f * pad;
    float bx = mx + 14.0f * s;
    if (bx + bw > uiW_) bx = mx - bw - 14.0f * s;
    float by = my + 12.0f * s;
    if (by + bh > uiH_) by = uiH_ - bh;
    bx = std::max(0.0f, bx);
    by = std::max(0.0f, by);

    drawRect(Rect{ bx, by, bw, bh }, RGBA{ 0.0f, 0.0f, 0.0f, 0.5f });
    float ty = by + pad;
    for (const auto& l : lines) {
        drawListText(l.first, bx + pad, ty, l.second);
        ty += lineH + gap;
    }
}

void Renderer::drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units) {
    const float s = uiH_ / 720.0f;
    const float bgAlpha = 0.5f;
    drawRect(L.panel, RGBA{ 0.05f, 0.05f, 0.08f, bgAlpha });

    for (const UnitCell& c : L.cells) {
        const Entity e = units[c.index];
        const bool selected = w.has<Selected>(e);
        const Rect frame{ c.rect.x - 2.0f, c.rect.y - 2.0f, c.rect.w + 4.0f, c.rect.h + 4.0f };
        // A selected unit's frame is solid purple, so it stands out from the see-through frames
        drawRect(frame, selected ? RGBA{ 0.66f, 0.38f, 1.0f, 1.0f } : RGBA{ 0.25f, 0.25f, 0.3f, bgAlpha });
        drawRect(Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, RGBA{ 0.1f, 0.1f, 0.14f, bgAlpha });

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

        // Health bar under the portrait
        const Needs* nd = w.get<Needs>(e);
        const float frac = (nd && nd->maxHp > 0) ? std::clamp((float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f) : 0.0f;
        const float barY = c.rect.y + c.rect.w + 3.0f * s, barH = 4.0f * s;
        drawRect(Rect{ c.rect.x, barY, c.rect.w, barH }, RGBA{ 0.16f, 0.16f, 0.19f, bgAlpha });
        drawRect(Rect{ c.rect.x, barY, c.rect.w * frac, barH }, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });

        // Name under the bar, cut at a word with "..." to fit the cell
        const Name* nm = w.get<Name>(e);
        const std::string label = fitText(nm ? nm->text : std::string("Unnamed Unit"), c.rect.w, false);
        drawListText(label, c.rect.x, c.rect.y + c.rect.w + 8.0f * s, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    if (L.scrollable()) {
        drawRect(L.track, RGBA{ 0.18f, 0.18f, 0.22f, bgAlpha });
        drawRect(L.thumb, RGBA{ 0.7f, 0.7f, 0.8f, 1.0f });
    }
}

void Renderer::drawInventorySidebar(const SidebarLayout& L, World& w, Entity unit, float mx, float my,
                                    int dimSlot, int dimGear, int dragItem) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA grey{ 0.72f, 0.72f, 0.76f, 1.0f };
    const float bgAlpha = 0.5f;   // every background in the sidebar; text, item colours and bars stay solid
    const Inventory* inv = w.get<Inventory>(unit);
    const Needs needs = w.get<Needs>(unit) ? *w.get<Needs>(unit) : Needs{};
    const Stats stats = w.get<Stats>(unit) ? *w.get<Stats>(unit) : defaultStats();
    const Name* nm = w.get<Name>(unit);
    const Equipment* worn = w.get<Equipment>(unit);

    drawRect(L.panel, RGBA{ 0.04f, 0.04f, 0.06f, bgAlpha });

    // ---- Profile card: the unit's picture, name, health, hunger and carry ----
    drawRect(L.profile, RGBA{ 0.08f, 0.08f, 0.11f, bgAlpha });
    drawRect(L.portrait, RGBA{ 0.10f, 0.10f, 0.14f, bgAlpha });
    const TextTex& g = unitGlyph(w, unit);
    if (g.tex) {
        const float sz = L.portrait.w;
        float pw = sz, ph = sz;
        if (g.aspect > 1.0f) ph = sz / g.aspect;
        else pw = sz * g.aspect;
        const float x = L.portrait.x + (sz - pw) * 0.5f, y = L.portrait.y + (sz - ph) * 0.5f;
        const float v[4 * 5] = {
            x,      y + ph, 0.0f, 0.0f, 1.0f,
            x + pw, y + ph, 0.0f, 1.0f, 1.0f,
            x + pw, y,      0.0f, 1.0f, 0.0f,
            x,      y,      0.0f, 0.0f, 0.0f
        };
        glUseProgram(glyphProgram_);
        glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
        drawQuadVerts(g.tex, v, WHITE);
    }
    // Low health: a red gradient rises from the bottom of the picture by the share of health lost
    if (needs.maxHp > 0 && needs.hp < needs.maxHp) {
        const float lost = std::clamp(1.0f - (float)needs.hp / (float)needs.maxHp, 0.0f, 1.0f);
        const float gh = L.portrait.h * lost;
        if (gh > 0.0f) {
            drawRectGradient(Rect{ L.portrait.x, L.portrait.y + L.portrait.h - gh, L.portrait.w, gh },
                             RGBA{ 0.85f, 0.10f, 0.10f, 0.0f }, RGBA{ 0.85f, 0.10f, 0.10f, 0.65f });
        }
    }

    const float tx = L.portrait.x + L.portrait.w + 12.0f * s;
    const float rightEdge = L.profile.x + L.profile.w - 10.0f * s;
    const float colW = rightEdge - tx;

    const std::string name = nm ? nm->text : std::string("Unnamed Unit");
    float y = L.profile.y + 10.0f * s;
    drawUiText(fitText(name, L.statsButton.x - 8.0f * s - tx, true), tx, y, white);

    // The stats button opens the stats popout
    {
        const bool hot = L.statsButton.contains(mx, my);
        drawRect(L.statsButton, hot ? RGBA{ 0.25f, 0.34f, 0.50f, bgAlpha } : RGBA{ 0.14f, 0.17f, 0.22f, bgAlpha });
        const float tw = listTextWidth("Stats"), th = listTextHeight("Stats");
        drawListText("Stats", L.statsButton.x + (L.statsButton.w - tw) * 0.5f,
                     L.statsButton.y + (L.statsButton.h - th) * 0.5f, white);
    }
    y += uiTextHeight("Ag") + 6.0f * s;

    // A stat: its label and value on one line, and a bar under them. Returns the y for the next stat.
    auto statRow = [&](float rowY, const char* label, const std::string& value, float frac, RGBA fill) {
        drawListText(label, tx, rowY, grey);
        drawListText(value, rightEdge - listTextWidth(value), rowY, white);
        const float barY = rowY + listTextHeight("Ag") + 3.0f * s;
        const float barH = 8.0f * s;
        drawRect(Rect{ tx, barY, colW, barH }, RGBA{ 0.16f, 0.16f, 0.19f, bgAlpha });
        drawRect(Rect{ tx, barY, colW * std::clamp(frac, 0.0f, 1.0f), barH }, fill);
        return barY + barH + 9.0f * s;
    };
    y = statRow(y, "Health", std::to_string(needs.hp) + " / " + std::to_string(needs.maxHp),
                needs.maxHp > 0 ? (float)needs.hp / (float)needs.maxHp : 0.0f, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });
    y = statRow(y, "Hunger", std::to_string(needs.hunger) + " / " + std::to_string(needs.maxHunger),
                needs.maxHunger > 0 ? (float)needs.hunger / (float)needs.maxHunger : 0.0f, RGBA{ 0.86f, 0.62f, 0.22f, 1.0f });

    // Carried weight against what Strength lets the unit carry
    const float carried = inv ? carriedWeight(*inv) : 0.0f;
    const float limit = carryLimit(stats);
    char carry[48];
    std::snprintf(carry, sizeof(carry), "%.1f / %.0f", carried, limit);
    statRow(y, "Carry", carry, limit > 0.0f ? carried / limit : 0.0f, RGBA{ 0.45f, 0.65f, 0.88f, 1.0f });

    // ---- Gear: what the unit wears, four slots to a row, then the totals ----
    const bool offLocked = worn && offHandBlocked(*worn);
    const ItemDef* dragDef = dragItem != 0 ? findItem(dragItem) : nullptr;
    // A two-handed weapon, held or dragged, dims the off hand
    const bool dimOff = offLocked || (dragDef && dragDef->equipSlot == "two_hand");
    drawRect(L.gearPanel, RGBA{ 0.06f, 0.06f, 0.09f, bgAlpha });
    drawListText("Equipment", L.gearPanel.x + 10.0f * s, L.gearPanel.y + 6.0f * s, white);

    // While an item is dragged, each gear slot shows whether it may go there: green if so, red if not.
    // A gear item leaving its own slot is not in the way of itself.
    Equipment eval = worn ? *worn : Equipment{};
    if (dimGear >= 0) eval.items[dimGear] = 0;

    const float inset = 5.0f * s;
    const float listH = listTextHeight("Ag");
    int hoverGear = -1;
    for (size_t gi = 0; gi < L.gear.size() && gi < GEAR_SLOTS; ++gi) {
        const Rect& r = L.gear[gi];
        RGBA edge{ 0.30f, 0.30f, 0.36f, bgAlpha };
        if (dragItem != 0) {
            std::string why;
            const bool fits = canEquip(eval, dragItem, (int)gi, why);
            edge = fits ? RGBA{ 0.35f, 0.90f, 0.45f, 1.0f } : RGBA{ 0.92f, 0.30f, 0.30f, 1.0f };
        }
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, edge);
        drawRect(r, RGBA{ 0.10f, 0.10f, 0.13f, bgAlpha });
        if (r.contains(mx, my)) hoverGear = (int)gi;

        const int item = worn ? worn->items[gi] : 0;
        const bool locked = (int)gi == GEAR_OFF_HAND && offLocked;
        if (item == 0) {
            // An empty slot shows its name, or "Two-handed" for a locked off hand, centred and dimmed
            const char* text = locked ? "Two-handed" : gearSlotName((int)gi);
            const float tw = listTextWidth(text);
            drawListText(text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - listH) * 0.5f,
                         locked ? grey : RGBA{ 0.72f, 0.72f, 0.76f, 0.45f });
            continue;
        }

        drawListText(gearSlotName((int)gi), r.x + inset, r.y + inset, grey);
        const float rowY = r.y + inset + listH + 3.0f * s;
        float alpha = 1.0f;
        if ((int)gi == dimGear) alpha = 0.30f;
        else if ((int)gi == GEAR_OFF_HAND && dimOff) alpha = 0.35f;
        // The small square beside the name is the item's picture, or its kind's colour without one
        const float chip = 12.0f * s;
        const Rect chipRect{ r.x + inset, rowY, chip, chip };
        if (const TextTex* pic = itemTexture(item)) {
            drawFitted(*pic, chipRect, RGBA{ 1.0f, 1.0f, 1.0f, alpha });
        } else {
            RGBA c = categoryColor(item);
            c.a = alpha;
            drawRect(chipRect, c);
        }
        const float textW = r.w - 2.0f * inset - chip - 6.0f * s;
        drawListText(fitText(itemName(item), textW, false), r.x + inset + chip + 6.0f * s,
                     rowY + (chip - listH) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, alpha });
    }

    int attack = 0, defense = 0;
    if (worn) gearTotals(*worn, attack, defense);
    const std::string totals = "Attack " + std::to_string(attack) + "      Defense " + std::to_string(defense);
    drawListText(totals, L.gearPanel.x + 10.0f * s, L.gearPanel.y + L.gearPanel.h - listH - 6.0f * s, white);

    // ---- Inventory grid on its own background ----
    drawRect(L.gridPanel, RGBA{ 0.06f, 0.06f, 0.09f, bgAlpha });
    drawListText("Inventory", L.gridPanel.x + 10.0f * s, L.gridPanel.y + 6.0f * s, white);
    const std::string used = std::to_string(inv ? occupiedSlots(*inv) : 0) + " / " +
                             std::to_string(INVENTORY_SLOTS) + " slots";
    drawListText(used, L.gridPanel.x + L.gridPanel.w - 10.0f * s - listTextWidth(used),
                 L.gridPanel.y + 6.0f * s, grey);

    int hoverSlot = -1;
    for (size_t i = 0; inv && i < L.slots.size() && i < INVENTORY_SLOTS; ++i) {
        const Rect& r = L.slots[i];
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, RGBA{ 0.30f, 0.30f, 0.36f, bgAlpha });
        drawRect(r, RGBA{ 0.10f, 0.10f, 0.13f, bgAlpha });

        const ItemStack& st = inv->slots[i];
        if (st.item == 0 || st.count <= 0) continue;

        // An item shows its picture, or else a block in its kind's colour with its initials. The count sits in the corner.
        RGBA c = categoryColor(st.item);
        if ((int)i == dimSlot) c.a = 0.30f;
        const Rect block{ r.x + r.w * 0.12f, r.y + r.h * 0.12f, r.w * 0.76f, r.h * 0.76f };
        if (const TextTex* pic = itemTexture(st.item)) {
            drawFitted(*pic, block, RGBA{ 1.0f, 1.0f, 1.0f, c.a });
        } else {
            drawRect(block, c);
            const std::string ini = itemInitials(itemName(st.item));
            drawListText(ini, block.x + (block.w - listTextWidth(ini)) * 0.5f,
                         block.y + (block.h - listH) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, c.a });
        }
        if (st.count > 1) {
            const std::string n = std::to_string(st.count);
            drawListText(n, r.x + r.w - listTextWidth(n) - 3.0f * s, r.y + r.h - listH - 1.0f * s, white);
        }
        if (r.contains(mx, my)) hoverSlot = (int)i;
    }

    // Tooltips and highlights are for hovering, not while something is being dragged
    if (dragItem == 0 && hoverSlot >= 0 && (int)hoverSlot != dimSlot) {
        const Rect& r = L.slots[hoverSlot];
        drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.18f });
        drawItemTooltip(inv->slots[hoverSlot].item, inv->slots[hoverSlot].count, mx, my);
    }
    if (dragItem == 0 && hoverGear >= 0 && hoverGear != dimGear && worn && worn->items[hoverGear] != 0) {
        const Rect& r = L.gear[hoverGear];
        drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.18f });
        drawItemTooltip(worn->items[hoverGear], 1, mx, my);
    }
}

void Renderer::drawDraggedStack(float mx, float my, int item, int count) {
    const float s = uiH_ / 720.0f;
    const float sz = 40.0f * s;
    const Rect r{ mx - sz * 0.5f, my - sz * 0.5f, sz, sz };
    if (const TextTex* pic = itemTexture(item)) {
        drawFitted(*pic, r, RGBA{ 1.0f, 1.0f, 1.0f, 0.85f });
    } else {
        RGBA c = categoryColor(item);
        c.a = 0.85f;
        drawRect(r, c);
    }
    if (count > 1) {
        const std::string n = std::to_string(count);
        drawListText(n, r.x + r.w - listTextWidth(n) - 3.0f * s, r.y + r.h - listTextHeight(n) - 1.0f * s, WHITE);
    }
    const std::string name = itemName(item);
    drawListText(name, r.x + r.w + 6.0f * s, my - listTextHeight(name) * 0.5f, WHITE);
}

void Renderer::drawGroundItems(World& w, const Terrain& t) {
    for (Entity e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z)) continue;
        const GroundItem* gi = w.get<GroundItem>(e);
        if (const TextTex* pic = itemTexture(gi->item)) {
            drawGlyph(*pic, tr->x, tr->y, tr->z, ITEM_GLYPH_H, WHITE);
        } else {
            drawGlyph(white_, tr->x, tr->y, tr->z, ITEM_GLYPH_H, ITEM_PINK);
        }
    }
}

const TextTex* Renderer::itemTexture(int item) {
    auto it = itemPics_.find(item);
    if (it == itemPics_.end()) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/item/%d.png", item);
        it = itemPics_.emplace(item, loadOptionalImage(path)).first;
    }
    if (it->second.tex) return &it->second;
    return itemNone_.tex ? &itemNone_ : nullptr;
}

void Renderer::drawClock(const std::string& clock, const std::string& date, float right, int winH) {
    const float s = winH / 720.0f;
    const float pad = 10.0f * s, margin = 12.0f * s, lineGap = 4.0f * s;
    const RGBA fill{ 0.04f, 0.04f, 0.05f, 0.5f };
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA grey{ 0.78f, 0.78f, 0.82f, 1.0f };

    const float clockW = clockTextWidth(clock), clockH = uiTextHeight("Ag");
    const float dateW = listTextWidth(date), dateH = listTextHeight("Ag");
    const float w = std::max(clockW, dateW) + 2.0f * pad;
    const float h = clockH + lineGap + dateH + 2.0f * pad;
    const Rect box{ right - margin - w, margin, w, h };
    // Both lines are right aligned to the same edge
    const float textRight = box.x + box.w - pad;

    drawRect(box, fill);
    drawClockText(clock, textRight - clockW, box.y + pad, white);
    drawListText(date, textRight - dateW, box.y + pad + clockH + lineGap, grey);
}

void Renderer::drawNotice(const std::string& text, int winW, int winH) {
    if (text.empty()) return;
    const float s = winH / 720.0f;
    const float pad = 12.0f * s, top = 14.0f * s;
    const float w = uiTextWidth(text) + 2.0f * pad;
    const float h = uiTextHeight("Ag") + pad;
    const Rect box{ (winW - w) * 0.5f, top, w, h };
    drawRect(box, RGBA{ 0.04f, 0.04f, 0.05f, 0.5f });
    drawUiText(text, box.x + pad, top + pad * 0.5f, RGBA{ 1.0f, 1.0f, 0.6f, 1.0f });
}

StatsPanelLayout Renderer::statsPanelLayout(int winW, int winH) {
    const float s = winH / 720.0f;
    return layoutStatsPanel(winW, winH, fontTextHeight(statsRow_, "Ag") + 1.0f * s, fontTextHeight(statsTitle_, "Ag"));
}

void Renderer::drawStatsPanel(const StatsPanelLayout& L, const std::string& name, const Stats& stats,
                              float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA grey{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.80f, 0.86f, 0.96f, 1.0f };
    const float bgAlpha = 0.5f;
    const float rowH = fontTextHeight(statsRow_, "Ag") + 1.0f * s;
    const float pad = L.left.x - L.box.x;

    drawRect(L.box, RGBA{ 0.04f, 0.05f, 0.08f, bgAlpha });
    const std::string caption = "Stats: " + name;
    drawFontText(statsTitle_, caption, L.box.x + (L.box.w - fontTextWidth(statsTitle_, caption)) * 0.5f,
                 L.box.y + pad, white);

    // Columns: the name at the left, then the level, the total XP and the XP still to go, right aligned
    auto row = [&](const Rect& col, float rowY, const std::string& a, const std::string& b,
                   const std::string& c, const std::string& d, RGBA tint) {
        drawFontText(statsRow_, a, col.x, rowY, tint);
        drawFontText(statsRow_, b, col.x + 200.0f - fontTextWidth(statsRow_, b), rowY, tint);
        drawFontText(statsRow_, c, col.x + 312.0f - fontTextWidth(statsRow_, c), rowY, tint);
        drawFontText(statsRow_, d, col.x + col.w - fontTextWidth(statsRow_, d), rowY, tint);
    };
    auto column = [&](const Rect& col, const std::vector<StatGroup>& groups) {
        float rowY = col.y;
        row(col, rowY, "Stat", "Level", "Total XP", "To next", grey);
        rowY += rowH;
        for (StatGroup group : groups) {
            const char* title = group == StatGroup::Attributes ? "Attributes"
                              : group == StatGroup::Combat ? "Combat"
                              : group == StatGroup::Gathering ? "Resource Gathering"
                              : group == StatGroup::Crafting ? "Crafting" : "Utility";
            drawFontText(statsRow_, title, col.x, rowY, heading);
            rowY += rowH;
            for (int i = 0; i < STAT_COUNT; ++i) {
                if (statInfo(i).group != group) continue;
                const int lv = levelOf(stats, i);
                const std::string next = lv >= MAX_LEVEL ? std::string("Max")
                                       : withCommas(xpForLevel(lv + 1) - stats.xp[i]);
                row(col, rowY, statInfo(i).name, std::to_string(lv), withCommas(stats.xp[i]), next, white);
                rowY += rowH;
            }
        }
    };
    column(L.left, { StatGroup::Attributes, StatGroup::Combat });
    column(L.right, { StatGroup::Gathering, StatGroup::Crafting, StatGroup::Utility });

    const bool hot = L.close.contains(mx, my);
    drawRect(L.close, hot ? RGBA{ 0.25f, 0.34f, 0.50f, bgAlpha } : RGBA{ 0.14f, 0.17f, 0.22f, bgAlpha });
    const float tw = fontTextWidth(statsRow_, "Close"), th = fontTextHeight(statsRow_, "Close");
    drawFontText(statsRow_, "Close", L.close.x + (L.close.w - tw) * 0.5f, L.close.y + (L.close.h - th) * 0.5f, white);
}

ItemMenuLayout Renderer::itemMenuLayout(float x, float y, int buttonCount, int winW, int winH) {
    return layoutItemMenu(x, y, buttonCount, listTextHeight("Ag"), winW, winH);
}

void Renderer::drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my) {
    const float bgAlpha = 0.5f;
    drawRect(L.box, RGBA{ 0.04f, 0.04f, 0.06f, bgAlpha });
    for (size_t i = 0; i < L.buttons.size() && i < labels.size(); ++i) {
        const Rect& r = L.buttons[i];
        const bool hot = r.contains(mx, my);
        drawRect(r, hot ? RGBA{ 0.25f, 0.34f, 0.50f, bgAlpha } : RGBA{ 0.14f, 0.17f, 0.22f, bgAlpha });
        const float tw = listTextWidth(labels[i]), th = listTextHeight(labels[i]);
        drawListText(labels[i], r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

void Renderer::drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z)) continue;

        ItemOnScreen os;
        const TextTex* pic = itemTexture(gi->item);
        if (!itemOnScreen(vp, winW, winH, *tr, pic ? pic->aspect : white_.aspect, os)) continue;
        const std::string name = itemName(gi->item);
        const Rect box = labelBox(os, listTextWidth(name), listTextHeight(name), s);
        if (box.x + box.w < 0.0f || box.x > winW || box.y + box.h < 0.0f || box.y > winH) continue;

        drawRect(Rect{ box.x - 1.0f, box.y - 1.0f, box.w + 2.0f, box.h + 2.0f }, LABEL_EDGE);
        drawRect(box, LABEL_FILL);
        drawListText(name, box.x + 7.0f * s, box.y + 3.0f * s, LABEL_TEXT);
    }
}

void Renderer::drawUnitPicture(int sprite, const Rect& r) {
    const int idx = sprite - 1;
    const bool inRange = idx >= 0 && idx < (int)unitSprites_.size();
    const TextTex& g = (inRange && unitSprites_[idx].tex) ? unitSprites_[idx] : unitFallback_;
    drawFitted(g, r, WHITE);
}

void Renderer::drawFitted(const TextTex& g, const Rect& r, RGBA tint) {
    if (!g.tex) return;

    // Fit the picture inside r without changing its shape
    const float aspect = g.aspect > 0.0f ? g.aspect : 1.0f;
    float w = r.h * aspect, h = r.h;
    if (w > r.w) {
        w = r.w;
        h = r.w / aspect;
    }
    const float x = r.x + (r.w - w) * 0.5f, y = r.y + (r.h - h) * 0.5f;
    float v[4 * 5] = {
        x,     y + h, 0.0f, 0.0f, 1.0f,
        x + w, y + h, 0.0f, 1.0f, 1.0f,
        x + w, y,     0.0f, 1.0f, 0.0f,
        x,     y,     0.0f, 0.0f, 0.0f
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(g.tex, v, tint);
}

void Renderer::drawUnitNames(World& w, const Camera& cam, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA player{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA hostile{ 1.0f, 0.55f, 0.50f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    for (Entity u : w.view<Unit, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        const Name* nm = w.get<Name>(u);
        // The tag sits just above the top of the unit's picture
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, tr->y + GLYPH_H + 0.1f, tr->z, winW, winH, sx, sy)) continue;

        const std::string tag = "[" + nm->text + "]";
        const float tw = listTextWidth(tag), th = listTextHeight(tag);
        if (sx < -tw || sx > winW + tw || sy < -th || sy > winH + th) continue;
        const float x = sx - tw * 0.5f, y = sy - th;
        // The drop shadow is the same text in black, a little down and to the right
        drawListText(tag, x + 1.5f * s, y + 1.5f * s, shadow);
        drawListText(tag, x, y, w.has<Hostile>(u) ? hostile : player);
    }
}

void Renderer::drawPartySetup(const std::vector<Button>& buttons, const Party& p, const PartyUi& ui,
                              const PartyPanels& P, const std::string& mapName, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA grey{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA faint{ 0.45f, 0.45f, 0.50f, 1.0f };
    const RGBA heading{ 0.80f, 0.86f, 0.96f, 1.0f };
    const RGBA hover{ 1.0f, 1.0f, 0.6f, 1.0f };
    const RGBA warn{ 1.0f, 0.72f, 0.55f, 1.0f };
    const RGBA panel{ 0.05f, 0.06f, 0.09f, 0.92f };
    const RGBA plate{ 0.12f, 0.14f, 0.19f, 0.95f };
    const RGBA plateHot{ 0.20f, 0.25f, 0.36f, 0.95f };
    const RGBA plateOn{ 0.28f, 0.22f, 0.44f, 0.95f };    // the purple used for the selected unit
    const RGBA plateOff{ 0.10f, 0.10f, 0.12f, 0.70f };
    const float lineH = listTextHeight("Ag");
    const PartyMember& picked = pickedMember(p, ui);

    // Background for a plate: purple when chosen, lighter when hovered, dim when disabled
    auto plateFor = [&](bool hot, bool on, bool enabled) -> RGBA {
        if (!enabled) return plateOff;
        if (on) return plateOn;
        return hot ? plateHot : plate;
    };
    // Text centred in a box, shortened to fit
    auto centred = [&](const Rect& r, const std::string& text, RGBA tint) {
        const std::string t = fitText(text, r.w - 8.0f * s, false);
        drawListText(t, r.x + (r.w - listTextWidth(t)) * 0.5f, r.y + (r.h - lineH) * 0.5f, tint);
    };
    // Text at the left of a box, vertically centred, after an inset
    auto leftText = [&](const Rect& r, const std::string& text, RGBA tint, float inset) {
        const std::string t = fitText(text, r.w - inset - 6.0f * s, false);
        drawListText(t, r.x + inset, r.y + (r.h - lineH) * 0.5f, tint);
    };

    drawRect(P.left, panel);
    drawRect(P.centre, panel);
    drawRect(P.right, panel);
    drawRect(P.bottom, RGBA{ 0.03f, 0.03f, 0.05f, 0.95f });
    drawUiTextCentered("Starting party: " + mapName, uiW_ * 0.5f, (P.topH - uiTextHeight("Ag")) * 0.5f, white);

    for (const Button& b : buttons) {
        const bool hot = b.enabled && b.rect.contains(mx, my);
        switch (b.action) {
        case Action::PartyPick: {
            const PartyMember& u = p[static_cast<size_t>(b.param)];
            const bool chosen = b.param == std::clamp(ui.selected, 0, START_UNIT_COUNT - 1);
            drawRect(b.rect, plateFor(hot, chosen, true));
            const float pic = b.rect.h - 14.0f * s;
            drawUnitPicture(u.sprite, Rect{ b.rect.x + 7.0f * s, b.rect.y + 7.0f * s, pic, pic });
            const float tx = b.rect.x + pic + 16.0f * s, tw = b.rect.x + b.rect.w - tx - 8.0f * s;
            const bool named = !cleanName(u.name).empty();
            const std::string shown = named ? cleanName(u.name) : std::string("No name");
            drawListText(fitText(shown, tw, false), tx, b.rect.y + 22.0f * s,
                         !named ? warn : chosen ? hover : white);
            drawListText("Sprite " + std::to_string(u.sprite), tx, b.rect.y + 22.0f * s + lineH + 6.0f * s, grey);
            break;
        }

        case Action::PartyTabPick: {
            const bool on = b.param == (int)ui.tab;
            drawRect(b.rect, plateFor(hot, on, true));
            centred(b.rect, b.label, on ? hover : white);
            break;
        }

        case Action::PartyNameField: {
            drawListText("Name", b.rect.x, b.rect.y - 22.0f * s, grey);
            drawRect(b.rect, plateFor(hot, ui.editingName, true));
            if (b.label.empty() && !ui.editingName) {
                leftText(b.rect, "Click to type a name", faint, 10.0f * s);
            } else {
                leftText(b.rect, b.label + (ui.editingName ? "|" : ""), white, 10.0f * s);
            }
            break;
        }

        case Action::PartySpriteBox:
            drawListText("Sprite", b.rect.x, b.rect.y - 22.0f * s, grey);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Sprite, true));
            leftText(b.rect, b.label, white, 10.0f * s);
            break;

        case Action::PartyRandomUnit:
            drawRect(b.rect, plateFor(hot, false, true));
            centred(b.rect, b.label, white);
            break;

        case Action::PartyGearSlot: {
            const bool open = ui.dropKind == DropKind::Gear && ui.dropSlot == b.param;
            drawRect(b.rect, plateFor(hot, open, true));
            leftText(Rect{ b.rect.x, b.rect.y + 4.0f * s, b.rect.w, lineH }, gearSlotName(b.param), grey, 8.0f * s);
            leftText(Rect{ b.rect.x, b.rect.y + b.rect.h - lineH - 4.0f * s, b.rect.w, lineH }, b.label,
                     b.label == "Empty" ? faint : white, 8.0f * s);
            break;
        }

        case Action::PartyStatLabel: {
            drawRect(b.rect, RGBA{ 0.0f, 0.0f, 0.0f, 0.18f });
            leftText(b.rect, b.label, white, 6.0f * s);
            const std::string lv = std::to_string(levelOf(picked.stats, b.param));
            drawListText(lv, b.rect.x + b.rect.w - 6.0f * s - listTextWidth(lv),
                         b.rect.y + (b.rect.h - lineH) * 0.5f, hover);
            break;
        }

        case Action::PartyStep:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centred(b.rect, b.label, b.enabled ? white : faint);
            break;

        case Action::PartyHeading:
            leftText(b.rect, b.label, heading, 0.0f);
            break;

        case Action::PartyNote:
            leftText(b.rect, b.label, grey, 0.0f);
            break;

        case Action::PartyInvSlot: {
            const ItemStack& st = picked.items[b.param];
            drawRect(b.rect, plateFor(hot, b.param == ui.stackSlot, true));
            if (st.item > 0) {
                if (const TextTex* pic = itemTexture(st.item)) {
                    drawFitted(*pic, Rect{ b.rect.x + b.rect.w * 0.12f, b.rect.y + b.rect.h * 0.12f,
                                           b.rect.w * 0.76f, b.rect.h * 0.76f }, white);
                } else {
                    centred(b.rect, itemInitials(itemNameOf(st.item)), white);
                }
                const std::string n = std::to_string(st.count);
                drawListText(n, b.rect.x + b.rect.w - listTextWidth(n) - 3.0f * s,
                             b.rect.y + b.rect.h - lineH - 2.0f * s, grey);
            }
            break;
        }

        case Action::PartyCount:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centred(b.rect, b.label, b.enabled ? white : faint);
            break;

        case Action::PartyMessage:
            centred(b.rect, b.label, partyNamesValid(p) ? grey : warn);
            break;

        case Action::PartyDropBack:
            drawRect(Rect{ b.rect.x - 1.0f, b.rect.y - 1.0f, b.rect.w + 2.0f, b.rect.h + 2.0f }, RGBA{ 0.45f, 0.50f, 0.70f, 0.9f });
            drawRect(b.rect, RGBA{ 0.04f, 0.05f, 0.08f, 0.98f });
            break;

        case Action::PartyDropScroll:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centred(b.rect, b.label, b.enabled ? (hot ? hover : white) : faint);
            break;

        case Action::PartyDropPick:
            if (hot) drawRect(b.rect, plateHot);
            leftText(b.rect, b.label, hot ? hover : white, 10.0f * s);
            break;

        case Action::PartyRandomAll:
        case Action::PartyConfirm:
        case Action::Back:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centred(b.rect, b.label, b.enabled ? white : faint);
            break;

        default:
            break;
        }
    }

    // Middle: the picked unit, large, with its name and totals under it
    const float cw = P.centre.w;
    drawUnitPicture(picked.sprite, Rect{ P.centre.x + 20.0f * s, P.centre.y + 20.0f * s, cw - 40.0f * s,
                                         P.centre.h - 130.0f * s });
    const bool named = !cleanName(picked.name).empty();
    const std::string name = named ? cleanName(picked.name) : std::string("No name");
    drawUiTextCentered(fitText(name, cw - 24.0f * s, true), P.centre.x + cw * 0.5f,
                       P.centre.y + P.centre.h - 100.0f * s, named ? white : warn);
    int attack = 0, defense = 0;
    gearTotals(wornBy(picked), attack, defense);
    const std::string totals = "Attack " + std::to_string(attack) + "    Defence " + std::to_string(defense)
                             + "    Carry " + std::to_string((int)carryLimit(picked.stats));
    const std::string shownTotals = fitText(totals, cw - 24.0f * s, false);
    drawListText(shownTotals, P.centre.x + (cw - listTextWidth(shownTotals)) * 0.5f,
                 P.centre.y + P.centre.h - 62.0f * s, grey);
}

Entity Renderer::groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z)) continue;

        ItemOnScreen os;
        const TextTex* pic = itemTexture(gi->item);
        if (!itemOnScreen(vp, winW, winH, *tr, pic ? pic->aspect : white_.aspect, os)) continue;
        const std::string name = itemName(gi->item);
        const Rect box = labelBox(os, listTextWidth(name), listTextHeight(name), s);
        const bool onLabel = box.contains(mx, my);
        const bool onSprite = mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom;
        if (!onLabel && !onSprite) continue;

        // The item nearest the mouse wins when several overlap
        const float cy = (os.top + os.bottom) * 0.5f;
        const float d2 = (mx - os.cx) * (mx - os.cx) + (my - cy) * (my - cy);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = e;
            bestD2 = d2;
        }
    }
    return best;
}
