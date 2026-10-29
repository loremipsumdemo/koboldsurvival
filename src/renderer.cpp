#include "renderer.h"
#include "language.h"
#include "terrain_mesh.h"
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "pak.h"
#include "party.h"
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
// The name under the mouse: a darker cell with gold text
const RGBA LABEL_HOT_FILL{ 0.12f, 0.04f, 0.02f, 0.85f };
const RGBA LABEL_HOT_TEXT{ 1.0f, 0.80f, 0.25f, 1.0f };

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

// Where an item's sprite is on screen: the center x, the top and bottom, and half its width
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

// Color that marks an item's kind in the inventory
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
    return d ? itemDisplayName(*d) : std::string(lang("MISC_UNKNOWN_ITEM"));
}

struct V3 { float x, y, z; };
struct Vertex { V3 p; RGBA c; };         // position, then color

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

// Position (location 0) and color (location 1), for Vertex
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

// Colored triangles rebuilt every frame (selection rings, drag box)
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

// A texture from RGBA pixels, clamped at its edges and filtered linearly
static GLuint MakeRgbaTexture(int w, int h, const uint8_t* rgba) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

// A round green dot with a black outline, 16 pixels across, for the units on the maps
static GLuint MakeDotTexture(bool white = false) {
    constexpr int N = 16;
    std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float dx = x + 0.5f - N * 0.5f, dy = y + 0.5f - N * 0.5f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const bool green = d < 5.0f;             // inside is green, the ring out to the edge is black
            const float alpha = std::clamp(7.6f - d, 0.0f, 1.0f);
            uint8_t* p = &px[static_cast<size_t>(y * N + x) * 4];
            p[0] = green ? (white ? 255 : 64) : 0;
            p[1] = green ? (white ? 255 : 230) : 0;
            p[2] = green ? (white ? 255 : 80) : 0;
            p[3] = static_cast<uint8_t>(alpha * 255.0f);
        }
    }
    return MakeRgbaTexture(N, N, px.data());
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
uniform vec3 uSunCol;     // color of the sun's glow and disc
uniform vec3 uMoonDir;    // unit vector towards the moon, opposite the sun
uniform float uMoonAlpha; // 0 when the moon is hidden
uniform vec3 uMoonCol;
uniform float uStarAlpha; // 0 when there are no stars
uniform vec3 uZenith;     // sky color straight up
uniform vec3 uHorizon;    // sky color at the horizon
out vec4 FragColor;
void main() {
    vec3 d = normalize(vDir);
    // Gradient: the horizon color blends up to the zenith color
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

    if (!initTerrainGl()) return false;

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
    unitSprites_.reserve(UNIT_SPRITE_COUNT);
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/unit/%02d.png", i);
        unitSprites_.push_back(loadImage(path, false));
        report(0.05f + 0.75f * (float)i / (float)UNIT_SPRITE_COUNT);
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
    releaseTerrainGl();
    colorProgram_ = glyphProgram_ = 0;
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

    // Scale, then move so the center of the bottom sits on (x, y, z)
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
    // Combat log lines are new strings in every fight, so the list cache would grow for the whole session.
    // It is cleared here, once a frame, when no text reference is held: anything drawn again is rebuilt.
    constexpr size_t LIST_CACHE_LIMIT = 512;
    if (listText_.size() > LIST_CACHE_LIMIT) {
        for (auto& entry : listText_) {
            if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
        }
        listText_.clear();
    }
    uiW_ = winW;
    uiH_ = winH;
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);   // a clip left on by an earlier pass would cut off the menus
    // The UI backgrounds are translucent, so blending must be on. The sea pass in drawTerrain leaves it off.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pixels_ = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
}

void Renderer::endUi() {
    drawTip();
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);   // the world pass draws with blending off
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

// Automatic text fit. Text is drawn no smaller than this share of its size, and anything still too long is then
// shortened with "...".
static const float kMinTextFit = 0.6f;

// The text as it will be drawn in a box boxW wide: shortened only if it cannot fit even at the smallest size
std::string Renderer::fitShrinkText(const std::string& s, float boxW) {
    return fitText(s, boxW / kMinTextFit, true);
}

// The factor a line of ui text is drawn at to fit a box: 1 when it fits, otherwise smaller, never below kMinTextFit
float Renderer::fitShrinkScale(const std::string& shown, float boxW, float boxH) {
    const HudLine& hl = uiLine(shown);
    if (hl.w <= 0.0f || hl.h <= 0.0f) return 1.0f;
    float k = 1.0f;
    if (hl.w > boxW) k = std::min(k, boxW / hl.w);
    if (hl.h > boxH) k = std::min(k, boxH / hl.h);
    return std::max(k, kMinTextFit);
}

// Draws ui text with its top-left corner at (x, y), at k times its size
void Renderer::drawUiTextScaled(const std::string& s, float x, float y, float k, RGBA tint) {
    HudLine& hl = uiLine(s);
    if (!hl.tex.tex) return;
    const float w = hl.w * k, h = hl.h * k;
    float v[4 * 5] = {
        x,     y + h, 0.0f, 0.0f, 1.0f,
        x + w, y + h, 0.0f, 1.0f, 1.0f,
        x + w, y,     0.0f, 1.0f, 0.0f,
        x,     y,     0.0f, 0.0f, 0.0f
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(hl.tex.tex, v, tint);
}

// Draws ui text centred in a box, shrunk to fit it when automatic text fit is on
void Renderer::drawUiTextInBox(const std::string& s, const Rect& box, RGBA tint) {
    const std::string shown = fitShrinkText(s, box.w);
    if (shown != s) noteTip(s, box);
    const float k = fitShrinkScale(shown, box.w, box.h);
    const float w = uiTextWidth(shown) * k, h = uiTextHeight(shown) * k;
    drawUiTextScaled(shown, box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, k, tint);
}

// Sets the tooltip to the full text if the mouse is over the area the text was drawn in
void Renderer::noteTip(const std::string& full, const Rect& area) {
    if (area.contains(mouseX_, mouseY_)) tip_ = full;
}

// Draws the tooltip set this frame, beside the mouse and kept on screen, with its text wrapped
void Renderer::drawTip() {
    if (tip_.empty()) return;
    const float s = uiH_ / 720.0f;
    const float pad = 6.0f * s;
    const float maxW = std::min(360.0f * s, uiW_ * 0.5f);
    const std::vector<std::string> lines = wrapListText(tip_, maxW - 2.0f * pad);
    float w = 0.0f;
    for (const std::string& ln : lines) w = std::max(w, listTextWidth(ln));
    w += 2.0f * pad;
    const float lh = listTextHeight("Ag");
    const float h = (float)lines.size() * lh + 2.0f * pad;
    float x = mouseX_ + 14.0f * s, y = mouseY_ + 18.0f * s;
    if (x + w > uiW_) x = std::max(0.0f, uiW_ - w);
    if (y + h > uiH_) y = std::max(0.0f, mouseY_ - h - 4.0f * s);
    drawRect(Rect{ x, y, w, h }, RGBA{ 0.05f, 0.05f, 0.08f, 0.96f });
    float ty = y + pad;
    for (const std::string& ln : lines) {
        drawListText(ln, x + pad, ty, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        ty += lh;
    }
    tip_.clear();
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
// Swaps every text font at once, for the Font size setting. Textures made with the old fonts are dropped.
void Renderer::setFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow) {
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
    hudFont_ = hud;
    uiFont_ = ui;
    listFont_ = list;
    setStatsFonts(statsTitle, statsRow);
}

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
    drawUiTextInBox(label, r, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
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
    // A small inset keeps the text off the button's edge
    const float s = uiH_ / 720.0f;
    drawUiTextInBox(label, Rect{ r.x + 4.0f * s, r.y + 2.0f * s, r.w - 8.0f * s, r.h - 4.0f * s }, fg);
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
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
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
    float y = hudTop_;
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
        Mat4 proj = cam.projection((float)winW / (float)winH);
        Mat4 viewRot = lookAt(0.0f, 0.0f, 0.0f,
                              cam.focusX - ex, cam.focusY - ey, cam.focusZ - ez,
                              0.0f, 1.0f, 0.0f);
        float invSky[16];
        invert4x4(mul(proj, viewRot).m, invSky);

        // Sun, moon, stars and sky colors come from the sky settings and the time of day.
        // The sun rises at 06:00, peaks at noon and sets at 18:00. The moon sits opposite the sun.
        const float ang = (dayMinutes_ / 1440.0f - 0.25f) * 2.0f * 3.14159265f;
        const float elev = std::sin(ang);
        float sunDir[3] = { std::cos(ang) * 0.4f, elev, std::cos(ang) * 0.9f };
        const float sunLen = std::sqrt(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] + sunDir[2] * sunDir[2]);
        for (float& c : sunDir) c /= sunLen;
        const float moonDir[3] = { -sunDir[0], -sunDir[1], -sunDir[2] };
        skySun_[0] = sunDir[0];
        skySun_[1] = sunDir[1];
        skySun_[2] = sunDir[2];

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
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        drawModel(wagon_, tr->x, tr->y, tr->z, WAGON_SIZE);
    }

    for (Entity e : w.view<Unit, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const UnitSprite* us = w.get<UnitSprite>(e);
        int idx = us ? us->id - 1 : 0;
        const bool inRange = idx >= 0 && idx < (int)unitSprites_.size();
        const TextTex& g = (inRange && unitSprites_[idx].tex) ? unitSprites_[idx] : unitFallback_;
        RGBA tint = WHITE;
        if (w.has<Hostile>(e)) tint = RGBA{ 1.0f, 0.3f, 0.3f, 1.0f };
        else if (w.has<Neutral>(e)) tint = WHITE;   // neutral sprites are never tinted
        else if (w.has<Selected>(e)) tint = RGBA{ 1.0f, 1.0f, 0.6f, 1.0f };
        drawGlyph(g, tr->x, tr->y, tr->z, GLYPH_H, tint);
    }

    drawGroundItems(w, t, cam);

    // 3. Color overlays: selection rings and the drag box
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
        RGBA fill{ 0.3f, 1.0f, 0.3f, 0.30f };
        RGBA line{ 0.3f, 1.0f, 0.3f, 1.0f };
        float th = 2.0f;

        box.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, fill);
        box.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y0 + th, 0 }, { x0, y0 + th, 0 }, line);
        box.quad({ x0, y1 - th, 0 }, { x1, y1 - th, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, line);
        box.quad({ x0, y0, 0 }, { x0 + th, y0, 0 }, { x0 + th, y1, 0 }, { x0, y1, 0 }, line);
        box.quad({ x1 - th, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x1 - th, y1, 0 }, line);

        // Blending must be on for the box's fill to be 30% see-through; the world pass leaves it off
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        flush(box, ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f));
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    drawHud(hud, winW, winH);
    glBindVertexArray(0);
}

// ---- In-game panels ----

namespace {

// The color of an item's name by tier: gray at tier 1, rising to gold at tier 6. Tier 0 stays white.
RGBA tierTint(int tier) {
    if (tier <= 0) return RGBA{ 1.0f, 1.0f, 1.0f, 1.0f };
    const float t = std::clamp((tier - 1) / 5.0f, 0.0f, 1.0f);
    const RGBA gray{ 0.62f, 0.62f, 0.66f, 1.0f }, gold{ 1.0f, 0.84f, 0.32f, 1.0f };
    return RGBA{ gray.r + (gold.r - gray.r) * t, gray.g + (gold.g - gray.g) * t,
                 gray.b + (gold.b - gray.b) * t, 1.0f };
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

// A quad with one color at the top and another at the bottom
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

// The tooltip for an item: its name in its tier's color, then its stats that are not zero
// Colors for the parts of a tooltip. The change colors match the green and red of the combat text.
namespace {
const RGBA GOOD_CHANGE{ 0.42f, 0.95f, 0.45f, 1.0f };
const RGBA BAD_CHANGE{ 1.0f, 0.38f, 0.36f, 1.0f };
const RGBA TIP_TEXT{ 0.80f, 0.80f, 0.84f, 1.0f };
const RGBA TIP_HEADING{ 0.62f, 0.72f, 0.90f, 1.0f };
const RGBA TIP_WARNING{ 1.0f, 0.72f, 0.35f, 1.0f };

// The numbers a tooltip compares, summed over the items an item would replace
struct GearSum {
    int attack = 0, defense = 0, durability = 0;
    float weight = 0.0f;
};

GearSum sumGear(const std::vector<int>& items) {
    GearSum s;
    for (int id : items) {
        const ItemDef* d = findItem(id);
        if (!d) continue;
        s.attack += d->attackValue;
        s.defense += d->defenseValue;
        s.weight += d->weight;
        if (d->maxDurability > 0) s.durability += d->currentDurability;
    }
    return s;
}

// The rows of an item's tooltip. With a baseline (the gear it would replace), each number that changes
// shows the change in brackets: green when better, red when worse. A lighter weight is better.
// Wraps a line of tooltip text to the tooltip's text width
using TipWrap = std::function<std::vector<std::string>(const std::string&)>;

// "main_hand" reads lang("UILABEL_SLOT_MAIN_HAND"); "armor" reads "Armor"
std::string tipWords(std::string s) {
    for (char& c : s) if (c == '_') c = ' ';
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

// The language's word for an item's type, material or slot: the key prefix and the id in capitals (armor reads
// UILABEL_TYPE_ARMOR). A word with no key yet shows the fallback.
std::string itemWord(const std::string& prefix, const std::string& id, const std::string& fallback) {
    std::string key = prefix + id;
    for (char& c : key) c = (char)std::toupper((unsigned char)c);
    const std::string text = lang(key);
    return text == key ? fallback : text;
}

std::vector<TipRow> itemRows(const ItemDef& d, int count, const GearSum* base, const TipWrap& wrap) {
    std::vector<TipRow> rows;
    std::string title = itemDisplayName(d);
    if (count > 1) title += " x" + std::to_string(count);
    rows.push_back(TipRow{ title, tierTint(d.tier) });
    const std::string desc = itemDisplayDescription(d);
    if (!desc.empty()) {
        for (const std::string& line : wrap(desc)) rows.push_back(TipRow{ line, TIP_TEXT });
    }
    rows.push_back(TipRow{ langf("UILABEL_TIP_TYPE", { itemWord("UILABEL_TYPE_", d.typeId, tipWords(d.typeId)) }), TIP_TEXT });
    if (!d.material.empty()) rows.push_back(TipRow{ langf("UILABEL_TIP_MATERIAL", { itemWord("MISC_MATERIAL_", d.material, d.material) }), TIP_TEXT });
    if (!d.equipSlot.empty()) rows.push_back(TipRow{ langf("UILABEL_TIP_SLOT", { itemWord("UILABEL_SLOT_", d.equipSlot, tipWords(d.equipSlot)) }), TIP_TEXT });
    rows.push_back(TipRow{ langf("UILABEL_TIP_REQUIRED_LEVEL", { std::to_string(d.requiredLevel) }), TIP_TEXT });

    auto intChange = [&](TipRow& row, int now, int before, bool higherIsBetter) {
        if (!base || now == before) return;
        const int diff = now - before;
        row.change = "(" + std::string(diff > 0 ? "+" : "") + std::to_string(diff) + ")";
        row.changeTint = ((diff > 0) == higherIsBetter) ? GOOD_CHANGE : BAD_CHANGE;
    };

    if (d.attackValue > 0 || (base && base->attack > 0)) {
        TipRow row{ langf("UILABEL_TIP_ATTACK", { std::to_string(d.attackValue) }), TIP_TEXT };
        intChange(row, d.attackValue, base ? base->attack : 0, true);
        rows.push_back(row);
    }
    if (d.defenseValue > 0 || (base && base->defense > 0)) {
        TipRow row{ langf("UILABEL_TIP_DEFENSE", { std::to_string(d.defenseValue) }), TIP_TEXT };
        intChange(row, d.defenseValue, base ? base->defense : 0, true);
        rows.push_back(row);
    }

    char weight[32];
    std::snprintf(weight, sizeof(weight), "%.1f", d.weight);
    TipRow weightRow{ langf("UILABEL_TIP_WEIGHT", { weight }) + (count > 1 ? lang("UILABEL_TIP_EACH") : std::string()), TIP_TEXT };
    if (base && std::fabs(d.weight - base->weight) >= 0.05f) {
        std::snprintf(weight, sizeof(weight), "(%+.1f)", d.weight - base->weight);
        weightRow.change = weight;
        weightRow.changeTint = d.weight < base->weight ? GOOD_CHANGE : BAD_CHANGE;
    }
    rows.push_back(weightRow);

    if (d.maxDurability > 0) {
        TipRow row{ langf("UILABEL_TIP_DURABILITY", { std::to_string(d.currentDurability), std::to_string(d.maxDurability) }),
                    TIP_TEXT };
        intChange(row, d.currentDurability, base ? base->durability : 0, true);
        rows.push_back(row);
    }
    if (d.maxStackSize > 1) rows.push_back(TipRow{ langf("UILABEL_TIP_STACKS", { std::to_string(d.maxStackSize) }), TIP_TEXT });
    if (d.buyValue > 0) rows.push_back(TipRow{ langf("UILABEL_TIP_BUY", { std::to_string(d.buyValue) }), TIP_TEXT });
    if (d.sellValue > 0) rows.push_back(TipRow{ langf("UILABEL_TIP_SELL", { std::to_string(d.sellValue) }), TIP_TEXT });
    if (!d.onUse.empty()) rows.push_back(TipRow{ langf("UILABEL_TIP_USE", { d.onUse }), TIP_TEXT });
    return rows;
}

// A card beside a comparison: the gear slot, the item in it, and its stats. replaced marks the item the
// hovered item would take the place of. held is 0 for an empty slot.
std::vector<TipRow> gearCardRows(int slot, bool replaced, int held, const TipWrap& wrap) {
    std::vector<TipRow> rows;
    rows.push_back(TipRow{ gearSlotName(slot), TIP_HEADING });
    if (replaced) rows.push_back(TipRow{ lang("UILABEL_WILL_BE_REPLACED"), TIP_WARNING });
    const ItemDef* d = findItem(held);
    if (!d) {
        rows.push_back(TipRow{ lang("UILABEL_EMPTY"), TIP_TEXT });
        return rows;
    }
    for (const TipRow& row : itemRows(*d, 1, nullptr, wrap)) rows.push_back(row);
    return rows;
}
} // namespace

float Renderer::tipWidth(const std::vector<TipRow>& rows) {
    const float s = uiH_ / 720.0f, gap = 6.0f * s, pad = 7.0f * s;
    float w = 0.0f;
    for (const TipRow& r : rows) {
        float rowW = listTextWidth(r.text);
        if (!r.change.empty()) rowW += gap + listTextWidth(r.change);
        w = std::max(w, rowW);
    }
    return w + 2.0f * pad;
}

float Renderer::tipHeight(size_t rowCount) {
    if (rowCount == 0) return 0.0f;
    const float s = uiH_ / 720.0f, gap = 2.0f * s, pad = 7.0f * s;
    return rowCount * listTextHeight("Ag") + (rowCount - 1) * gap + 2.0f * pad;
}

void Renderer::drawTip(const std::vector<TipRow>& rows, float x, float y, float width) {
    const float s = uiH_ / 720.0f, gap = 2.0f * s, pad = 7.0f * s;
    const float lineH = listTextHeight("Ag");
    drawRect(Rect{ x, y, width, tipHeight(rows.size()) }, RGBA{ 0.0f, 0.0f, 0.0f, 0.5f });
    float ty = y + pad;
    for (const TipRow& r : rows) {
        drawListText(r.text, x + pad, ty, r.tint);
        if (!r.change.empty()) {
            drawListText(r.change, x + pad + listTextWidth(r.text) + 6.0f * s, ty, r.changeTint);
        }
        ty += lineH + gap;
    }
}

void Renderer::drawItemTooltip(int item, int count, float mx, float my, const Equipment* compareTo) {
    const ItemDef* d = findItem(item);
    if (!d) return;
    const float s = uiH_ / 720.0f;
    const float gap = 8.0f * s;

    // Gear is compared with the gear it would replace, and each gear slot of its kind gets a card
    const bool compare = compareTo && !d->equipSlot.empty();
    std::vector<int> displaced;
    std::vector<int> replacedItems;
    if (compare) {
        displaced = displacedSlots(*compareTo, item);
        for (int slot : displaced) replacedItems.push_back(compareTo->items[slot]);
    }
    const GearSum base = sumGear(replacedItems);
    const float tipTextW = 260.0f * s;
    const TipWrap wrapTip = [this, tipTextW](const std::string& t) { return wrapListText(t, tipTextW); };
    const std::vector<TipRow> main = itemRows(*d, count, compare ? &base : nullptr, wrapTip);

    std::vector<std::vector<TipRow>> cards;
    if (compare) {
        for (int slot : comparisonSlots(item)) {
            const int held = compareTo->items[slot];
            const bool replaced = held != 0 && std::find(displaced.begin(), displaced.end(), slot) != displaced.end();
            cards.push_back(gearCardRows(slot, replaced, held, wrapTip));
        }
    }

    // The tooltip sits beside the mouse, moved to its left near the right edge
    const float mainW = tipWidth(main), mainH = tipHeight(main.size());
    float bx = mx + 14.0f * s;
    if (bx + mainW > uiW_) bx = mx - mainW - 14.0f * s;
    bx = std::max(0.0f, bx);

    std::vector<float> cardW;
    float cardsW = 0.0f, cardsH = 0.0f;
    for (const auto& c : cards) {
        cardW.push_back(tipWidth(c));
        cardsW += cardW.back() + (cardW.size() > 1 ? gap : 0.0f);
        cardsH = std::max(cardsH, tipHeight(c.size()));
    }

    float by = my + 12.0f * s;
    const float tallest = std::max(mainH, cardsH);
    if (by + tallest > uiH_) by = uiH_ - tallest;
    by = std::max(0.0f, by);

    // The cards go on the side of the tooltip that has room: to its right if they fit, else to its left
    const bool right = bx + mainW + gap + cardsW <= uiW_;
    float cx = std::max(0.0f, right ? bx + mainW + gap : bx - gap - cardsW);

    drawTip(main, bx, by, mainW);
    for (size_t i = 0; i < cards.size(); ++i) {
        drawTip(cards[i], cx, by, cardW[i]);
        cx += cardW[i] + gap;
    }
}

// ---- the minimap, the compass and the world map ----------------------------------------------------

void Renderer::pollOverview() {
    OverviewPicture pic;
    if (!overview_.take(pic)) return;
    if (overviewTex_) glDeleteTextures(1, &overviewTex_);
    overviewTex_ = pic.px > 0 ? MakeRgbaTexture(pic.px, pic.px, pic.rgba.data()) : 0;
    overviewSize_ = pic.px > 0 ? static_cast<float>(pic.size) : 0.0f;
    overviewExtent_ = pic.px > 0 ? pic.extent() : 1.0f;
}

void Renderer::drawUiQuad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                          RGBA tint) {
    // Vertices BL, BR, TR, TL: the top edge (y0) takes the top texture coordinate v0
    const float v[4 * 5] = {
        x0, y1, 0.0f, u0, v1,
        x1, y1, 0.0f, u1, v1,
        x1, y0, 0.0f, u1, v0,
        x0, y0, 0.0f, u0, v0
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(tex, v, tint);
}

void Renderer::drawDot(float cx, float cy, float d) {
    if (!dotTex_) dotTex_ = MakeDotTexture();
    const float h = d * 0.5f;
    drawUiQuad(dotTex_, cx - h, cy - h, cx + h, cy + h, 0.0f, 0.0f, 1.0f, 1.0f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

void Renderer::drawMinimap(const Rect& r, const std::vector<MapDot>& dots, float cx, float cz, float span) {
    pollOverview();
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    drawRect(Rect{ r.x - 2.0f * s, r.y - 2.0f * s, r.w + 4.0f * s, r.h + 4.0f * s }, RGBA{ 0.02f, 0.02f, 0.03f, 0.9f });
    drawRect(r, RGBA{ 0.10f, 0.20f, 0.30f, 1.0f });   // sea, where the window is past the edge of the level
    if (span <= 0.0f) return;

    // The window in tiles. Its left edge is the largest x and its top edge the largest z.
    const float xHi = cx + span * 0.5f, xLo = cx - span * 0.5f;
    const float zHi = cz + span * 0.5f, zLo = cz - span * 0.5f;
    auto sx = [&](float x) { return r.x + (xHi - x) / span * r.w; };
    auto sy = [&](float z) { return r.y + (zHi - z) / span * r.h; };

    if (overviewTex_ && overviewSize_ > 0.0f) {
        const float size = overviewSize_, ext = overviewExtent_;
        auto u = [&](float x) { return (size - x) / ext; };
        auto v = [&](float z) { return (size - z) / ext; };
        // The part of the window inside the level
        const float ax = std::min(xHi, size), bx = std::max(xLo, 0.0f);
        const float az = std::min(zHi, size), bz = std::max(zLo, 0.0f);
        if (ax > bx && az > bz) drawUiQuad(overviewTex_, sx(ax), sy(az), sx(bx), sy(bz), u(ax), v(az), u(bx), v(bz), white);
    }

    const float d = std::max(5.0f, r.w * 0.045f);
    for (const MapDot& dot : dots) {
        const Vec2& p = dot.at;
        if (p.x < xLo || p.x > xHi || p.z < zLo || p.z > zHi) continue;
        if (dot.kind == MapDotKind::Player) {
            drawDot(sx(p.x), sy(p.z), d);
            continue;
        }
        // Red and gray dots: the white dot tinted, with its black ring
        if (!dotWhiteTex_) dotWhiteTex_ = MakeDotTexture(true);
        const RGBA tint = dot.kind == MapDotKind::Hostile ? RGBA{ 1.0f, 0.25f, 0.25f, 1.0f } : RGBA{ 0.6f, 0.6f, 0.6f, 1.0f };
        const float cx = sx(p.x), cy = sy(p.z), h = d * 0.5f;
        drawUiQuad(dotWhiteTex_, cx - h, cy - h, cx + h, cy + h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
    }
}

// The world map: the part of the level `span` tiles wide centred on cx, cz, in the square under the title
void Renderer::drawWorldMap(const Rect& area, const std::vector<MapDot>& dots, float cx, float cz, float span) {
    const float s = uiH_ / 720.0f;
    drawRect(area, RGBA{ 0.0f, 0.0f, 0.02f, 0.72f });
    drawListText(lang("UILABEL_WORLD_MAP_HINT"), area.x + 28.0f * s, area.y + 8.0f * s,
                 RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    drawMinimap(WorldMapBox(area, s), dots, cx, cz, span);
}

void Renderer::drawMinimapToggle(const Rect& r, bool open) {
    drawRect(r, RGBA{ 0.05f, 0.06f, 0.09f, 0.85f });
    const char* sym = open ? "-" : "+";
    const float tw = listTextWidth(sym), th = listTextHeight(sym);
    drawListText(sym, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

std::vector<std::string> Renderer::wrapListText(const std::string& text, float maxW) {
    std::vector<std::string> out;
    std::string cur;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (!word.empty()) {
            const std::string trial = cur.empty() ? word : cur + " " + word;
            // A word wider than the line keeps a line of its own; the caller shortens what does not fit
            if (cur.empty() || listTextWidth(trial) <= maxW) cur = trial;
            else {
                out.push_back(cur);
                cur = word;
            }
        }
        start = end + 1;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void Renderer::drawDialogue(const DialogueLayout& L, const std::vector<SpokenLine>& convo,
                            const std::vector<DialogueOption>& options, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA playerTint{ 0.72f, 0.88f, 1.0f, 1.0f };
    const RGBA rowBg{ 0.10f, 0.11f, 0.15f, 0.92f };
    const RGBA plate{ 0.16f, 0.18f, 0.24f, 1.0f };
    const Rect& b = L.box;
    drawRect(Rect{ b.x - 1.5f * s, b.y - 1.5f * s, b.w + 3.0f * s, b.h + 3.0f * s }, RGBA{ 0.50f, 0.55f, 0.66f, 1.0f });
    drawRect(b, RGBA{ 0.04f, 0.05f, 0.08f, 0.96f });

    // The lines: the last ones said, the NPC's portrait on the left and the player's on the right
    const float lineH = listTextHeight("Ag");
    const size_t shownFrom = convo.size() > L.lines.size() ? convo.size() - L.lines.size() : 0;
    for (size_t i = 0; i < L.lines.size() && shownFrom + i < convo.size(); ++i) {
        const SpokenLine& ln = convo[shownFrom + i];
        const Rect& r = L.lines[i];
        drawRect(r, rowBg);
        const float pic = r.h - 10.0f * s;
        const Rect picRect = ln.player ? Rect{ r.x + r.w - pic - 5.0f * s, r.y + 5.0f * s, pic, pic }
                                       : Rect{ r.x + 5.0f * s, r.y + 5.0f * s, pic, pic };
        drawRect(Rect{ picRect.x - 1.0f * s, picRect.y - 1.0f * s, picRect.w + 2.0f * s, picRect.h + 2.0f * s }, plate);
        drawUnitPicture(ln.sprite, picRect);
        const float textX = ln.player ? r.x + 10.0f * s : picRect.x + picRect.w + 10.0f * s;
        const float textW = r.w - pic - 24.0f * s;
        const std::vector<std::string> wrapped = wrapListText(ln.text, textW);
        for (size_t j = 0; j < wrapped.size() && (float)j * lineH < r.h - 8.0f * s; ++j) {
            drawListText(fitText(wrapped[j], textW, false), textX, r.y + 6.0f * s + (float)j * lineH,
                         ln.player ? playerTint : white);
        }
    }

    // The options the player can say, then End Dialogue, which is always last
    for (size_t i = 0; i < L.options.size() && i < options.size(); ++i) {
        const Rect& r = L.options[i];
        drawButton(r, fitText(options[i].prompt, r.w - 16.0f * s, false), true, r.contains(mx, my));
    }
    drawButton(L.end, lang("UILABEL_END_DIALOGUE"), true, L.end.contains(mx, my));
}

void Renderer::drawItemCell(const Rect& r, int item, int count) {
    if (item == 0) return;
    const float s = uiH_ / 720.0f;
    const Rect inner{ r.x + 3.0f * s, r.y + 3.0f * s, r.w - 6.0f * s, r.h - 6.0f * s };
    if (const TextTex* pic = itemTexture(item)) {
        drawFitted(*pic, inner, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    } else {
        RGBA c = categoryColor(item);
        c.a = 0.9f;
        drawRect(inner, c);
    }
    if (count > 1) {
        const std::string n = std::to_string(count);
        drawListText(n, r.x + r.w - listTextWidth(n) - 3.0f * s, r.y + r.h - listTextHeight("Ag") - 1.0f * s,
                     RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

void Renderer::drawUiTextLarge(const std::string& s, float cx, float cy, float scale, RGBA tint) {
    HudLine& hl = uiLine(s);
    if (!hl.tex.tex) return;
    const float w = hl.w * scale, h = hl.h * scale;
    const float x = cx - w * 0.5f, y = cy - h * 0.5f;
    float v[4 * 5] = {
        x,     y + h, 0.0f, 0.0f, 1.0f,
        x + w, y + h, 0.0f, 1.0f, 1.0f,
        x + w, y,     0.0f, 1.0f, 0.0f,
        x,     y,     0.0f, 0.0f, 0.0f
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(hl.tex.tex, v, tint);
}

// A black bar across the top at 30% opacity, with PAUSED in large white letters, centred. With a hint, a line under
// it says how to resume.
void Renderer::drawPauseBar(bool hint) {
    const float s = uiH_ / 720.0f;
    const float pad = 10.0f * s;
    const float bigH = uiTextHeight(lang("UILABEL_PAUSED")) * 2.6f;
    const std::string tip = lang("UILABEL_PAUSE_HINT");
    const float tipH = hint ? uiTextHeight(tip) : 0.0f;
    const float h = pad + bigH + (hint ? pad + tipH : 0.0f) + pad;
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, h }, RGBA{ 0.0f, 0.0f, 0.0f, 0.3f });
    drawUiTextLarge(lang("UILABEL_PAUSED"), uiW_ * 0.5f, pad + bigH * 0.5f, 2.6f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    if (hint) drawUiText(tip, uiW_ * 0.5f - uiTextWidth(tip) * 0.5f, pad + bigH + pad, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

// The value bar's colour at t from 0 to 1: red at the start, yellow halfway, green at the end
static RGBA valueBarColor(float t) {
    const RGBA red{ 0.85f, 0.20f, 0.20f, 1.0f }, yellow{ 0.95f, 0.85f, 0.20f, 1.0f }, green{ 0.25f, 0.85f, 0.30f, 1.0f };
    const RGBA a = t < 0.5f ? red : yellow;
    const RGBA b = t < 0.5f ? yellow : green;
    const float u = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    return RGBA{ a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, 1.0f };
}

void Renderer::drawTrade(const TradeLayout& L, const std::string& npcName, const Inventory* npcInv,
                         const std::vector<TradeGhost>& theirs, const std::vector<TradeGhost>& yours, int dragFrom,
                         const TradeStatus& status, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.80f, 0.86f, 0.96f, 1.0f };
    const RGBA panel{ 0.05f, 0.06f, 0.09f, 0.95f };
    const RGBA bar{ 0.16f, 0.18f, 0.24f, 1.0f };
    const RGBA well{ 0.03f, 0.03f, 0.05f, 1.0f };
    const RGBA plate{ 0.12f, 0.14f, 0.19f, 1.0f };
    const RGBA plateHot{ 0.22f, 0.26f, 0.36f, 1.0f };
    const float th = listTextHeight("Ag");
    const float pad = 10.0f * s, line = 20.0f * s;

    // The Interaction Panel: the NPC's name, and its inventory
    drawRect(L.interaction, panel);
    drawRect(L.interBar, bar);
    drawListText(lang("UILABEL_INTERACTION_PANEL"), L.interBar.x + 6.0f * s, L.interBar.y + (L.interBar.h - th) * 0.5f, white);
    const float nameY = L.interBar.y + L.interBar.h + pad;
    drawListText(fitText(npcName, L.interaction.w - 2.0f * pad, false), L.interaction.x + pad, nameY, white);
    drawListText(lang("UILABEL_INVENTORY"), L.interaction.x + pad, nameY + line, heading);
    drawRect(L.npcGrid, well);
    int hoverNpc = -1;
    for (size_t i = 0; i < L.npcSlots.size(); ++i) {
        const Rect& r = L.npcSlots[i];
        const bool hot = r.contains(mx, my);
        drawRect(r, hot ? plateHot : plate);
        if (!npcInv || i >= (size_t)INVENTORY_SLOTS) continue;
        const ItemStack& st = npcInv->slots[i];
        if (st.item == 0 || (int)i == dragFrom) continue;
        drawItemCell(r, st.item, st.count);
        if (hot) hoverNpc = (int)i;
    }

    // The Trade Panel: a box for each side, its items in cells, the total under it
    drawRect(L.tradePanel, panel);
    drawRect(L.tradeBar, bar);
    drawListText(lang("UILABEL_TRADE_PANEL"), L.tradeBar.x + 6.0f * s, L.tradeBar.y + (L.tradeBar.h - th) * 0.5f, white);
    auto side = [&](const Rect& box, const std::vector<Rect>& cells, const std::vector<TradeGhost>& ghosts,
                    const std::string& hint, const Rect& total) {
        drawRect(box, well);
        for (size_t i = 0; i < cells.size(); ++i) {
            drawRect(cells[i], plate);
            if (i < ghosts.size()) drawItemCell(cells[i], ghosts[i].stack.item, ghosts[i].stack.count);
        }
        // The hint shows while the side is empty, centred in its box
        if (ghosts.empty()) {
            const std::vector<std::string> lines = wrapListText(hint, box.w - 2.0f * pad);
            float ty = box.y + (box.h - (float)lines.size() * th) * 0.5f;
            for (const std::string& ln : lines) {
                const std::string shown = fitText(ln, box.w - 2.0f * pad, false);
                drawListText(shown, box.x + (box.w - listTextWidth(shown)) * 0.5f, ty, gray);
                ty += th;
            }
        }
        drawListText(fitText(langf("UILABEL_TOTAL_VALUE", { std::to_string(tradeValueOf(ghosts)) }), total.w, false), total.x, total.y, white);
    };
    side(L.theirBox, L.theirCells, theirs, lang("UILABEL_TRADE_DROP_THEIRS"), L.theirTotal);
    side(L.yourBox, L.yourCells, yours, lang("UILABEL_TRADE_DROP_YOURS"), L.yourTotal);

    // The value bar: red while the offer is far from the price, yellow, then green once it is met
    drawRect(L.valueBar, well);
    const float fillW = L.valueBar.w * std::clamp(status.progress, 0.0f, 1.0f);
    const int slices = 24;
    for (int i = 0; i < slices; ++i) {
        const float x0 = L.valueBar.x + L.valueBar.w * (float)i / slices;
        const float x1 = L.valueBar.x + L.valueBar.w * (float)(i + 1) / slices;
        if (x0 >= L.valueBar.x + fillW) break;
        drawRect(Rect{ x0, L.valueBar.y, std::min(x1, L.valueBar.x + fillW) - x0, L.valueBar.h },
                 valueBarColor((i + 0.5f) / slices));
    }
    drawListText(fitText(status.text, L.statusLine.w, false), L.statusLine.x, L.statusLine.y,
                 status.ok ? RGBA{ 0.6f, 1.0f, 0.6f, 1.0f } : RGBA{ 1.0f, 0.72f, 0.55f, 1.0f });

    drawButton(L.cancelBtn, lang("UILABEL_CANCEL"), true, L.cancelBtn.contains(mx, my));
    drawButton(L.tradeBtn, lang("UILABEL_TRADE"), status.ok, status.ok && L.tradeBtn.contains(mx, my));

    if (hoverNpc >= 0 && npcInv) {
        const ItemStack& st = npcInv->slots[hoverNpc];
        drawItemTooltip(st.item, st.count, mx, my);
    }
    // Item ghosts show their item's tooltip on hover, as items do elsewhere
    for (size_t i = 0; i < L.theirCells.size() && i < theirs.size(); ++i) {
        if (theirs[i].stack.item != 0 && L.theirCells[i].contains(mx, my)) drawItemTooltip(theirs[i].stack.item, theirs[i].stack.count, mx, my);
    }
    for (size_t i = 0; i < L.yourCells.size() && i < yours.size(); ++i) {
        if (yours[i].stack.item != 0 && L.yourCells[i].contains(mx, my)) drawItemTooltip(yours[i].stack.item, yours[i].stack.count, mx, my);
    }
}

void Renderer::drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                                  float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA panel{ 0.05f, 0.06f, 0.09f, 0.98f };
    const RGBA well{ 0.03f, 0.03f, 0.05f, 1.0f };
    const float th = listTextHeight("Ag");
    drawRect(L.box, panel);
    const std::string title = langf("UILABEL_CHOOSE_HOW_MANY", { itemName, std::to_string(available) });
    drawListText(fitText(title, L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, white);
    drawRect(L.countBox, well);
    const std::string num = std::to_string(chosen);
    drawListText(num, L.countBox.x + (L.countBox.w - listTextWidth(num)) * 0.5f,
                 L.countBox.y + (L.countBox.h - th) * 0.5f, white);
    auto btn = [&](const Rect& r, const std::string& label, bool enabled) {
        drawButton(r, label, enabled, enabled && r.contains(mx, my));
    };
    btn(L.minus100, "-100", chosen > 1);
    btn(L.minus10, "-10", chosen > 1);
    btn(L.minus1, "-", chosen > 1);
    btn(L.plus1, "+", chosen < available);
    btn(L.plus10, "+10", chosen < available);
    btn(L.plus100, "+100", chosen < available);
    btn(L.single, lang("UILABEL_SINGLE"), chosen != 1);
    btn(L.max, lang("UILABEL_MAX"), chosen != available);
    btn(L.confirm, lang("UILABEL_CONFIRM"), true);
    btn(L.cancel, lang("UILABEL_CANCEL"), true);
    (void)s;
}

void Renderer::drawCompass(const Rect& r, float bearingDeg) {
    const float s = uiH_ / 720.0f;
    drawRect(Rect{ r.x - 2.0f * s, r.y - 2.0f * s, r.w + 4.0f * s, r.h + 4.0f * s }, RGBA{ 0.02f, 0.02f, 0.03f, 0.9f });
    drawRect(r, RGBA{ 0.05f, 0.05f, 0.08f, 0.8f });

    // The strip shows 60 degrees either side of the bearing, a tick every 15 degrees and a letter every 90
    const float half = 60.0f;
    const float perDeg = r.w / (2.0f * half);
    const float cx = r.x + r.w * 0.5f;
    const std::string names[4] = { lang("UILABEL_COMPASS_N"), lang("UILABEL_COMPASS_E"), lang("UILABEL_COMPASS_S"), lang("UILABEL_COMPASS_W") };
    for (int a = 0; a < 360; a += 15) {
        const float d = std::fmod(static_cast<float>(a) - bearingDeg + 540.0f, 360.0f) - 180.0f;
        if (std::fabs(d) > half) continue;
        const float x = cx + d * perDeg;
        const bool major = a % 90 == 0;
        const float tick = (major ? 8.0f : 5.0f) * s;
        drawRect(Rect{ x - 0.5f, r.y + r.h - tick, 1.0f, tick }, RGBA{ 0.9f, 0.9f, 0.95f, 0.9f });
        if (major) {
            const std::string name = names[a / 90];
            drawListText(name, x - listTextWidth(name) * 0.5f, r.y + 2.0f * s, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }
    // The bearing the camera looks along: a small mark at the centre
    drawRect(Rect{ cx - 1.5f * s, r.y, 3.0f * s, 4.0f * s }, RGBA{ 1.0f, 0.85f, 0.3f, 1.0f });
}

void Renderer::drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units, bool running) {
    const float s = uiH_ / 720.0f;
    const float bgAlpha = 0.5f;
    drawRect(L.panel, RGBA{ 0.05f, 0.05f, 0.08f, bgAlpha });
    drawTitleBar(L.title, ICON_SQUAD_PANEL, lang("UILABEL_SQUAD_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    // Walk and run: the lit one is the pace the squad is moving at
    auto mode = [&](const Rect& r, const std::string& text, bool on) {
        drawRect(r, on ? RGBA{ 0.66f, 0.38f, 1.0f, 1.0f } : RGBA{ 0.14f, 0.14f, 0.18f, bgAlpha });
        const float tw = listTextWidth(text), th = listTextHeight(text);
        drawListText(text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    };
    // One toggle for the pace, lit while the squad runs, and the View Map button beside it
    mode(L.paceButton, running ? lang("UILABEL_PACE_RUN") : lang("UILABEL_PACE_WALK"), running);

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

        const Needs* nd = w.get<Needs>(e);
        // Low health: a red gradient rises from the bottom of the portrait by the share of health lost, as in the sidebar
        if (nd && nd->maxHp > 0 && nd->hp < nd->maxHp) {
            const float lost = std::clamp(1.0f - (float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f);
            const float gh = c.rect.w * lost;
            if (gh > 0.0f) {
                drawRectGradient(Rect{ c.rect.x, c.rect.y + c.rect.w - gh, c.rect.w, gh },
                                 RGBA{ 0.85f, 0.10f, 0.10f, 0.0f }, RGBA{ 0.85f, 0.10f, 0.10f, 0.65f });
            }
        }

        // Health bar under the portrait
        const float frac = (nd && nd->maxHp > 0) ? std::clamp((float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f) : 0.0f;
        const float barY = c.rect.y + c.rect.w + 3.0f * s, barH = 4.0f * s;
        drawRect(Rect{ c.rect.x, barY, c.rect.w, barH }, RGBA{ 0.16f, 0.16f, 0.19f, bgAlpha });
        drawRect(Rect{ c.rect.x, barY, c.rect.w * frac, barH }, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });

        // Name under the bar, cut at a word with "..." to fit the cell
        const Name* nm = w.get<Name>(e);
        const std::string full = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
        const std::string label = fitText(full, c.rect.w, false);
        if (label != full) noteTip(full, c.rect);
        drawListText(label, c.rect.x, c.rect.y + c.rect.w + 8.0f * s, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    if (L.scrollable()) {
        drawRect(L.track, RGBA{ 0.18f, 0.18f, 0.22f, bgAlpha });
        drawRect(L.thumb, RGBA{ 0.7f, 0.7f, 0.8f, 1.0f });
    }
}

// The Stats tab: every stat's level under the heading of its group, with its progress bar. Returns the stat under
// the mouse, or -1. The hover tooltip is drawn later, outside the panel's clip (drawStatTip).
int Renderer::drawStatsTab(const SidebarLayout& L, const Stats& stats, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.80f, 0.86f, 0.96f, 1.0f };
    const Rect& area = L.statsArea;
    const float rowH = L.statRowH;
    const float th = listTextHeight("Ag");
    // The level sits on the left, right aligned in its column; then the icon and the name. The name and its
    // progress bar run to the right edge.
    const float levelR = area.x + 26.0f * s;
    const float iconX = area.x + 32.0f * s;
    const float nameX = iconX + rowH;
    const float barEnd = area.x + area.w;
    int hovered = -1;

    auto row = [&](float y, int icon, const std::string& name, const std::string& lv, RGBA tint) {
        // The text sits above a 3px progress bar strip along the bottom edge of the row
        const float ty = y + (rowH - 4.0f * s - th) * 0.5f;
        drawListText(lv, levelR - listTextWidth(lv), ty, tint);
        if (icon >= 0) drawUiIcon(icon, Rect{ iconX, y, rowH - 2.0f * s, rowH - 2.0f * s });
        drawListText(fitText(name, barEnd - 8.0f * s - nameX, false), nameX, ty, tint);
    };
    float y = area.y;
    row(y, -1, lang("UILABEL_STAT"), lang("UILABEL_LEVEL"), gray);
    y += rowH;
    const StatGroup groups[] = { StatGroup::Attributes, StatGroup::Combat, StatGroup::Gathering,
                                 StatGroup::Crafting, StatGroup::Utility };
    for (StatGroup group : groups) {
        const std::string title = group == StatGroup::Attributes ? lang("UILABEL_ATTRIBUTES")
                          : group == StatGroup::Combat ? lang("UILABEL_COMBAT")
                          : group == StatGroup::Gathering ? lang("UILABEL_RESOURCE_GATHERING")
                          : group == StatGroup::Crafting ? lang("UILABEL_CRAFTING") : lang("UILABEL_UTILITY");
        drawListText(title, area.x, y + (rowH - th) * 0.5f, heading);
        y += rowH;
        for (int i = 0; i < STAT_COUNT; ++i) {
            if (statInfo(i).group != group) continue;
            const int lv = levelOf(stats, i);
            row(y, statIcon(i), statName(i), std::to_string(lv), white);
            // Progress from this level toward the next one
            const float barY = y + rowH - 3.0f * s, barH = 3.0f * s;
            const float barW = barEnd - nameX;
            drawRect(Rect{ nameX, barY, barW, barH }, RGBA{ 0.16f, 0.16f, 0.19f, 1.0f });
            drawRect(Rect{ nameX, barY, barW * levelProgress(stats, i), barH }, RGBA{ 0.45f, 0.65f, 0.88f, 1.0f });
            if (mx >= area.x && mx < area.x + area.w && my >= y && my < y + rowH) hovered = i;
            y += rowH;
        }
    }
    return hovered;
}

// The tooltip for a stat: its name, level, progress to the next level, its current XP and the XP the next level needs
void Renderer::drawStatTip(const Stats& stats, int stat, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const int lv = levelOf(stats, stat);
    const int pct = (int)std::lround(levelProgress(stats, stat) * 100.0f);
    std::vector<TipRow> rows(5);
    rows[0].text = statName(stat);
    rows[0].tint = RGBA{ 0.80f, 0.86f, 0.96f, 1.0f };
    rows[1].text = langf("UILABEL_TIP_LEVEL", { std::to_string(lv) });
    rows[2].text = langf("UILABEL_TIP_PROGRESS", { std::to_string(pct) });
    rows[3].text = langf("UILABEL_TIP_CURRENT_XP", { withCommas(stats.xp[stat]) });
    rows[4].text = langf("UILABEL_NEXT_LEVEL_XP_LINE", { lv >= MAX_LEVEL ? std::string(lang("UILABEL_MAX")) : withCommas(xpForLevel(lv + 1)) });
    const float width = tipWidth(rows), height = tipHeight(rows.size());
    // Beside the mouse, moved to the other side or up when the window has no room for it
    float x = mx + 14.0f * s, y = my + 14.0f * s;
    if (x + width > uiW_) x = mx - 14.0f * s - width;
    if (y + height > uiH_) y = uiH_ - height;
    drawTip(rows, std::max(0.0f, x), std::max(0.0f, y), width);
}

void Renderer::drawInventorySidebar(const SidebarLayout& L, World& w, Entity unit, float mx, float my,
                                    int dimSlot, int dimGear, int dragItem, bool editable) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    const float bgAlpha = 0.5f;   // every background in the sidebar; text, item colors and bars stay solid
    // The bar with its [-] or [+] button, and the tabs under it. They stay where they are when the content scrolls.
    auto frame = [&]() {
        drawTitleBar(L.title, ICON_UNIT_PANEL, lang("UILABEL_UNIT_PANEL"));
        if (L.minus.w > 0.0f) drawCollapseButton(L.minus, false);
        if (L.plus.w > 0.0f) drawCollapseButton(L.plus, true);
        const std::string names[UNIT_TAB_COUNT] = { lang("UILABEL_INVENTORY"), lang("UILABEL_EQUIPMENT"), lang("UILABEL_PROFILE"), lang("UILABEL_STATS"), lang("UILABEL_BEHAVIOR") };
        for (int i = 0; i < UNIT_TAB_COUNT; ++i) {
            const Rect& t = L.tabs[i];
            if (t.w <= 0.0f) continue;
            drawRect(t, i == L.tab ? RGBA{ 0.30f, 0.36f, 0.56f, 0.95f } : RGBA{ 0.12f, 0.14f, 0.20f, 0.9f });
            const float tw = listTextWidth(names[i]), th = listTextHeight("Ag");
            drawListText(names[i], t.x + (t.w - tw) * 0.5f, t.y + (t.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    };
    drawRect(L.panel, RGBA{ 0.04f, 0.04f, 0.06f, bgAlpha });
    if (!L.open) {
        frame();
        return;
    }
    if (unit == NULL_ENTITY) {
        // Nothing is selected, and the body says so
        frame();
        drawUiTextCentered(lang("UILABEL_NO_UNIT_SELECTED"), L.panel.x + L.panel.w * 0.5f,
                           L.body.y + (L.body.h - uiTextHeight("Ag")) * 0.5f, gray);
        return;
    }
    const Inventory* inv = w.get<Inventory>(unit);
    const Needs needs = w.get<Needs>(unit) ? *w.get<Needs>(unit) : Needs{};
    const Stats stats = w.get<Stats>(unit) ? *w.get<Stats>(unit) : defaultStats();
    const Name* nm = w.get<Name>(unit);
    const Equipment* worn = w.get<Equipment>(unit);
    // The picked tab's contents scroll, so they are clipped to the panel
    beginClip(L.panel);

    const float listH = listTextHeight("Ag");
    int hoverGear = -1;
    int hoverSlot = -1;
    int hoverStat = -1;   // the stat under the mouse in the Stats tab
    if (L.tab == UNIT_TAB_PROFILE) {
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

    const std::string name = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
    float y = L.profile.y + 10.0f * s;
    drawUiText(fitText(name, L.profile.x + L.profile.w - 10.0f * s - tx, true), tx, y, white);

    y += uiTextHeight("Ag") + 6.0f * s;
    // The body type the name was drawn for
    if (const Body* bd = w.get<Body>(unit)) {
        drawListText(lang(bodyTypeKey(bd->type)), tx, y, gray);
        y += listTextHeight("Ag") + 6.0f * s;
    }

    // A stat: its label and value on one line, and a bar under them. Returns the y for the next stat.
    auto statRow = [&](float rowY, const std::string& label, const std::string& value, float frac, RGBA fill) {
        drawListText(label, tx, rowY, gray);
        drawListText(value, rightEdge - listTextWidth(value), rowY, white);
        const float barY = rowY + listTextHeight("Ag") + 3.0f * s;
        const float barH = 8.0f * s;
        drawRect(Rect{ tx, barY, colW, barH }, RGBA{ 0.16f, 0.16f, 0.19f, bgAlpha });
        drawRect(Rect{ tx, barY, colW * std::clamp(frac, 0.0f, 1.0f), barH }, fill);
        return barY + barH + 9.0f * s;
    };
    y = statRow(y, lang("UILABEL_HEALTH"), std::to_string(needs.hp) + " / " + std::to_string(needs.maxHp),
                needs.maxHp > 0 ? (float)needs.hp / (float)needs.maxHp : 0.0f, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });
    y = statRow(y, lang("UILABEL_HUNGER"), std::to_string(needs.hunger) + " / " + std::to_string(needs.maxHunger),
                needs.maxHunger > 0 ? (float)needs.hunger / (float)needs.maxHunger : 0.0f, RGBA{ 0.86f, 0.62f, 0.22f, 1.0f });

    // Carried weight against what Strength lets the unit carry
    const float carried = carriedWeightOf(w, unit);
    const float limit = carryLimit(stats);
    char carry[48];
    std::snprintf(carry, sizeof(carry), "%.1f / %.0f", carried, limit);
    statRow(y, lang("UILABEL_CARRY"), carry, limit > 0.0f ? carried / limit : 0.0f, RGBA{ 0.45f, 0.65f, 0.88f, 1.0f });
    }

    if (L.tab == UNIT_TAB_EQUIPMENT) {
    // ---- Gear: what the unit wears, four slots to a row, then the totals ----
    const bool offLocked = worn && offHandBlocked(*worn);
    const ItemDef* dragDef = dragItem != 0 ? findItem(dragItem) : nullptr;
    // A two-handed weapon, held or dragged, dims the off hand
    const bool dimOff = offLocked || (dragDef && dragDef->equipSlot == "two_hand");
    drawRect(L.gearPanel, RGBA{ 0.06f, 0.06f, 0.09f, bgAlpha });
    drawIconText(L.gearPanel.x + 10.0f * s, L.gearPanel.y + 6.0f * s, listTextHeight("Ag"), ICON_EQUIPMENT, lang("UILABEL_EQUIPMENT"), white);

    // While an item is dragged, each gear slot shows whether it may go there: green if so, red if not.
    // A gear item leaving its own slot is not in the way of itself.
    Equipment eval = worn ? *worn : Equipment{};
    if (dimGear >= 0) eval.items[dimGear] = 0;

    const float inset = 5.0f * s;
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
            // An empty slot shows its name, or lang("UILABEL_TWO_HANDED") for a locked off hand, centerd and dimmed
            const std::string text = locked ? lang("UILABEL_TWO_HANDED") : gearSlotName((int)gi);
            const float tw = listTextWidth(text);
            drawListText(text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - listH) * 0.5f,
                         locked ? gray : RGBA{ 0.72f, 0.72f, 0.76f, 0.45f });
            continue;
        }

        // The slot's name stays, and the item is shown by its picture below it. Its name is in the hover tooltip.
        drawListText(gearSlotName((int)gi), r.x + inset, r.y + inset, gray);
        float alpha = 1.0f;
        if ((int)gi == dimGear) alpha = 0.30f;
        else if ((int)gi == GEAR_OFF_HAND && dimOff) alpha = 0.35f;
        const float top = r.y + inset + listH + 2.0f * s;
        const Rect area{ r.x + inset, top, r.w - 2.0f * inset, r.y + r.h - inset - top };
        if (const TextTex* pic = itemTexture(item)) {
            drawFitted(*pic, area, RGBA{ 1.0f, 1.0f, 1.0f, alpha });
        } else {
            // Without a picture, the item's kind shows as a color block with its initials
            const float side = std::min(area.w, area.h) * 0.8f;
            const Rect block{ area.x + (area.w - side) * 0.5f, area.y + (area.h - side) * 0.5f, side, side };
            RGBA c = categoryColor(item);
            c.a = alpha;
            drawRect(block, c);
            const std::string ini = itemInitials(itemName(item));
            drawListText(ini, block.x + (block.w - listTextWidth(ini)) * 0.5f, block.y + (block.h - listH) * 0.5f,
                         RGBA{ 1.0f, 1.0f, 1.0f, alpha });
        }
    }

    int attack = 0, defense = 0;
    if (worn) gearTotals(*worn, attack, defense);
    const std::string totals = langf("UILABEL_ATTACK_DEFENSE", { std::to_string(attack), std::to_string(defense) });
    drawListText(totals, L.gearPanel.x + 10.0f * s, L.gearPanel.y + L.gearPanel.h - listH - 6.0f * s, white);

    }
    // ---- Inventory grid on its own background ----
    if (L.tab == UNIT_TAB_INVENTORY) {
    drawRect(L.gridPanel, RGBA{ 0.06f, 0.06f, 0.09f, bgAlpha });
    drawIconText(L.gridPanel.x + 10.0f * s, L.gridPanel.y + 6.0f * s, listTextHeight("Ag"), ICON_INVENTORY, lang("UILABEL_INVENTORY"), white);
    // The slot count sits left of the sort button, which is at the right of the heading
    const std::string used = std::to_string(inv ? occupiedSlots(*inv) : 0) + " / " +
                             std::to_string(INVENTORY_SLOTS) + " slots";
    drawListText(used, L.sortButton.x - 8.0f * s - listTextWidth(used), L.gridPanel.y + 6.0f * s, gray);
    drawSmallButton(L.sortButton, lang("UILABEL_SORT"), editable, editable && L.sortButton.contains(mx, my));

    for (size_t i = 0; inv && i < L.slots.size() && i < INVENTORY_SLOTS; ++i) {
        const Rect& r = L.slots[i];
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, RGBA{ 0.30f, 0.30f, 0.36f, bgAlpha });
        drawRect(r, RGBA{ 0.10f, 0.10f, 0.13f, bgAlpha });

        const ItemStack& st = inv->slots[i];
        if (st.item == 0 || st.count <= 0) continue;

        // An item shows its picture, or else a block in its kind's color with its initials. The count sits in the corner.
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

    }
    // The behavior list
    if (L.tab == UNIT_TAB_BEHAVIOR) {
        const Behaviors* beh = w.get<Behaviors>(unit);
        drawBehaviorSection(L, beh ? beh->rules : std::vector<BehaviorRule>(), mx, my, editable);
    }
    if (L.tab == UNIT_TAB_STATS) hoverStat = drawStatsTab(L, stats, mx, my);
    if (L.maxScroll > 0.0f) {
        // The scroll bar down the right side: its thumb is the part of the tab in view
        const Rect track{ L.panel.x + L.panel.w - 9.0f * s, L.body.y + 4.0f * s, 5.0f * s, L.body.h - 8.0f * s };
        const float totalH = L.maxScroll + L.body.h;
        const float thumbH = std::max(16.0f * s, track.h * L.body.h / totalH);
        const float thumbY = track.y + (track.h - thumbH) * (L.scroll / L.maxScroll);
        drawRect(track, RGBA{ 0.14f, 0.16f, 0.21f, 0.9f });
        drawRect(Rect{ track.x, thumbY, track.w, thumbH }, RGBA{ 0.50f, 0.55f, 0.66f, 0.9f });
    }
    endClip();
    frame();

    // Tooltips and highlights are for hovering, not while something is being dragged
    if (dragItem == 0 && hoverStat >= 0) drawStatTip(stats, hoverStat, mx, my);
    if (dragItem == 0 && hoverSlot >= 0 && (int)hoverSlot != dimSlot) {
        const Rect& r = L.slots[hoverSlot];
        drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.18f });
        drawItemTooltip(inv->slots[hoverSlot].item, inv->slots[hoverSlot].count, mx, my, worn);
    }
    if (dragItem == 0 && hoverGear >= 0 && hoverGear != dimGear && worn && worn->items[hoverGear] != 0) {
        const Rect& r = L.gear[hoverGear];
        drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.18f });
        drawItemTooltip(worn->items[hoverGear], 1, mx, my);
    }
}

void Renderer::beginClip(const Rect& r) {
    // OpenGL counts rows from the bottom of the window, the layout from the top
    const int x0 = std::max(0, (int)std::floor(r.x));
    const int x1 = std::min(uiW_, (int)std::ceil(r.x + r.w));
    const int top = std::max(0, (int)std::floor(r.y));
    const int bottom = std::min(uiH_, (int)std::ceil(r.y + r.h));
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, uiH_ - bottom, std::max(0, x1 - x0), std::max(0, bottom - top));
}

void Renderer::endClip() {
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::drawSmallButton(const Rect& r, const std::string& label, bool enabled, bool hovered) {
    const RGBA bg = !enabled ? RGBA{ 0.10f, 0.10f, 0.12f, 0.70f }
                  : hovered  ? RGBA{ 0.25f, 0.34f, 0.50f, 0.95f }
                             : RGBA{ 0.14f, 0.17f, 0.22f, 0.90f };
    drawRect(r, bg);
    const RGBA fg = enabled ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.5f, 0.5f, 0.5f, 1.0f };
    const float tw = listTextWidth(label), th = listTextHeight(label);
    drawListText(label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, fg);
}

void Renderer::drawBehaviorSection(const SidebarLayout& L, const std::vector<BehaviorRule>& rules,
                                   float mx, float my, bool editable) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    const float bgAlpha = 0.5f;
    const float listH = listTextHeight("Ag");
    const Rect& P = L.behaviorPanel;

    drawRect(P, RGBA{ 0.06f, 0.06f, 0.09f, bgAlpha });
    drawIconText(P.x + 10.0f * s, P.y + 6.0f * s, listTextHeight("Ag"), ICON_BEHAVIOR, lang("UILABEL_BEHAVIOR"), white);
    const std::string count = std::to_string(rules.size()) + " / " + std::to_string(BEHAVIOR_LIMIT);
    drawListText(count, P.x + P.w - 10.0f * s - listTextWidth(count), P.y + 6.0f * s, gray);

    const std::string heads[5] = { lang("UILABEL_BEHAVIOR_ON"), lang("UILABEL_TARGET"), lang("UILABEL_CONDITION"), lang("UILABEL_ACTION"), lang("UILABEL_TARGET") };
    for (size_t i = 0; i < 5; ++i) {
        const Rect& h = L.behaviorHeads[i];
        drawListText(fitText(heads[i], h.w, false), h.x, h.y + (h.h - listH) * 0.5f, gray);
    }

    for (size_t i = 0; i < L.behaviorRows.size() && i < rules.size(); ++i) {
        const BehaviorRowLayout& R = L.behaviorRows[i];
        const BehaviorRule& r = rules[i];
        if (editable && R.row.contains(mx, my)) drawRect(R.row, RGBA{ 1.0f, 1.0f, 1.0f, 0.06f });
        // A rule that is off reads gray, so the list shows at a glance which ones work
        const RGBA text = r.on ? white : gray;

        // The on box: a dark square, with a blue fill while the rule is on
        drawRect(Rect{ R.toggle.x - 1.0f, R.toggle.y - 1.0f, R.toggle.w + 2.0f, R.toggle.h + 2.0f },
                 RGBA{ 0.30f, 0.30f, 0.36f, bgAlpha });
        drawRect(R.toggle, RGBA{ 0.10f, 0.10f, 0.13f, bgAlpha });
        if (r.on) {
            const float inset = 3.0f * s;
            drawRect(Rect{ R.toggle.x + inset, R.toggle.y + inset,
                           R.toggle.w - 2.0f * inset, R.toggle.h - 2.0f * inset },
                     RGBA{ 0.35f, 0.62f, 0.95f, editable ? 1.0f : 0.5f });
        }

        // Each option cell shows its text, fitted to the cell, and lights up under the mouse
        auto cell = [&](const Rect& c, const std::string& label) {
            const bool hot = editable && c.contains(mx, my);
            drawRect(c, hot ? RGBA{ 0.22f, 0.28f, 0.38f, 0.90f } : RGBA{ 0.10f, 0.10f, 0.13f, 0.90f });
            drawListText(fitText(label, c.w - 8.0f * s, false), c.x + 4.0f * s, c.y + (c.h - listH) * 0.5f, text);
        };
        cell(R.conditionTarget, targetName(r.conditionTarget));
        cell(R.condition, std::string(conditionName(r.condition)) + " " + opName(r.op) + " " +
                          std::to_string(r.percent) + "%");
        cell(R.action, actionName(r.action));
        cell(R.actionTarget, targetName(r.actionTarget));

        const bool upOn = editable && i > 0;
        const bool downOn = editable && i + 1 < rules.size();
        drawSmallButton(R.up, "^", upOn, upOn && R.up.contains(mx, my));
        drawSmallButton(R.down, "v", downOn, downOn && R.down.contains(mx, my));
        drawSmallButton(R.remove, "x", editable, editable && R.remove.contains(mx, my));
    }

    const bool canAdd = editable && rules.size() < (size_t)BEHAVIOR_LIMIT;
    drawButton(L.addButton, "+ Add behavior", canAdd, canAdd && L.addButton.contains(mx, my));
}

void Renderer::drawBehaviorTip(const SidebarLayout& L, World& w, Entity unit, float mx, float my) {
    const BehaviorHit hit = behaviorHitAt(L, mx, my);
    const Behaviors* b = w.get<Behaviors>(unit);
    if (hit.row < 0 || !b || hit.row >= (int)b->rules.size()) return;
    const float s = uiH_ / 720.0f;
    std::vector<TipRow> rows(1);
    rows[0].text = describeRule(b->rules[(size_t)hit.row]);
    const float width = tipWidth(rows), height = tipHeight(rows.size());
    // Beside the mouse, moved to the other side or up when the window has no room for it
    float x = mx + 14.0f * s, y = my + 14.0f * s;
    if (x + width > uiW_) x = mx - 14.0f * s - width;
    if (y + height > uiH_) y = uiH_ - height;
    drawTip(rows, std::max(0.0f, x), std::max(0.0f, y), width);
}

ConditionEditorLayout Renderer::conditionEditorLayout(float x, float y, int winW, int winH) {
    return layoutConditionEditor(x, y, listTextHeight("Ag"), winW, winH);
}

void Renderer::drawConditionEditor(const ConditionEditorLayout& L, const BehaviorRule& r, float mx, float my) {
    drawRect(L.box, RGBA{ 0.04f, 0.04f, 0.06f, 0.92f });
    // A choice is lit when it is the rule's current one, and brighter still under the mouse
    auto choice = [&](const Rect& c, const std::string& label, bool current) {
        const bool hot = c.contains(mx, my);
        const RGBA bg = current ? RGBA{ 0.28f, 0.46f, 0.74f, 0.95f }
                      : hot     ? RGBA{ 0.25f, 0.34f, 0.50f, 0.95f }
                                : RGBA{ 0.14f, 0.17f, 0.22f, 0.95f };
        drawRect(c, bg);
        const float tw = listTextWidth(label), th = listTextHeight(label);
        drawListText(label, c.x + (c.w - tw) * 0.5f, c.y + (c.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    };
    for (size_t i = 0; i < L.kinds.size(); ++i) {
        choice(L.kinds[i], conditionName((BehaviorCondition)i), (int)r.condition == (int)i);
    }
    for (size_t i = 0; i < L.ops.size(); ++i) {
        choice(L.ops[i], opName((BehaviorOp)i), (int)r.op == (int)i);
    }
    static const char* const stepLabels[4] = { "-10", "-1", "+1", "+10" };
    for (size_t i = 0; i < L.steps.size() && i < 4; ++i) {
        choice(L.steps[i], stepLabels[i], false);
    }
    drawRect(L.value, RGBA{ 0.10f, 0.10f, 0.13f, 0.95f });
    const std::string pct = std::to_string(r.percent) + "%";
    drawListText(pct, L.value.x + (L.value.w - listTextWidth(pct)) * 0.5f,
                 L.value.y + (L.value.h - listTextHeight(pct)) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
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

void Renderer::drawGroundItems(World& w, const Terrain& t, const Camera& cam) {
    for (Entity e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
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

// The UI icon with this id: its picture from data/ui, loaded once. An id with no picture has no texture.
const TextTex& Renderer::uiIconTexture(int icon) {
    auto it = uiIcons_.find(icon);
    if (it != uiIcons_.end()) return it->second;
    const std::string num = icon < 10 ? "0" + std::to_string(icon) : std::to_string(icon);
    return uiIcons_.emplace(icon, loadOptionalImage(("data/ui/icon_" + num + ".png").c_str())).first->second;
}

void Renderer::drawUiIcon(int icon, const Rect& r) {
    const TextTex& t = uiIconTexture(icon);
    if (t.tex) {
        drawFitted(t, r, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        return;
    }
    // The placeholder until the picture is packed: a pink square with its id in it
    drawRect(r, RGBA{ 1.0f, 0.42f, 0.78f, 1.0f });
    const std::string num = icon < 10 ? "0" + std::to_string(icon) : std::to_string(icon);
    const float tw = listTextWidth(num), th = listTextHeight("Ag");
    drawListText(num, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 0.12f, 0.02f, 0.08f, 1.0f });
}

void Renderer::drawIconText(float x, float y, float h, int icon, const std::string& text, RGBA tint) {
    const float s = uiH_ / 720.0f;
    drawUiIcon(icon, Rect{ x, y, h, h });
    const float th = listTextHeight("Ag");
    drawListText(text, x + h + 4.0f * s, y + (h - th) * 0.5f, tint);
}

void Renderer::drawIconLabel(const Rect& r, int icon, const std::string& text, RGBA tint, bool uiFont) {
    const float s = uiH_ / 720.0f;
    const float gap = 6.0f * s;
    const float sz = std::max(0.0f, r.h - 8.0f * s);
    if (uiFont) {
        // The text gets the room beside the icon, and is shrunk to it when automatic text fit is on
        const float avail = std::max(0.0f, r.w - sz - gap - 8.0f * s);
        const std::string shown = fitShrinkText(text, avail);
        if (shown != text) noteTip(text, r);
        const float k = fitShrinkScale(shown, avail, r.h);
        const float x0 = r.x + (r.w - (sz + gap + uiTextWidth(shown) * k)) * 0.5f;
        drawUiIcon(icon, Rect{ x0, r.y + (r.h - sz) * 0.5f, sz, sz });
        drawUiTextScaled(shown, x0 + sz + gap, r.y + (r.h - uiTextHeight("Ag") * k) * 0.5f, k, tint);
        return;
    }
    const float textW = listTextWidth(text);
    const float textH = listTextHeight("Ag");
    const float x0 = r.x + (r.w - (sz + gap + textW)) * 0.5f;
    drawUiIcon(icon, Rect{ x0, r.y + (r.h - sz) * 0.5f, sz, sz });
    drawListText(text, x0 + sz + gap, r.y + (r.h - textH) * 0.5f, tint);
}

void Renderer::drawTitleBar(const Rect& bar, int icon, const std::string& title) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    drawRect(bar, RGBA{ 0.14f, 0.18f, 0.28f, 0.95f });
    if (icon == ICON_NONE) {
        drawListText(title, bar.x + 6.0f * s, bar.y + (bar.h - listTextHeight("Ag")) * 0.5f, white);
        return;
    }
    drawIconText(bar.x + 6.0f * s, bar.y + 3.0f * s, std::max(0.0f, bar.h - 6.0f * s), icon, title, white);
}

void Renderer::drawCollapseButton(const Rect& r, bool collapsed) {
    drawRect(r, RGBA{ 0.22f, 0.26f, 0.36f, 0.95f });
    const char* sign = collapsed ? "+" : "-";
    const float tw = listTextWidth(sign), th = listTextHeight("Ag");
    drawListText(sign, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

// The clock: the time in the larger line, and the date under it in the list font, as 500-01-01.
// The block's right edge is `right`, and it starts at `top`.
void Renderer::drawClock(const std::string& clock, const std::string& date, float right, float top, int winH) {
    const float s = winH / 720.0f;
    const float pad = 10.0f * s, lineGap = 4.0f * s;
    const RGBA fill{ 0.04f, 0.04f, 0.05f, 0.5f };
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.78f, 0.78f, 0.82f, 1.0f };

    const float bigW = clockTextWidth(clock), bigH = uiTextHeight("Ag");
    const float smallW = listTextWidth(date), smallH = listTextHeight("Ag");
    const float w = std::max(bigW, smallW) + 2.0f * pad;
    const float h = bigH + lineGap + smallH + 2.0f * pad;
    const Rect box{ right - w, top, w, h };
    // Both lines are right aligned to the same edge
    const float textRight = box.x + box.w - pad;

    drawRect(box, fill);
    drawClockText(clock, textRight - bigW, box.y + pad, white);
    drawListText(date, textRight - smallW, box.y + pad + bigH + lineGap, gray);
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
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.80f, 0.86f, 0.96f, 1.0f };
    const float bgAlpha = 0.5f;
    const float rowH = fontTextHeight(statsRow_, "Ag") + 1.0f * s;
    const float pad = L.left.x - L.box.x;
    const float iconH = fontTextHeight(statsRow_, "Ag");
    const float nameIndent = iconH + 4.0f * s;   // the stat names sit after their icons

    drawRect(L.box, RGBA{ 0.04f, 0.05f, 0.08f, bgAlpha });
    const std::string caption = langf("UILABEL_STATS_CAPTION", { name });
    drawFontText(statsTitle_, caption, L.box.x + (L.box.w - fontTextWidth(statsTitle_, caption)) * 0.5f,
                 L.box.y + pad, white);

    // Columns: the name at the left, then the level, the total XP and the XP still to go, right aligned
    auto row = [&](const Rect& col, float rowY, float indent, const std::string& a, const std::string& b,
                   const std::string& c, const std::string& d, RGBA tint) {
        drawFontText(statsRow_, a, col.x + indent, rowY, tint);
        drawFontText(statsRow_, b, col.x + 200.0f - fontTextWidth(statsRow_, b), rowY, tint);
        drawFontText(statsRow_, c, col.x + 312.0f - fontTextWidth(statsRow_, c), rowY, tint);
        drawFontText(statsRow_, d, col.x + col.w - fontTextWidth(statsRow_, d), rowY, tint);
    };
    auto column = [&](const Rect& col, const std::vector<StatGroup>& groups) {
        float rowY = col.y;
        row(col, rowY, nameIndent, lang("UILABEL_STAT"), lang("UILABEL_LEVEL"), lang("UILABEL_TOTAL_XP"), lang("UILABEL_TO_NEXT"), gray);
        rowY += rowH;
        for (StatGroup group : groups) {
            const std::string title = group == StatGroup::Attributes ? lang("UILABEL_ATTRIBUTES")
                              : group == StatGroup::Combat ? lang("UILABEL_COMBAT")
                              : group == StatGroup::Gathering ? lang("UILABEL_RESOURCE_GATHERING")
                              : group == StatGroup::Crafting ? lang("UILABEL_CRAFTING") : lang("UILABEL_UTILITY");
            drawFontText(statsRow_, title, col.x + nameIndent, rowY, heading);
            rowY += rowH;
            for (int i = 0; i < STAT_COUNT; ++i) {
                if (statInfo(i).group != group) continue;
                const int lv = levelOf(stats, i);
                const std::string next = lv >= MAX_LEVEL ? std::string(lang("UILABEL_MAX"))
                                       : withCommas(xpForLevel(lv + 1) - stats.xp[i]);
                drawUiIcon(statIcon(i), Rect{ col.x, rowY, iconH, iconH });
                row(col, rowY, nameIndent, statName(i), std::to_string(lv), withCommas(stats.xp[i]), next, white);
                rowY += rowH;
            }
        }
    };
    column(L.left, { StatGroup::Attributes, StatGroup::Combat });
    column(L.right, { StatGroup::Gathering, StatGroup::Crafting, StatGroup::Utility });

    const bool hot = L.close.contains(mx, my);
    drawRect(L.close, hot ? RGBA{ 0.25f, 0.34f, 0.50f, bgAlpha } : RGBA{ 0.14f, 0.17f, 0.22f, bgAlpha });
    const float tw = fontTextWidth(statsRow_, lang("UILABEL_CLOSE")), th = fontTextHeight(statsRow_, lang("UILABEL_CLOSE"));
    drawFontText(statsRow_, lang("UILABEL_CLOSE"), L.close.x + (L.close.w - tw) * 0.5f, L.close.y + (L.close.h - th) * 0.5f, white);
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

std::vector<Renderer::GroundStack> Renderer::groundStacks(World& w, const Terrain& t, const Camera& cam,
                                                          int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float padX = 7.0f * s, padY = 3.0f * s, gapY = 4.0f * s, lineGap = 2.0f * s;
    const float lineH = listTextHeight("Ag");
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);

    // The items on each tile, with their sprite positions, names and squared distance from the camera
    struct Seen { Entity e; ItemOnScreen os; std::string name; float dist2; };
    std::map<std::pair<int, int>, std::vector<Seen>> tiles;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z)) continue;
        Seen seen{ e, ItemOnScreen{}, itemName(gi->item), 0.0f };
        const float dx = tr->x - ex, dy = tr->y - ey, dz = tr->z - ez;
        seen.dist2 = dx * dx + dy * dy + dz * dz;
        if (seen.dist2 > LOD_MID_TILES * LOD_MID_TILES) continue;
        const TextTex* pic = itemTexture(gi->item);
        if (!itemOnScreen(vp, winW, winH, *tr, pic ? pic->aspect : white_.aspect, seen.os)) continue;
        tiles[{ (int)std::floor(tr->x), (int)std::floor(tr->z) }].push_back(seen);
    }

    std::vector<GroundStack> out;
    for (auto& entry : tiles) {
        std::vector<Seen>& seen = entry.second;
        // Entity order, so the names keep the same places from frame to frame
        std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.e < b.e; });

        // One box above the middle of the tile's items, one line per item, the widest name setting its width
        const float n = (float)seen.size();
        float cx = 0.0f, top = 0.0f, maxW = 0.0f, nearest = seen[0].dist2;
        for (const Seen& sn : seen) {
            cx += sn.os.cx;
            top += sn.os.top;
            maxW = std::max(maxW, listTextWidth(sn.name));
            nearest = std::min(nearest, sn.dist2);
        }
        cx /= n;
        top /= n;
        const float boxW = maxW + 2.0f * padX;
        const float boxH = n * lineH + (n - 1.0f) * lineGap + 2.0f * padY;

        GroundStack st;
        st.dist2 = nearest;
        st.box = Rect{ cx - boxW * 0.5f, top - gapY - boxH, boxW, boxH };
        if (st.box.x + st.box.w < 0.0f || st.box.x > winW || st.box.y + st.box.h < 0.0f || st.box.y > winH) continue;
        for (size_t i = 0; i < seen.size(); ++i) {
            st.items.push_back(seen[i].e);
            st.names.push_back(seen[i].name);
            st.rows.push_back(Rect{ st.box.x, st.box.y + padY + (float)i * (lineH + lineGap), boxW, lineH });
        }
        out.push_back(std::move(st));
    }
    // Farthest first, so the nearest box is drawn last, on top
    std::stable_sort(out.begin(), out.end(),
                     [](const GroundStack& a, const GroundStack& b) { return a.dist2 > b.dist2; });
    return out;
}

bool Renderer::topLabelRow(const std::vector<GroundStack>& stacks, float mx, float my, size_t& stack, size_t& row) {
    // The last stack is drawn on top, so search from the end. A box that covers the point without a
    // name line under it hides the boxes behind it.
    for (size_t k = stacks.size(); k-- > 0;) {
        const GroundStack& st = stacks[k];
        for (size_t j = 0; j < st.rows.size(); ++j) {
            if (st.rows[j].contains(mx, my)) {
                stack = k;
                row = j;
                return true;
            }
        }
        if (st.box.contains(mx, my)) return false;
    }
    return false;
}

void Renderer::drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH, float mx, float my) {
    const float s = winH / 720.0f;
    // Items on one tile share a box, with one line each, top line first and the last one nearest the item.
    // Boxes are drawn farthest first, so a nearer box covers the ones behind it. The one line on top under
    // (mx, my) gets a darker cell and gold text, so it is clear which item a click picks.
    const std::vector<GroundStack> stacks = groundStacks(w, t, cam, winW, winH);
    size_t hotStack = 0, hotRow = 0;
    const bool hasHot = topLabelRow(stacks, mx, my, hotStack, hotRow);
    for (size_t k = 0; k < stacks.size(); ++k) {
        const GroundStack& st = stacks[k];
        drawRect(Rect{ st.box.x - 1.0f, st.box.y - 1.0f, st.box.w + 2.0f, st.box.h + 2.0f }, LABEL_EDGE);
        drawRect(st.box, LABEL_FILL);
        for (size_t i = 0; i < st.names.size(); ++i) {
            const bool hot = hasHot && k == hotStack && i == hotRow;
            if (hot) drawRect(st.rows[i], LABEL_HOT_FILL);
            drawListText(st.names[i], st.box.x + 7.0f * s, st.rows[i].y, hot ? LABEL_HOT_TEXT : LABEL_TEXT);
        }
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
    const RGBA neutral{ 0.6f, 0.6f, 0.6f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    for (Entity u : w.view<Unit, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        const Name* nm = w.get<Name>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // The tag sits just above the top of the unit's picture
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, tr->y + GLYPH_H + 0.1f, tr->z, winW, winH, sx, sy)) continue;

        const std::string tag = "[" + nm->text + "]";
        const float tw = listTextWidth(tag), th = listTextHeight(tag);
        if (sx < -tw || sx > winW + tw || sy < -th || sy > winH + th) continue;
        const float x = sx - tw * 0.5f, y = sy - th;
        // The drop shadow is the same text in black, a little down and to the right
        drawListText(tag, x + 1.5f * s, y + 1.5f * s, shadow);
        drawListText(tag, x, y, w.has<Hostile>(u) ? hostile : w.has<Neutral>(u) ? neutral : player);
    }
}

// The color of a damage number or log line: green for a player unit's, red for a hostile's, gray otherwise
static RGBA logTint(Tint t) {
    switch (t) {
    case Tint::Green: return RGBA{ 0.42f, 0.95f, 0.45f, 1.0f };
    case Tint::Red:   return RGBA{ 1.0f, 0.38f, 0.36f, 1.0f };
    case Tint::Gray:  break;
    }
    return RGBA{ 0.78f, 0.78f, 0.82f, 1.0f };
}

void Renderer::drawFloatingNumbers(const std::vector<FloatingNumber>& floats, const Camera& cam, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (const FloatingNumber& f : floats) {
        if (!withinDetail(cam, f.x, f.y, f.z)) continue;
        // Anchored just above the unit's head, like its name
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, f.x, f.y + GLYPH_H, f.z, winW, winH, sx, sy)) continue;

        // Arc: up and to the right, then down, fading out over the last 40% of its life. The rise peaks
        // about two fifths of the way through (about 38 pixels up at 720 lines), then falls 60 pixels below its start.
        const float t = std::clamp(f.age / FLOAT_LIFE, 0.0f, 1.0f);
        const float rise = (200.0f * t - 260.0f * t * t) * s;
        const float drift = 40.0f * t * s;
        const float alpha = t < 0.6f ? 1.0f : (1.0f - t) / 0.4f;

        const std::string text = "-" + std::to_string(f.amount);
        const float x = sx + drift - uiTextWidth(text) * 0.5f;
        const float y = sy - 4.0f * s - uiTextHeight(text) - rise;
        RGBA c = logTint(f.tint);
        c.a *= alpha;
        drawUiText(text, x + 1.5f * s, y + 1.5f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f * alpha });
        drawUiText(text, x, y, c);
    }
}

// Trade values over their units: a second's life, fading out over its last 30%, above the name tag
void Renderer::drawTradeLabels(const std::vector<TradeLabel>& labels, World& w, const Camera& cam, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (const TradeLabel& l : labels) {
        const Transform* tr = w.get<Transform>(l.unit);
        if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, tr->y + GLYPH_H * 2.2f, tr->z, winW, winH, sx, sy)) continue;
        const float t = std::clamp(l.age / TRADE_LABEL_LIFE, 0.0f, 1.0f);
        const float alpha = t < 0.7f ? 1.0f : (1.0f - t) / 0.3f;
        const float x = sx - uiTextWidth(l.text) * 0.5f, y = sy - uiTextHeight(l.text);
        drawUiText(l.text, x + 1.5f * s, y + 1.5f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f * alpha });
        drawUiText(l.text, x, y, RGBA{ 1.0f, 0.90f, 0.45f, alpha });
    }
}

// The rows of the Game tab's filter menu, in order. A heading names a group of kinds and is not clickable.
struct FilterEntry {
    const char* label;     // a language key, looked up when the menu is drawn
    LogKind kind;
    bool heading;
};
static const FilterEntry kFilterEntries[] = {
    { "UILABEL_COMBAT_MESSAGES", LogKind::Other, true },
    { "UILABEL_SEEN", LogKind::Seen, false },
    { "UILABEL_HIT", LogKind::Hit, false },
    { "UILABEL_MISS", LogKind::Miss, false },
    { "UILABEL_DAMAGE", LogKind::Damage, false },
    { "UILABEL_EXPERIENCE_MESSAGES", LogKind::Other, true },
    { "UILABEL_EXPERIENCE", LogKind::Xp, false },
    { "UILABEL_ITEM_MESSAGES", LogKind::Other, true },
    { "UILABEL_PICKUP", LogKind::Pickup, false },
    { "UILABEL_DROP", LogKind::Drop, false },
    { "UILABEL_LOOT", LogKind::Loot, false },
};
constexpr int kFilterRowCount = (int)(sizeof(kFilterEntries) / sizeof(kFilterEntries[0]));

CombatLogLayout Renderer::combatLogLayout(int winW, int winH, int tab, bool filterOpen, int offset, int lineCount,
                                          float rightLimit, bool collapsed, const PanelPlace& place) {
    const float s = winH / 720.0f;
    const float pad = 8.0f * s, scrollW = 10.0f * s, gap = 4.0f * s;
    const float barH = 22.0f * s, tabH = 20.0f * s;
    const float lineH = listTextHeight("Ag") + 2.0f * s;
    // Half the height of the Unit Panel by default, or as dragged. As many lines as that has room for, under the tabs.
    const float bodyH = place.h > 0.0f ? std::min(place.h, (float)winH) : unitPanelBodyHeight(winH) * 0.5f;
    const int linesVisible = std::max(1, (int)std::floor((bodyH - barH - tabH - 2.0f * pad) / lineH));

    CombatLogLayout L;
    L.linesVisible = linesVisible;
    L.lineCount = lineCount;
    L.lineH = lineH;
    L.offset = std::clamp(offset, 0, L.maxOffset());
    L.tab = std::clamp(tab, 0, 1);
    L.filterOpen = filterOpen && L.tab == 1 && !collapsed;

    // By default its width stops short of the Squad Panel. A dragged width stays between its minimum and the screen.
    const float w = place.w > 0.0f
        ? std::min(std::max(place.w, 120.0f * s), winW - 16.0f * s)
        : std::max(180.0f * s, std::min(340.0f * s, rightLimit - 8.0f * s));
    const float h = collapsed ? barH : bodyH;
    L.panel = placedRect(place, 8.0f * s, w, h, winW, winH, 8.0f * s);
    const float x = L.panel.x, y = L.panel.y;
    L.bar = Rect{ x, y, w, barH };
    L.toggle = BarToggle(L.bar, s);
    L.collapsed = collapsed;
    L.textX = x + pad;
    L.textW = w - 2.0f * pad - scrollW - gap;

    // Which lines are visible: the window ends L.offset lines above the newest
    const int end = lineCount - L.offset;
    L.shown = std::min(linesVisible, std::max(0, end));
    L.first = end - L.shown;

    const float trackX = x + w - pad * 0.5f - scrollW * 0.5f;
    const float trackY = y + barH + tabH + pad, trackH = h - barH - tabH - 2.0f * pad;
    L.track = Rect{ trackX, trackY, scrollW * 0.5f, trackH };
    if (L.scrollable()) {
        const float share = (float)linesVisible / (float)lineCount;
        const float thumbH = std::max(16.0f * s, trackH * share);
        // At offset 0 the thumb sits at the bottom, and at the top of its range it sits at the top
        const float frac = L.maxOffset() > 0 ? (float)L.offset / (float)L.maxOffset() : 0.0f;
        L.thumb = Rect{ trackX, trackY + (trackH - thumbH) * (1.0f - frac), scrollW * 0.5f, thumbH };
    }

    // The tabs under the bar. The Filter button sits at the right of the row.
    const float tabY = y + barH;
    L.tabMessage = Rect{ x + pad * 0.5f, tabY + 2.0f * s, 84.0f * s, tabH - 2.0f * s };
    L.tabGame = Rect{ L.tabMessage.x + L.tabMessage.w + 4.0f * s, tabY + 2.0f * s, 60.0f * s, tabH - 2.0f * s };
    L.filterBtn = Rect{ x + w - pad - scrollW - 62.0f * s, tabY + 2.0f * s, 62.0f * s, tabH - 2.0f * s };

    // The filter menu opens upward from the Filter button, so it stays on screen at the bottom of the window
    if (L.filterOpen) {
        const float rowH = listTextHeight("Ag") + 6.0f * s;
        const float menuW = 160.0f * s;
        const float menuH = kFilterRowCount * rowH + 8.0f * s;
        L.filterMenu = Rect{ L.filterBtn.x + L.filterBtn.w - menuW, std::max(0.0f, tabY + tabH - menuH), menuW, menuH };
        for (int i = 0; i < kFilterRowCount; ++i) {
            const Rect r{ L.filterMenu.x, L.filterMenu.y + 4.0f * s + i * rowH, menuW, rowH };
            L.filterRows.push_back(CombatLogLayout::FilterRow{ r, kFilterEntries[i].kind, kFilterEntries[i].heading,
                                                               lang(kFilterEntries[i].label) });
        }
    }
    if (collapsed) {
        L.track = L.thumb = Rect{};
        L.tabMessage = L.tabGame = L.filterBtn = Rect{};
    }
    return L;
}

void Renderer::drawCombatLog(const CombatLogLayout& L, const std::vector<const LogLine*>& rows, const LogFilter& filter,
                             float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA lit{ 0.22f, 0.26f, 0.36f, 0.95f };
    const RGBA dim{ 0.10f, 0.11f, 0.15f, 0.90f };

    drawRect(L.panel, RGBA{ 0.05f, 0.06f, 0.09f, 0.86f });
    drawTitleBar(L.bar, ICON_COMBAT_LOG, lang("UILABEL_INFORMATION_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    // The two tabs: the one shown is lit
    auto tab = [&](const Rect& r, const std::string& label, bool selected) {
        drawRect(r, selected ? lit : dim);
        drawListText(label, r.x + (r.w - listTextWidth(label)) * 0.5f, r.y + (r.h - listTextHeight("Ag")) * 0.5f,
                     selected ? white : gray);
    };
    tab(L.tabMessage, lang("UILABEL_MESSAGE"), L.tab == 0);
    tab(L.tabGame, lang("UILABEL_GAME"), L.tab == 1);
    if (L.tab == 1) drawButton(L.filterBtn, lang("UILABEL_FILTER"), true, L.filterBtn.contains(mx, my) || L.filterOpen);

    // The rows are centred in the space under the tabs
    const float tabH = 20.0f * s;
    const float bodyTop = L.bar.y + L.bar.h + tabH;
    const float top = bodyTop + (L.panel.y + L.panel.h - bodyTop - L.linesVisible * L.lineH) * 0.5f;
    const int count = (int)rows.size();
    for (int k = 0; k < L.shown && L.first + k < count; ++k) {
        const LogLine& ln = *rows[(size_t)(L.first + k)];
        // Bottom-aligned: the newest line is the last row
        const int row = L.linesVisible - L.shown + k;
        drawListText(fitText(ln.text, L.textW, false), L.textX, top + row * L.lineH, logTint(ln.tint));
    }
    if (L.scrollable()) {
        drawRect(L.track, RGBA{ 0.14f, 0.16f, 0.21f, 0.9f });
        drawRect(L.thumb, RGBA{ 0.50f, 0.55f, 0.66f, 0.9f });
    }

    // The filter menu, over the lines. Each kind reads On or Off.
    if (L.filterOpen) {
        drawRect(L.filterMenu, RGBA{ 0.04f, 0.05f, 0.08f, 0.97f });
        const float th = listTextHeight("Ag");
        for (const CombatLogLayout::FilterRow& fr : L.filterRows) {
            if (fr.heading) {
                drawListText(fr.label, fr.r.x + 6.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, RGBA{ 0.80f, 0.86f, 0.96f, 1.0f });
                continue;
            }
            if (fr.r.contains(mx, my)) drawRect(fr.r, RGBA{ 1.0f, 1.0f, 1.0f, 0.10f });
            const bool on = filter.on[(int)fr.kind];
            drawListText(fr.label, fr.r.x + 16.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, white);
            const std::string state = on ? lang("UILABEL_ON") : lang("UILABEL_OFF");
            drawListText(state, fr.r.x + fr.r.w - 10.0f * s - listTextWidth(state), fr.r.y + (fr.r.h - th) * 0.5f,
                         on ? RGBA{ 0.6f, 1.0f, 0.6f, 1.0f } : gray);
        }
    }
}

void Renderer::drawPartySetup(const std::vector<Button>& buttons, const Party& p, const PartyUi& ui,
                              const PartyPanels& P, const std::string& mapName, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
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
    // Text centerd in a box, shortened to fit
    auto centerd = [&](const Rect& r, const std::string& text, RGBA tint) {
        const std::string t = fitText(text, r.w - 8.0f * s, false);
        drawListText(t, r.x + (r.w - listTextWidth(t)) * 0.5f, r.y + (r.h - lineH) * 0.5f, tint);
    };
    // Text at the left of a box, vertically centerd, after an inset
    auto leftText = [&](const Rect& r, const std::string& text, RGBA tint, float inset) {
        const std::string t = fitText(text, r.w - inset - 6.0f * s, false);
        if (t != text) noteTip(text, r);
        drawListText(t, r.x + inset, r.y + (r.h - lineH) * 0.5f, tint);
    };

    drawRect(P.left, panel);
    drawRect(P.center, panel);
    drawRect(P.right, panel);
    drawRect(P.bottom, RGBA{ 0.03f, 0.03f, 0.05f, 0.95f });
    drawUiTextCentered(langf("UILABEL_STARTING_PARTY", { mapName }), uiW_ * 0.5f, (P.topH - uiTextHeight("Ag")) * 0.5f, white);

    for (const Button& b : buttons) {
        const bool hot = b.enabled && b.rect.contains(mx, my);
        switch (b.action) {
        case Action::PartyPick: {
            const PartyMember& u = p[static_cast<size_t>(b.param)];
            const bool chosen = b.param == std::clamp(ui.selected, 0, std::max(0, (int)p.size() - 1));
            drawRect(b.rect, plateFor(hot, chosen, true));
            const float pic = b.rect.h - 14.0f * s;
            drawUnitPicture(u.sprite, Rect{ b.rect.x + 7.0f * s, b.rect.y + 7.0f * s, pic, pic });
            const float tx = b.rect.x + pic + 16.0f * s, tw = b.rect.x + b.rect.w - tx - 8.0f * s;
            const bool named = !cleanName(u.name).empty();
            const std::string shown = named ? cleanName(u.name) : std::string(lang("UILABEL_NO_NAME"));
            drawListText(fitText(shown, tw, false), tx, b.rect.y + 22.0f * s,
                         !named ? warn : chosen ? hover : white);
            drawListText(langf("UILABEL_SPRITE_N", { std::to_string(u.sprite) }), tx, b.rect.y + 22.0f * s + lineH + 6.0f * s, gray);
            break;
        }

        case Action::PartyTabPick: {
            const bool on = b.param == (int)ui.tab;
            drawRect(b.rect, plateFor(hot, on, true));
            drawIconLabel(b.rect, partyTabIcon(b.param), b.label, on ? hover : white, false);
            break;
        }

        case Action::PartyNameField: {
            drawListText(lang("UILABEL_NAME"), b.rect.x, b.rect.y - 22.0f * s, gray);
            drawRect(b.rect, plateFor(hot, ui.editingName, true));
            if (b.label.empty() && !ui.editingName) {
                leftText(b.rect, lang("UILABEL_CLICK_TO_TYPE"), faint, 10.0f * s);
            } else {
                leftText(b.rect, b.label + (ui.editingName ? "|" : ""), white, 10.0f * s);
            }
            break;
        }

        case Action::PartySpriteBox:
            drawListText(lang("UILABEL_SPRITE"), b.rect.x, b.rect.y - 22.0f * s, gray);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Sprite, true));
            leftText(b.rect, b.label, white, 10.0f * s);
            break;

        case Action::PartyBodyBox:
            drawListText(lang("UILABEL_BODY_TYPE"), b.rect.x, b.rect.y - 22.0f * s, gray);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Body, true));
            leftText(b.rect, b.label, white, 10.0f * s);
            break;

        case Action::PartyRandomUnit:
            drawRect(b.rect, plateFor(hot, false, true));
            centerd(b.rect, b.label, white);
            break;

        case Action::PartyAddUnit:
        case Action::PartyRemoveUnit:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? white : faint);
            break;

        case Action::PartyGearSlot: {
            const bool open = ui.dropKind == DropKind::Gear && ui.dropSlot == b.param;
            drawRect(b.rect, plateFor(hot, open, true));
            leftText(Rect{ b.rect.x, b.rect.y + 4.0f * s, b.rect.w, lineH }, gearSlotName(b.param), gray, 8.0f * s);
            leftText(Rect{ b.rect.x, b.rect.y + b.rect.h - lineH - 4.0f * s, b.rect.w, lineH }, b.label,
                     b.label == lang("UILABEL_EMPTY") ? faint : white, 8.0f * s);
            break;
        }

        case Action::PartyStatLabel: {
            drawRect(b.rect, RGBA{ 0.0f, 0.0f, 0.0f, 0.18f });
            const float isz = b.rect.h - 4.0f * s;
            drawUiIcon(statIcon(b.param), Rect{ b.rect.x + 4.0f * s, b.rect.y + 2.0f * s, isz, isz });
            leftText(Rect{ b.rect.x + isz + 8.0f * s, b.rect.y, b.rect.w - isz - 8.0f * s, b.rect.h }, b.label, white, 2.0f * s);
            const std::string lv = std::to_string(levelOf(picked.stats, b.param));
            drawListText(lv, b.rect.x + b.rect.w - 6.0f * s - listTextWidth(lv),
                         b.rect.y + (b.rect.h - lineH) * 0.5f, hover);
            break;
        }

        case Action::PartyStep:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? white : faint);
            break;

        case Action::PartyHeading:
            leftText(b.rect, b.label, heading, 0.0f);
            break;

        case Action::PartyNote:
            leftText(b.rect, b.label, gray, 0.0f);
            break;

        case Action::PartyInvSlot: {
            const ItemStack& st = picked.items[b.param];
            drawRect(b.rect, plateFor(hot, b.param == ui.stackSlot, true));
            if (st.item > 0) {
                if (const TextTex* pic = itemTexture(st.item)) {
                    drawFitted(*pic, Rect{ b.rect.x + b.rect.w * 0.12f, b.rect.y + b.rect.h * 0.12f,
                                           b.rect.w * 0.76f, b.rect.h * 0.76f }, white);
                } else {
                    centerd(b.rect, itemInitials(itemNameOf(st.item)), white);
                }
                const std::string n = std::to_string(st.count);
                drawListText(n, b.rect.x + b.rect.w - listTextWidth(n) - 3.0f * s,
                             b.rect.y + b.rect.h - lineH - 2.0f * s, gray);
            }
            break;
        }

        case Action::PartyCount:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? white : faint);
            break;

        case Action::PartyMessage:
            centerd(b.rect, b.label, partyNamesValid(p) ? gray : warn);
            break;

        case Action::PartyDropBack:
            drawRect(Rect{ b.rect.x - 1.0f, b.rect.y - 1.0f, b.rect.w + 2.0f, b.rect.h + 2.0f }, RGBA{ 0.45f, 0.50f, 0.70f, 0.9f });
            drawRect(b.rect, RGBA{ 0.04f, 0.05f, 0.08f, 0.98f });
            break;

        case Action::PartyDropScroll:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? (hot ? hover : white) : faint);
            break;

        case Action::PartyDropPick:
            if (hot) drawRect(b.rect, plateHot);
            leftText(b.rect, b.label, hot ? hover : white, 10.0f * s);
            break;

        case Action::PartyRandomAll:
        case Action::PartyConfirm:
        case Action::Back:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? white : faint);
            break;

        default:
            break;
        }
    }

    // Middle: the picked unit, large, with its name and totals under it. With no units, only the message.
    if (p.empty()) {
        drawUiTextCentered(fitText(lang("UILABEL_NEEDS_UNITS"), P.center.w - 24.0f * s, true), P.center.x + P.center.w * 0.5f,
                           P.center.y + P.center.h * 0.5f, warn);
        return;
    }
    const float cw = P.center.w;
    drawUnitPicture(picked.sprite, Rect{ P.center.x + 20.0f * s, P.center.y + 20.0f * s, cw - 40.0f * s,
                                         P.center.h - 130.0f * s });
    const bool named = !cleanName(picked.name).empty();
    const std::string name = named ? cleanName(picked.name) : std::string(lang("UILABEL_NO_NAME"));
    drawUiTextCentered(fitText(name, cw - 24.0f * s, true), P.center.x + cw * 0.5f,
                       P.center.y + P.center.h - 100.0f * s, named ? white : warn);
    int attack = 0, defense = 0;
    gearTotals(wornBy(picked), attack, defense);
    const std::string totals = langf("UILABEL_ATTACK_DEFENSE_CARRY", { std::to_string(attack), std::to_string(defense), std::to_string((int)carryLimit(picked.stats)) });
    const std::string shownTotals = fitText(totals, cw - 24.0f * s, false);
    drawListText(shownTotals, P.center.x + (cw - listTextWidth(shownTotals)) * 0.5f,
                 P.center.y + P.center.h - 62.0f * s, gray);
}

Entity Renderer::groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH) {
    // A name line in a label box picks its item. The box on top wins, so the nearer item is picked.
    const std::vector<GroundStack> stacks = groundStacks(w, t, cam, winW, winH);
    size_t k = 0, j = 0;
    if (topLabelRow(stacks, mx, my, k, j)) return stacks[k].items[j];

    // Otherwise the sprite under the mouse
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;

        ItemOnScreen os;
        const TextTex* pic = itemTexture(gi->item);
        if (!itemOnScreen(vp, winW, winH, *tr, pic ? pic->aspect : white_.aspect, os)) continue;
        const bool onSprite = mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom;
        if (!onSprite) continue;

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
