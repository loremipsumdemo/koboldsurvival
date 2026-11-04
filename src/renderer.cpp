#include "renderer.h"
#include "language.h"
#include "loading.h"
#include "terrain_mesh.h"
#include "equip.h"
#include "factions.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "pak.h"
#include "party.h"
#include "systems.h"
#include "claims.h"
#include "textcase.h"
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
constexpr float CARRIED_LIFT = 0.55f;    // how high a carried unit lies, over its carrier's shoulders
constexpr float CARRIED_MODEL_LIFT = 0.95f;   // the same for a unit drawn by its model
constexpr float FACTION_TINT = 0.85f;    // how far a unit model's colors are pulled toward its faction's color
constexpr float ITEM_GLYPH_H = GLYPH_H * 0.5f;   // world height of an item sprite on the ground: half a unit's
constexpr float SKY_SPAN = 0.2f;         // fraction of the panorama visible across the screen
constexpr float TWO_PI = 6.28318530f;

const RGBA WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };

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

// An item on the ground stands in the world as a square picture ITEM_GLYPH_H high, half a unit's height, so it grows
// and shrinks with the zoom like the units. aspect is the picture's width over its height (1 for an item).
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
uniform float uSat;   // 1 keeps the colors, 0 is fully gray
uniform vec4 uFaction;   // a faction's color the texture's colors are pulled toward, by a (0 leaves them as they are)
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.1) discard;   // keep only the visible pixels
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    // The faction's color at the texture's own brightness, so shading and detail stay
    vec3 fl = uFaction.rgb / max(dot(uFaction.rgb, vec3(0.299, 0.587, 0.114)), 0.05);
    c.rgb = mix(c.rgb, clamp(fl * l, 0.0, 1.0), uFaction.a);
    l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    c.rgb = mix(vec3(l), c.rgb, uSat);
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
                    const std::function<void(float, const std::string&)>& progress) {
    // How far along it is, and the kind of asset loading now with how many of it are done, named under the loading
    // bar; log.txt gets how long each kind took. Nothing is shown until the fonts and shaders the loading screen draws
    // with are ready.
    LoadStages stages("Start-up", progress);
    font_ = font;
    hudFont_ = hudFont;
    uiFont_ = uiFont;

    colorProgram_ = linkProgram(COLOR_VS, COLOR_FS);
    uMVPColor_ = glGetUniformLocation(colorProgram_, "uMVP");

    glyphProgram_ = linkProgram(GLYPH_VS, GLYPH_FS);
    uMVPGlyph_ = glGetUniformLocation(glyphProgram_, "uMVP");
    uTint_ = glGetUniformLocation(glyphProgram_, "uTint");
    uSat_ = glGetUniformLocation(glyphProgram_, "uSat");
    uFaction_ = glGetUniformLocation(glyphProgram_, "uFaction");
    glUseProgram(glyphProgram_);
    glUniform1i(glGetUniformLocation(glyphProgram_, "uTex"), 0);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1f(uSat_, 1.0f);
    glUniform4f(uFaction_, 0.0f, 0.0f, 0.0f, 0.0f);
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
    // The _0 file of each kind of picture stands in for one that is missing (and pink_ when that is missing too)
    const int pictureCount = 3 + UNIT_SPRITE_COUNT;
    stages.stage(0.0f, "LOADING_PICTURES", pictureCount);
    itemNone_ = loadOptionalImage("data/item/item_0.png");
    emoteFallback_ = loadOptionalImage("data/emote/emote_0.png");
    iconFallback_ = loadOptionalImage("data/icon/icon_0.png");
    stages.step(0.03f, 3);
    // Each unit model's profile picture, numbered as the models are
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/profile/profile_%02d.png", i);
        profilePics_[(size_t)i - 1] = loadOptionalImage(path);
        stages.step(0.03f + 0.02f * (float)i / (float)UNIT_SPRITE_COUNT, 3 + i);
    }
    stages.stage(0.05f, "LOADING_UNIT_MODELS", UNIT_SPRITE_COUNT);
    // Each unit model: data/model/unit/unit_<nn>.glb. A file with the same bytes as an earlier one is not loaded
    // again: its sprite number shares the earlier model. A missing or unreadable one leaves its units pink squares.
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/model/unit/unit_%02d.glb", i);
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (!pakFind(path, bytes, size)) {
            SDL_Log("Unit model %s not in data.pak: units with sprite %d are drawn as pink squares", path, i);
            stages.step(0.05f + 0.35f * (float)i / (float)UNIT_SPRITE_COUNT, i);
            continue;
        }
        for (int j = 1; j < i && !unitModelFor_[(size_t)i - 1]; ++j) {
            char other[64];
            std::snprintf(other, sizeof(other), "data/model/unit/unit_%02d.glb", j);
            const unsigned char* otherBytes = nullptr;
            size_t otherSize = 0;
            if (unitModelFor_[(size_t)j - 1] && pakFind(other, otherBytes, otherSize) && otherSize == size
                && std::memcmp(otherBytes, bytes, size) == 0) {
                unitModelFor_[(size_t)i - 1] = unitModelFor_[(size_t)j - 1];
            }
        }
        if (!unitModelFor_[(size_t)i - 1]) {
            Model& m = unitModels_[(size_t)i - 1];
            if (loadGLBModel(path, m) && m.vao && !m.draws.empty() && m.maxY > m.minY) {
                unitModelFor_[(size_t)i - 1] = &m;
            } else {
                destroyModel(m);
                m = Model{};
                SDL_Log("Unit model %s could not be read or has nothing to draw: units with sprite %d are drawn as pink squares",
                        path, i);
            }
        }
        stages.step(0.05f + 0.35f * (float)i / (float)UNIT_SPRITE_COUNT, i);
    }
    int mobModelCount = 0;
    for (int kind = 1; kind <= MOB_KIND_COUNT; ++kind) mobModelCount += mobInfo(kind).model > 0 ? 1 : 0;
    stages.stage(0.4f, "LOADING_MOB_MODELS", mobModelCount);
    // Each mob kind's model, when it has one: data/model/mob/mob_<nn>.glb. A missing one leaves it drawn by its picture.
    int mobModelsDone = 0;
    for (int kind = 1; kind <= MOB_KIND_COUNT; ++kind) {
        const int n = mobInfo(kind).model;
        if (n <= 0) continue;
        ++mobModelsDone;
        char path[64];
        std::snprintf(path, sizeof(path), "data/model/mob/mob_%02d.glb", n);
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        Model& m = mobModels_[(size_t)kind - 1];
        if (!pakFind(path, bytes, size)) {
            SDL_Log("Mob model %s not in data.pak: the %s is drawn by its picture", path, lang(mobInfo(kind).nameKey).c_str());
        } else if (!loadGLBModel(path, m) || !m.vao || m.draws.empty() || !(m.maxY > m.minY)) {
            destroyModel(m);
            m = Model{};
            SDL_Log("Mob model %s could not be read or has nothing to draw: the %s is drawn by its picture", path,
                    lang(mobInfo(kind).nameKey).c_str());
        }
        stages.step(0.4f + 0.1f * (float)mobModelsDone / (float)std::max(1, mobModelCount), mobModelsDone);
    }
    stages.stage(0.5f, "LOADING_MOB_PICTURES", MOB_KIND_COUNT + 1);
    mobFallback_ = loadOptionalImage("data/mob/mob_0.png");
    mobSprites_.reserve(MOB_KIND_COUNT);
    for (int i = 1; i <= MOB_KIND_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/mob/mob_%d.png", i);
        mobSprites_.push_back(loadOptionalImage(path));
        stages.step(0.5f + 0.05f * (float)i / (float)MOB_KIND_COUNT, i + 1);
    }
    stages.stage(0.55f, "LOADING_SKY");

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
    {
        // One-texel textures: plain white, for colored rectangles and glyphs, and the pink square drawn for a picture
        // that is missing along with its _0 stand-in
        auto texel = [](TextTex& t, unsigned char r, unsigned char g, unsigned char b) {
            const unsigned char px[4] = { r, g, b, 255 };
            glGenTextures(1, &t.tex);
            glBindTexture(GL_TEXTURE_2D, t.tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            t.aspect = 1.0f;
        };
        texel(white_, 255, 255, 255);
        texel(pink_, 255, 89, 191);   // MISSING_PROP_PINK
    }
    stages.stage(0.57f, "LOADING_PROP_MODELS", PROP_KIND_COUNT + 1);

    // The stand-in model, model_0.glb, for a kind whose own model is missing or unreadable
    {
        const char* fallbackFile = "data/model/model_0.glb";
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (pakFind(fallbackFile, bytes, size)) {
            const bool read = loadGLBModel(fallbackFile, fallbackModel_);
            const float extent = std::max({ fallbackModel_.maxX - fallbackModel_.minX, fallbackModel_.maxY - fallbackModel_.minY,
                                            fallbackModel_.maxZ - fallbackModel_.minZ });
            if (!read || !fallbackModel_.vao || fallbackModel_.draws.empty() || !(extent > 0.0f)) {
                destroyModel(fallbackModel_);
                fallbackModel_ = Model{};
                SDL_Log("The stand-in model %s could not be read or has nothing to draw", fallbackFile);
            }
        }
    }
    stages.step(0.58f, 1);
    const char* standIn = fallbackModel_.vao ? "with model_0.glb" : "as a pink square";
    // Each prop kind's model: exactly data/model/model_<n>.glb (props.h). No other name is tried, so a file that is
    // missing or misnamed shows up at once: that kind is drawn with the stand-in (propModel), and log.txt names it.
    for (int kind = 0; kind < PROP_KIND_COUNT; ++kind) {
        const PropInfo& info = propInfo(kind);
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (!pakFind(info.file, bytes, size)) {
            SDL_Log("Prop model %s not in data.pak: the %s is drawn %s", info.file, lang(info.nameKey).c_str(), standIn);
        } else {
            Model& m = propModels_[(size_t)kind];
            const bool read = loadGLBModel(info.file, m);
            const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
            if (!read || !m.vao || m.draws.empty() || !(extent > 0.0f)) {
                // Unreadable, or nothing in it to draw: drop what was made, so the stand-in shows instead
                destroyModel(m);
                m = Model{};
                SDL_Log("Prop model %s could not be read or has nothing to draw: the %s is drawn %s",
                        info.file, lang(info.nameKey).c_str(), standIn);
            }
        }
        stages.step(0.58f + 0.4f * (float)(kind + 1) / (float)PROP_KIND_COUNT, kind + 2);
    }
    stages.finish();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return colorProgram_ != 0 && glyphProgram_ != 0;
}

void Renderer::shutdown() {
    TextTex* textures[] = { &white_, &pink_, &itemNone_, &emoteFallback_, &iconFallback_ };
    for (TextTex* g : textures) {
        if (g->tex) glDeleteTextures(1, &g->tex);
        g->tex = 0;
    }
    for (TextTex& g : profilePics_) {
        if (g.tex) glDeleteTextures(1, &g.tex);
        g = TextTex{};
    }
    for (Model& m : unitModels_) destroyModel(m);
    for (Model& m : mobModels_) destroyModel(m);
    unitModelFor_.fill(nullptr);
    unitFacing_.clear();
    for (auto& entry : itemPics_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    itemPics_.clear();
    for (auto& entry : emotePics_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    emotePics_.clear();
    // Both text sets: the one in use, then the parked one
    clearActiveText();
    swapTextSet();
    clearActiveText();
    swapTextSet();
    for (Model& m : propModels_) destroyModel(m);
    destroyModel(fallbackModel_);

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

// A prop kind with no model is drawn as a pink square this many tiles on a side: the height of a unit's sprite, so it
// stands like a unit would, whatever the kind
float missingPropSide(int) {
    return GLYPH_H;
}

void Renderer::setSaturation(float sat) {
    glUseProgram(glyphProgram_);
    glUniform1f(uSat_, sat);
}

// How high the top of a unit's sprite is above its feet: its height standing, or lying (Unconscious) its width, over
// its carrier's shoulders if it is carried
float Renderer::unitTop(World& w, Entity u) {
    const float h = unitHeight(w, u);
    if (w.has<Carried>(u)) return h * unitAspect(w, u) + (unitModel(w, u) ? CARRIED_MODEL_LIFT : CARRIED_LIFT);
    if (w.has<Unconscious>(u) || w.has<PlayingDead>(u)) return h * unitAspect(w, u);
    return h + unitHover(w, u);
}

float Renderer::unitHover(World& w, Entity u) const {
    const Mob* mb = w.get<Mob>(u);
    if (!mb) return 0.0f;
    if (w.has<Unconscious>(u) || w.has<PlayingDead>(u) || w.has<Corpse>(u) || w.has<Carried>(u) || w.has<Occupying>(u)) {
        return 0.0f;
    }
    return mobHoverFraction(mb->kind) * unitHeight(w, u);
}

// How gray a unit is drawn: half for one Unconscious, fully for a corpse, not at all otherwise
static float unitSaturation(World& w, Entity u) {
    if (w.has<Corpse>(u)) return 0.0f;
    if (w.has<Unconscious>(u)) return 0.5f;
    return 1.0f;
}

void Renderer::drawLyingGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint, float toward) {
    if (!g.tex) return;
    // The sprite turned 90 degrees, lying on its side across the camera's view: its height runs along the right
    // vector and its width up from the ground. Its head is to the right.
    float dx = billX_ - x, dz = billZ_ - z;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    const float rx = dz, rz = -dx;
    const float hw = h * 0.5f;                 // half its length, lying
    const float up = h * g.aspect;             // how high it is, lying: its width
    // Moved a little toward the camera, so a carried unit shows in front of its carrier
    const float cx = x + dx * toward, cz = z + dz * toward;
    const float y0 = y, y1 = y + up;
    float v[4 * 5] = {
        cx - rx * hw, y0, cz - rz * hw, 1.0f, 1.0f,   // bottom-left: the sprite's bottom right
        cx + rx * hw, y0, cz + rz * hw, 1.0f, 0.0f,   // bottom-right: its top right
        cx + rx * hw, y1, cz + rz * hw, 0.0f, 0.0f,   // top-right: its top left
        cx - rx * hw, y1, cz - rz * hw, 0.0f, 1.0f    // top-left: its bottom left
    };
    drawQuadVerts(g.tex, v, tint);
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

void Renderer::drawModel(const Model& m, float x, float y, float z, float targetSize, float yaw) {
    if (!m.vao) return;
    // Scale, turn by yaw, then move so the center of the bottom sits on (x, y, z): the same placement picking uses
    const ModelPlacement p = placeModel(m, x, y, z, targetSize, yaw);
    if (!p.ok) return;
    Mat4 model{};
    // Column-major: the turn about the vertical axis, scaled by k. x' = cs*x + sn*z, z' = -sn*x + cs*z
    model.m[0] = p.k * p.cs;
    model.m[2] = -p.k * p.sn;
    model.m[5] = p.k;
    model.m[8] = p.k * p.sn;
    model.m[10] = p.k * p.cs;
    model.m[15] = 1.0f;
    model.m[12] = p.ox;
    model.m[13] = p.oy;
    model.m[14] = p.oz;

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

// Where a unit model stands: scaled to GLYPH_H tall, turned by yaw about the vertical axis (0 faces +z), with the centre
// of its bottom on (x, y, z). Lying, it is first tipped onto its side so its head points along its facing's right, and
// it rests on y.
// The unit models face their own +x, so they are turned a quarter turn more to face +z at yaw 0
constexpr float UNIT_MODEL_FRONT_YAW = -1.5707963f;

static Mat4 unitModelMatrix(const Model& m, float x, float y, float z, float yaw, bool lying, bool flip = false,
                            float height = GLYPH_H) {
    const float k = height / (m.maxY - m.minY);
    yaw += UNIT_MODEL_FRONT_YAW;
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    // R row by row: the turn by yaw, after the tip onto its side when lying: (x, y, z) -> (y, -x, z), its head along
    // its +x, or flipped (x, y, z) -> (-y, x, z), its head the other way and its other side up
    const float f = flip ? -1.0f : 1.0f;
    const float R[3][3] = {
        { lying ? 0.0f : cs, lying ? f * cs : 0.0f, sn },
        { lying ? -f : 0.0f, lying ? 0.0f : 1.0f, 0.0f },
        { lying ? 0.0f : -sn, lying ? -f * sn : 0.0f, cs }
    };
    // The model point put on the world point: the centre of its bottom standing, its middle lying
    const float c[3] = { (m.minX + m.maxX) * 0.5f, lying ? (m.minY + m.maxY) * 0.5f : m.minY, (m.minZ + m.maxZ) * 0.5f };
    const float t[3] = { x, lying ? y + k * (m.maxX - m.minX) * 0.5f : y, z };
    Mat4 out{};
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) out.m[col * 4 + r] = k * R[r][col];
        out.m[12 + r] = t[r] - k * (R[r][0] * c[0] + R[r][1] * c[1] + R[r][2] * c[2]);
    }
    out.m[15] = 1.0f;
    return out;
}

void Renderer::drawUnitModel(const Model& m, float x, float y, float z, float yaw, bool lying, float toward, bool carried,
                             float height) {
    if (!m.vao || !(m.maxY > m.minY)) return;
    // Moved a little toward the camera, so a carried unit shows in front of its carrier
    float dx = billX_ - x, dz = billZ_ - z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len > 1e-4f) {
        x += dx / len * toward;
        z += dz / len * toward;
    }
    const Mat4 mvp = mul(viewProj_, unitModelMatrix(m, x, y, z, yaw, lying, carried, height));
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT, (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

// A unit turns toward the way it walks, easing round rather than snapping. One seen for the first time faces the camera.
// One idle for UNIT_SETTLE_MS settles and keeps its facing: being pushed about by units near it no longer turns it, until
// it stops being idle or is moved more than UNIT_SETTLE_SLIP tiles from where it settled.
namespace {
constexpr unsigned long long UNIT_SETTLE_MS = 3000;
constexpr float UNIT_SETTLE_SLIP = 0.25f;
}
float Renderer::unitYaw(Entity e, float x, float z, bool idle) {
    const unsigned long long now = std::max<unsigned long long>(1, SDL_GetTicks());
    auto it = unitFacing_.find(e);
    if (it == unitFacing_.end()) {
        UnitFacing f;
        f.x = x;
        f.z = z;
        f.yaw = std::atan2(billX_ - x, billZ_ - z);
        f.idleSince = idle ? now : 0;
        unitFacing_[e] = f;
        return f.yaw;
    }
    UnitFacing& f = it->second;
    if (!idle) {
        f.idleSince = 0;
        f.settled = false;
    } else if (f.idleSince == 0) {
        f.idleSince = now;
    }
    if (f.settled) {
        const float sx = x - f.settleX, sz = z - f.settleZ;
        if (sx * sx + sz * sz <= UNIT_SETTLE_SLIP * UNIT_SETTLE_SLIP) {
            f.x = x;
            f.z = z;
            return f.yaw;   // settled: nudges don't turn it
        }
        f.settled = false;   // pushed well off its spot: it turns again, and settles anew once idle long enough
        f.idleSince = now;
    }
    if (idle && now - f.idleSince >= UNIT_SETTLE_MS) {
        f.settled = true;
        f.settleX = x;
        f.settleZ = z;
    }
    const float dx = x - f.x, dz = z - f.z;
    if (dx * dx + dz * dz > 1e-6f) {
        const float want = std::atan2(dx, dz);
        float turn = std::remainder(want - f.yaw, 6.2831853f);
        f.yaw += turn * 0.25f;
    }
    f.x = x;
    f.z = z;
    return f.yaw;
}

// A unit's model seen in a box of the UI, with its own depth: it stands in the middle, the whole of it in view, and
// turns about its vertical axis only
void Renderer::drawUnitModelPreview(int sprite, const Rect& r, float yaw) {
    drawModelPreview(unitModelNumber(sprite), r, yaw);
}

void Renderer::drawMobPreview(int kind, const Rect& r, float yaw) {
    const int idx = kind - 1;
    if (idx >= 0 && idx < MOB_KIND_COUNT && mobModels_[(size_t)idx].vao) {
        drawModelPreview(&mobModels_[(size_t)idx], r, yaw);
        return;
    }
    drawFitted(mobPicture(kind), r, WHITE);
}

const TextTex& Renderer::mobPicture(int kind) const {
    const int idx = kind - 1;
    if (kind >= 1 && kind <= MOB_KIND_COUNT && mobInfo(kind).model > 0) return pink_;
    if (idx >= 0 && idx < (int)mobSprites_.size() && mobSprites_[(size_t)idx].tex) return mobSprites_[(size_t)idx];
    return mobFallback_.tex ? mobFallback_ : pink_;
}

void Renderer::drawModelPreview(const Model* m, const Rect& r, float yaw) {
    if (!m || r.w < 1.0f || r.h < 1.0f) {
        drawFitted(pink_, r, WHITE);   // the stand-in for a missing model
        return;
    }
    const int vx = (int)std::lround(r.x), vy = (int)std::lround((float)uiH_ - r.y - r.h);
    const int vw = (int)std::lround(r.w), vh = (int)std::lround(r.h);
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // The model GLYPH_H tall with its feet at the origin; the camera in front of it on +z, far enough back that its
    // height and width both fit
    const float aspect = r.w / r.h;
    const float fov = 30.0f * 3.14159265f / 180.0f, tanHalf = std::tan(fov * 0.5f);
    const float k = GLYPH_H / (m->maxY - m->minY);
    const float halfH = GLYPH_H * 0.5f;
    const float halfW = k * std::max(m->maxX - m->minX, m->maxZ - m->minZ) * 0.5f;
    const float dist = std::max(halfH / tanHalf, halfW / (tanHalf * aspect)) * 1.12f + halfW;
    const Mat4 vp = mul(perspective(fov, aspect, 0.05f, dist * 4.0f), lookAt(0.0f, halfH, dist, 0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f));
    const Mat4 mvp = mul(vp, unitModelMatrix(*m, 0.0f, 0.0f, 0.0f, yaw, false));

    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindVertexArray(m->vao);
    for (const DrawRange& d : m->draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT, (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);

    // Back to the flat UI over the whole window
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, uiW_, uiH_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
}

// Loading screen: the title screen's moving backdrop, with a rounded progress bar across the middle, the stage that is
// running above it and how far along it is below
void Renderer::drawLoadingScreen(float progress, int winW, int winH, const std::string& label, const std::string& category,
                                 bool backdrop) {
    if (!backdrop) {
        // Plain dark behind the bar
        glViewport(0, 0, winW, winH);
        glClearColor(0.03f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    beginUi(winW, winH);
    if (backdrop) drawTitleBackground();
    const float W = (float)winW, H = (float)winH;
    const float s = H / 720.0f;
    const float p = std::clamp(progress, 0.0f, 1.0f);

    const float barW = std::min(W * 0.6f, 760.0f * s), barH = 26.0f * s;
    const Rect outer{ (W - barW) * 0.5f, (H - barH) * 0.5f, barW, barH };
    const float border = std::max(2.0f, 2.0f * s), gap = std::max(2.0f, 3.0f * s);
    // A soft glow, the pink border, the dark inside, then the green fill
    drawRoundedRect(Rect{ outer.x - 6.0f * s, outer.y - 6.0f * s, outer.w + 12.0f * s, outer.h + 12.0f * s },
                    (outer.h + 12.0f * s) * 0.5f, theme::alpha(theme::PINK, 0.12f));
    drawRoundedRect(outer, outer.h * 0.5f, theme::PINK);
    const Rect inside{ outer.x + border, outer.y + border, outer.w - 2.0f * border, outer.h - 2.0f * border };
    drawRoundedRect(inside, inside.h * 0.5f, theme::alpha(theme::NIGHT, 0.96f));
    const Rect track{ inside.x + gap, inside.y + gap, inside.w - 2.0f * gap, inside.h - 2.0f * gap };
    if (p > 0.0f) {
        const Rect fill{ track.x, track.y, std::max(track.h, track.w * p), track.h };
        drawRoundedRect(fill, fill.h * 0.5f, theme::GREEN);
        // A lighter line along its top, so the fill reads as lit
        drawRoundedRect(Rect{ fill.x + fill.h * 0.3f, fill.y + fill.h * 0.18f, std::max(0.0f, fill.w - fill.h * 0.6f), fill.h * 0.22f },
                        fill.h * 0.11f, RGBA{ 1.0f, 1.0f, 1.0f, 0.28f });
    }

    // The stage above the bar, and the share done below it
    if (!label.empty()) {
        drawUiTextCentered(label, W * 0.5f, outer.y - 14.0f * s - uiTextHeight("Ag"), theme::BONE);
    }
    const std::string pct = std::to_string((int)std::lround(p * 100.0f)) + "%";
    drawUiTextCentered(pct, W * 0.5f, outer.y + outer.h + 12.0f * s, theme::ASH);
    // What is loading now, under the share done
    if (!category.empty()) {
        drawUiTextCentered(category, W * 0.5f, outer.y + outer.h + 18.0f * s + uiTextHeight("Ag"), theme::BONE);
    }
    endUi();
    glEnable(GL_DEPTH_TEST);
}

// A rectangle with round corners of radius `radius` (at most half its shorter side), in window pixels
void Renderer::drawRoundedRect(const Rect& r, float radius, RGBA c) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float rad = std::clamp(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
    std::vector<UiVert> v;
    auto quad = [&](float x0, float y0, float x1, float y1) {
        if (x1 <= x0 || y1 <= y0) return;
        v.insert(v.end(), { { x0, y0, c }, { x1, y0, c }, { x1, y1, c }, { x0, y0, c }, { x1, y1, c }, { x0, y1, c } });
    };
    // The middle band the full height, and the two side bands between the corners
    quad(r.x + rad, r.y, r.x + r.w - rad, r.y + r.h);
    quad(r.x, r.y + rad, r.x + rad, r.y + r.h - rad);
    quad(r.x + r.w - rad, r.y + rad, r.x + r.w, r.y + r.h - rad);
    // Each corner a fan of thin wedges
    if (rad > 0.0f) {
        const int steps = std::clamp((int)(rad * 0.6f), 4, 24);
        const float cx[4] = { r.x + rad, r.x + r.w - rad, r.x + r.w - rad, r.x + rad };
        const float cy[4] = { r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad };
        const float start[4] = { 3.14159265f, 4.71238898f, 0.0f, 1.57079633f };
        for (int k = 0; k < 4; ++k) {
            for (int i = 0; i < steps; ++i) {
                const float a0 = start[k] + 1.57079633f * (float)i / (float)steps;
                const float a1 = start[k] + 1.57079633f * (float)(i + 1) / (float)steps;
                v.push_back({ cx[k], cy[k], c });
                v.push_back({ cx[k] + std::cos(a0) * rad, cy[k] + std::sin(a0) * rad, c });
                v.push_back({ cx[k] + std::cos(a1) * rad, cy[k] + std::sin(a1) * rad, c });
            }
        }
    }
    drawUiTris(v);
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

namespace {

// Smooth 2D noise, as the title backdrop's edges use: Perlin's gradient noise over a shuffled table, seeded so each
// edge has its own shape. Returns about -0.6 to 0.6.
struct EdgeNoise {
    unsigned char p[512];
    explicit EdgeNoise(unsigned seed) {
        unsigned char perm[256];
        for (int i = 0; i < 256; ++i) perm[i] = (unsigned char)i;
        unsigned st = seed;
        auto rnd = [&]() { st = st * 1664525u + 1013904223u; return st / 4294967296.0; };
        for (int i = 255; i > 0; --i) {
            const int j = (int)(rnd() * (i + 1));
            std::swap(perm[i], perm[j]);
        }
        for (int i = 0; i < 512; ++i) p[i] = perm[i & 255];
    }
    float operator()(float x, float y) const {
        const int X = (int)std::floor(x) & 255, Y = (int)std::floor(y) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
        auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
        auto grad = [](int h, float gx, float gy) { return ((h & 1) ? -gx : gx) + ((h & 2) ? -gy : gy); };
        const float u = fade(x), v = fade(y);
        const int A = p[X] + Y, B = p[X + 1] + Y;
        return lerp(lerp(grad(p[A], x, y), grad(p[B], x - 1.0f, y), u),
                    lerp(grad(p[A + 1], x, y - 1.0f), grad(p[B + 1], x - 1.0f, y - 1.0f), u), v) * 0.62f;
    }
};

RGBA mixColor(RGBA a, RGBA b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return RGBA{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}
RGBA rgb255(int r, int g, int b, float a) { return RGBA{ r / 255.0f, g / 255.0f, b / 255.0f, a }; }

// A colour from stops at 0..1 (stops in order)
RGBA stopColor(const std::vector<std::pair<float, RGBA>>& stops, float t) {
    if (t <= stops.front().first) return stops.front().second;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].first) {
            const float span = stops[i].first - stops[i - 1].first;
            return mixColor(stops[i - 1].second, stops[i].second, span > 0.0f ? (t - stops[i - 1].first) / span : 1.0f);
        }
    }
    return stops.back().second;
}

// One side of one band: where its edge sits, how much it wanders, and how fast
struct BandEdge {
    int layer;
    int side;   // -1 the left side, 1 the right
    float base, a1, a2, f1, f2, m1, m2;
    EdgeNoise noise;
};

} // namespace

void Renderer::drawUiTris(const std::vector<UiVert>& in) {
    if (in.empty()) return;
    std::vector<Vertex> v;
    v.reserve(in.size());
    for (const UiVert& u : in) v.push_back(Vertex{ { u.x, u.y, 0.0f }, u.c });
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
}

void Renderer::setTitleFont(TTF_Font* font) {
    for (auto& entry : titleWords_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    titleWords_.clear();
    titleFont_ = font;
}

const TextTex& Renderer::titleWord(const std::string& text) {
    auto it = titleWords_.find(text);
    if (it != titleWords_.end()) return it->second;
    TextTex t;
    if (titleFont_ && !text.empty()) {
        t = makeGlyphTexture(titleFont_, text.c_str(), SDL_Color{ 255, 255, 255, 255 });
        if (t.tex) {
            // Mipmaps, so the large picture shrinks smoothly to whatever size the name is drawn at
            glBindTexture(GL_TEXTURE_2D, t.tex);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
    }
    return titleWords_.emplace(text, t).first->second;
}

void Renderer::noteHover(const Rect& r) {
    if (hoverAny_) return;   // the first one drawn under the pointer counts
    hoverAny_ = true;
    hoverNow_ = r;
}

bool Renderer::hoverFrameEnd() {
    auto same = [](const Rect& a, const Rect& b) {
        return std::fabs(a.x - b.x) < 1.0f && std::fabs(a.y - b.y) < 1.0f && std::fabs(a.w - b.w) < 1.0f && std::fabs(a.h - b.h) < 1.0f;
    };
    const bool ping = hoverAny_ && (!hoverHad_ || !same(hoverNow_, hoverLast_));
    hoverHad_ = hoverAny_;
    hoverLast_ = hoverNow_;
    return ping;
}

void Renderer::drawRectGradientH(const Rect& r, RGBA left, RGBA right) {
    const float x0 = r.x, x1 = r.x + r.w, y0 = r.y, y1 = r.y + r.h;
    drawUiTris({ { x0, y0, left }, { x1, y0, right }, { x1, y1, right }, { x0, y0, left }, { x1, y1, right }, { x0, y1, left } });
}

void Renderer::drawTitleBackground() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float W = (float)uiW_, H = (float)uiH_;
    if (W <= 0.0f || H <= 0.0f) return;
    const float s = H / 720.0f;
    const float t = (float)(SDL_GetTicks() % 100000000ULL);   // milliseconds

    // The mouse sways the bands a little, eased toward where it is
    {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        const float target = std::clamp((mx / W - 0.5f) * 2.0f, -1.0f, 1.0f);
        const unsigned long long now = SDL_GetTicks();
        const float frames = titleMouseAt_ == 0 ? 0.0f : std::min(10.0f, (float)(now - titleMouseAt_) / 16.7f);
        titleMouse_ += (target - titleMouse_) * (1.0f - std::pow(1.0f - 0.07f, frames));
        titleMouseAt_ = now;
    }

    // A grid of corners, each coloured by fn, as triangles
    auto mesh = [&](int cols, int rows, auto fn) {
        std::vector<UiVert> v;
        v.reserve((size_t)cols * rows * 6);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const float x0 = W * c / cols, x1 = W * (c + 1) / cols;
                const float y0 = H * r / rows, y1 = H * (r + 1) / rows;
                const UiVert a{ x0, y0, fn(x0, y0) }, b{ x1, y0, fn(x1, y0) }, cc{ x1, y1, fn(x1, y1) }, d{ x0, y1, fn(x0, y1) };
                v.insert(v.end(), { a, b, cc, a, cc, d });
            }
        }
        drawUiTris(v);
    };

    // The night: a deep purple glow high in the middle, fading to black at the edges
    const std::vector<std::pair<float, RGBA>> sky{ { 0.0f, rgb255(16, 11, 28, 1.0f) }, { 0.58f, rgb255(7, 6, 13, 1.0f) },
                                                   { 1.0f, rgb255(3, 3, 6, 1.0f) } };
    const RGBA purple = rgb255(109, 40, 217, 1.0f);
    mesh(32, 18, [&](float x, float y) {
        const float r1 = std::hypot((x - 0.5f * W) / (1.4f * W), (y - 0.38f * H) / (1.1f * H));
        RGBA c = stopColor(sky, r1);
        const float r2 = std::hypot((x - 0.5f * W) / (0.8f * W), (y - 0.26f * H) / (0.55f * H));
        const float a = 0.22f * std::clamp(1.0f - r2 / 0.65f, 0.0f, 1.0f);
        c = mixColor(c, purple, a);
        c.a = 1.0f;
        return c;
    });

    // The bands: green further in, pink over it nearer each side. Their edges wander, and drift up the screen.
    static BandEdge edges[4] = {
        { 0, -1, 0.335f, 0.082f, 0.020f, 0.0019f, 0.0079f, 0.000130f, 0.00037f, EdgeNoise(10771) },
        { 0, 1, 0.355f, 0.074f, 0.024f, 0.0023f, 0.0068f, 0.000094f, 0.00051f, EdgeNoise(55219) },
        { 1, -1, 0.185f, 0.058f, 0.015f, 0.0027f, 0.0096f, 0.000171f, 0.00029f, EdgeNoise(90413) },
        { 1, 1, 0.168f, 0.065f, 0.013f, 0.0016f, 0.0110f, 0.000212f, 0.00044f, EdgeNoise(31867) },
    };
    struct Layer {
        float speed, mouse;
        std::vector<std::pair<float, RGBA>> fill;
        RGBA stroke;
        float lw;
        RGBA glow;
        float gb;
    };
    const Layer layers[2] = {
        { 0.050f, 10.0f, { { 0.0f, rgb255(124, 184, 0, 0.92f) }, { 0.55f, rgb255(70, 112, 0, 0.9f) }, { 1.0f, rgb255(16, 26, 2, 0.92f) } },
          rgb255(159, 230, 0, 0.95f), 2.0f, rgb255(159, 230, 0, 0.55f), 16.0f },
        { 0.019f, 28.0f, { { 0.0f, rgb255(122, 11, 71, 0.96f) }, { 0.6f, rgb255(58, 5, 33, 0.96f) }, { 1.0f, rgb255(14, 2, 9, 0.97f) } },
          rgb255(224, 33, 138, 0.9f), 2.5f, rgb255(224, 33, 138, 0.5f), 14.0f },
    };
    const float S = std::min(W, 1500.0f);
    const float step = std::max(4.0f, 9.0f * s);
    for (int li = 0; li < 2; ++li) {
        const Layer& L = layers[li];
        const float scroll = -t * L.speed;
        const float push = titleMouse_ * L.mouse * s;
        for (const BandEdge& e : edges) {
            if (e.layer != li) continue;
            std::vector<std::pair<float, float>> pts;   // x, y
            for (float y = -step * 2.0f; y <= H + step * 2.0f; y += step) {
                const float yy = y / s * 1.25f + scroll;   // in the units the shapes were drawn for (a 900-high screen)
                const float n1 = e.noise(yy * e.f1, t * e.m1);
                const float n2 = e.noise(yy * e.f2 + 400.0f, t * e.m2 + 90.0f);
                const float v = e.base * W + n1 * e.a1 * S + n2 * e.a2 * S;
                pts.push_back({ e.side < 0 ? v + push : W - v + push, y });
            }
            const float off = e.side < 0 ? -2.0f : W + 2.0f;
            std::vector<UiVert> fill;
            for (size_t i = 1; i < pts.size(); ++i) {
                const auto [xa, ya] = pts[i - 1];
                const auto [xb, yb] = pts[i];
                const RGBA ca = stopColor(L.fill, ya / H), cb = stopColor(L.fill, yb / H);
                fill.insert(fill.end(), { { xa, ya, ca }, { xb, yb, cb }, { off, yb, cb }, { xa, ya, ca }, { off, yb, cb }, { off, ya, ca } });
            }
            drawUiTris(fill);
            // The edge line and its glow: a soft strip either side of it, then the line
            std::vector<UiVert> line;
            auto strip = [&](float width, RGBA mid, bool soft) {
                const float hw = width * 0.5f * s;
                const RGBA edgeC = soft ? theme::alpha(mid, 0.0f) : mid;
                for (size_t i = 1; i < pts.size(); ++i) {
                    const auto [xa, ya] = pts[i - 1];
                    const auto [xb, yb] = pts[i];
                    line.insert(line.end(), { { xa - hw, ya, edgeC }, { xa, ya, mid }, { xb, yb, mid },
                                              { xa - hw, ya, edgeC }, { xb, yb, mid }, { xb - hw, yb, edgeC },
                                              { xa, ya, mid }, { xa + hw, ya, edgeC }, { xb + hw, yb, edgeC },
                                              { xa, ya, mid }, { xb + hw, yb, edgeC }, { xb, yb, mid } });
                }
            };
            strip(L.lw + L.gb * 1.6f, theme::alpha(L.glow, L.glow.a * 0.45f), true);
            strip(L.lw + L.gb * 0.5f, theme::alpha(L.glow, L.glow.a * 0.8f), true);
            strip(L.lw, L.stroke, false);
            drawUiTris(line);
        }
    }

    // The veil (darker at the top and much darker at the bottom) and the vignette, as one layer of black
    const std::vector<std::pair<float, RGBA>> veil{ { 0.0f, RGBA{ 0, 0, 0, 0.6f } }, { 0.38f, RGBA{ 0, 0, 0, 0.12f } },
                                                    { 1.0f, RGBA{ 0, 0, 0, 0.85f } } };
    mesh(32, 18, [&](float x, float y) {
        const float a1 = stopColor(veil, y / H).a;
        const float r = std::hypot((x - 0.5f * W) / (1.18f * W), (y - 0.48f * H) / (0.78f * H));
        const float a2 = r < 0.34f ? 0.0f : 0.88f * std::min(1.0f, (r - 0.34f) / 0.66f);
        return RGBA{ 0.012f, 0.012f, 0.024f, 1.0f - (1.0f - a1) * (1.0f - a2) };
    });

    // Scan lines: a faint dark line every third pixel
    std::vector<UiVert> lines;
    lines.reserve((size_t)(H / 3.0f + 2.0f) * 6);
    const RGBA scan{ 0.0f, 0.0f, 0.0f, 0.17f };
    for (float y = 0.0f; y < H; y += 3.0f) {
        lines.insert(lines.end(), { { 0, y, scan }, { W, y, scan }, { W, y + 1.0f, scan }, { 0, y, scan }, { W, y + 1.0f, scan }, { 0, y + 1.0f, scan } });
    }
    drawUiTris(lines);
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

float Renderer::profileCardHeight(int H) {
    const float s = H / 720.0f;
    const float line = listTextHeight("Ag");
    const float statRow = line + 3.0f * s + 8.0f * s + 9.0f * s;   // label line, gap, bar, gap: as statRow draws it
    // Health, Doom (while Unconscious), Hunger, Rested, Comfort and Carry
    return 10.0f * s + uiTextHeight("Ag") + 6.0f * s + 2.0f * (line + 6.0f * s) + 6.0f * statRow + 4.0f * s;
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
    drawRect(Rect{ x, y, w, h }, RGBA{ 0.05f, 0.035f, 0.075f, 0.96f });
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
// Drops every text picture of the set in use
void Renderer::clearActiveText() {
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
}

void Renderer::replaceActiveFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow) {
    clearActiveText();
    hudFont_ = hud;
    uiFont_ = ui;
    listFont_ = list;
    statsTitle_.font = statsTitle;
    statsRow_.font = statsRow;
}

void Renderer::swapTextSet() {
    std::swap(hudFont_, parked_.hud);
    std::swap(hudLines_, parked_.hudLines);
    std::swap(uiFont_, parked_.ui);
    std::swap(uiText_, parked_.uiText);
    std::swap(listFont_, parked_.list);
    std::swap(listText_, parked_.listText);
    std::swap(statsTitle_, parked_.statsTitle);
    std::swap(statsRow_, parked_.statsRow);
}

void Renderer::setMenuText(bool on) {
    if (on == menuText_) return;
    // Without menu fonts there is nothing to swap to
    if (on && !parked_.ui) return;
    swapTextSet();
    menuText_ = on;
}

// Swaps every text font at once, for the Font size setting. Textures made with the old fonts are dropped.
void Renderer::setFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow) {
    if (menuText_) swapTextSet();
    replaceActiveFonts(hud, ui, list, statsTitle, statsRow);
    if (menuText_) swapTextSet();
}

void Renderer::setMenuFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow) {
    if (!menuText_) swapTextSet();
    replaceActiveFonts(hud, ui, list, statsTitle, statsRow);
    if (!menuText_) swapTextSet();
}

void Renderer::drawBigText(const std::string& text, float cx, float cy, float h, RGBA tint) {
    const TextTex& t = titleWord(text);
    if (!t.tex) {
        drawUiTextLarge(text, cx, cy, h / std::max(1.0f, uiTextHeight("Ag")), tint);
        return;
    }
    const float w = t.aspect * h, x = cx - w * 0.5f, y = cy - h * 0.5f;
    const float v[4 * 5] = { x, y + h, 0.0f, 0.0f, 1.0f, x + w, y + h, 0.0f, 1.0f, 1.0f,
                             x + w, y, 0.0f, 1.0f, 0.0f, x, y, 0.0f, 0.0f, 0.0f };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(t.tex, v, tint);
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
    // Held down, it fills with hot pink from the left
    const float s = uiH_ / 720.0f;
    drawRectGradientH(r, theme::alpha(theme::PINK_INK, 0.85f), theme::alpha(theme::PINK_DARK, 0.95f));
    if (fill > 0.0f) drawRect(Rect{ r.x, r.y, r.w * fill, r.h }, theme::alpha(theme::PINK, 0.9f));
    drawRect(Rect{ r.x, r.y, 3.0f * s, r.h }, theme::PINK);
    drawUiTextInBox(label, r, theme::BONE);
}

void Renderer::drawListButton(const Rect& r, const std::string& label, bool hovered) {
    // A pink button, for a row's own action (Delete)
    const float s = uiH_ / 720.0f;
    if (hovered) noteHover(r);
    drawRectGradientH(r, hovered ? theme::alpha(theme::PINK_DEEP, 0.95f) : theme::alpha(theme::PINK_INK, 0.9f),
                      hovered ? theme::alpha(theme::PINK, 0.95f) : theme::alpha(theme::PINK_DARK, 0.95f));
    drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, hovered ? theme::BONE : theme::PINK);
    const float tw = listLine(label).w;
    const float th = listTextHeight(label);
    drawListText(label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, theme::BONE);
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
    // A dark band with an edge down its left side; under the pointer the band turns green and the edge lights up
    const float s = uiH_ / 720.0f;
    const bool lit = enabled && hovered;
    if (lit) noteHover(r);
    if (!enabled) {
        drawRect(r, theme::alpha(theme::NIGHT, 0.6f));
    } else if (lit) {
        drawRectGradientH(r, rgb255(20, 30, 0, 0.85f), rgb255(45, 66, 0, 0.95f));
    } else {
        drawRectGradientH(r, rgb255(18, 14, 26, 0.85f), rgb255(10, 8, 16, 0.95f));
    }
    drawRect(Rect{ r.x, r.y, 3.0f * s, r.h }, lit ? theme::GREEN : theme::alpha(theme::ASH, enabled ? 0.45f : 0.15f));
    const RGBA fg = !enabled ? theme::alpha(theme::ASH, 0.5f) : lit ? theme::BONE : rgb255(204, 208, 218, 1.0f);
    // A small inset keeps the text off the button's edge
    drawUiTextInBox(label, Rect{ r.x + 6.0f * s, r.y + 2.0f * s, r.w - 10.0f * s, r.h - 4.0f * s }, fg);
}

// The sprite for a unit, or the stand-in if its image is missing
const TextTex& Renderer::unitGlyph(World& w, Entity e) const {
    // A mob's picture is its kind's (mob_<kind>.png), with mob_0.png for a missing one. A kind drawn by a model has the
    // pink square for its portrait, as a person has.
    if (const Mob* m = w.get<Mob>(e)) {
        const int idx = m->kind - 1;
        // A kind meant to be drawn by a model has no picture of its own: the pink square, standing in the world as a
        // billboard when the model is missing, and its portrait always
        if (mobInfo(m->kind).model > 0) return pink_;
        if (idx >= 0 && idx < (int)mobSprites_.size() && mobSprites_[(size_t)idx].tex) return mobSprites_[(size_t)idx];
        return mobFallback_.tex ? mobFallback_ : pink_;
    }
    return pink_;   // a person is drawn by a model; the pink square stands in for a missing one
}

// The profile picture of model n (profile_<nn>.png), or the pink square when it is missing
const TextTex& Renderer::unitPicture(int n) const {
    const int idx = ((n - 1) % UNIT_SPRITE_COUNT + UNIT_SPRITE_COUNT) % UNIT_SPRITE_COUNT;
    return profilePics_[(size_t)idx].tex ? profilePics_[(size_t)idx] : pink_;
}

const TextTex& Renderer::unitPortrait(World& w, Entity e) const {
    if (w.has<Mob>(e)) return unitGlyph(w, e);
    const UnitSprite* us = w.get<UnitSprite>(e);
    return unitPicture(us ? us->id : 1);
}

const Model* Renderer::unitModelNumber(int n) const {
    const int idx = ((n - 1) % UNIT_SPRITE_COUNT + UNIT_SPRITE_COUNT) % UNIT_SPRITE_COUNT;
    return unitModelFor_[(size_t)idx];
}

RGBA Renderer::unitFactionTint(World& w, Entity e) const {
    const Faction* f = w.get<Faction>(e);
    if (!f || f->id == FACTION_PLAYER || w.has<Mob>(e)) return RGBA{ 0.0f, 0.0f, 0.0f, 0.0f };
    const FactionColor c = factionColor(f->id);
    return RGBA{ c.r, c.g, c.b, FACTION_TINT };
}

float Renderer::unitHeight(World& w, Entity e) const {
    if (const Mob* mb = w.get<Mob>(e)) {
        return unitModel(w, e) ? mobInfo(mb->kind).modelHeight : GLYPH_H * mobSpriteScale(mb->kind);
    }
    return GLYPH_H;
}

const Model* Renderer::unitModel(World& w, Entity e) const {
    if (const Mob* mb = w.get<Mob>(e)) {
        const int idx = mb->kind - 1;
        if (idx >= 0 && idx < MOB_KIND_COUNT && mobModels_[(size_t)idx].vao) return &mobModels_[(size_t)idx];
        return nullptr;
    }
    const UnitSprite* us = w.get<UnitSprite>(e);
    return unitModelNumber(us ? us->id : 1);
}

float Renderer::unitAspect(World& w, Entity e) const {
    if (const Model* m = unitModel(w, e)) {
        return std::max(m->maxX - m->minX, m->maxZ - m->minZ) / (m->maxY - m->minY);
    }
    return unitGlyph(w, e).aspect;
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
        const float hover = unitHover(w, u);   // a flying mob is found where it is drawn, up off the ground
        if (!project(tr->x, tr->y + hover, tr->z, bottomX, bottomY)) continue;
        const float tall = unitHeight(w, u);
        if (!project(tr->x, tr->y + hover + tall, tr->z, topX, topY)) continue;
        if (!project(tr->x, tr->y + hover + tall * 0.5f, tr->z, midX, midY)) continue;

        float heightPx = std::fabs(bottomY - topY);
        const float aspect = unitAspect(w, u);
        float halfW = heightPx * 0.5f * aspect;
        float top = std::min(bottomY, topY), bottom = std::max(bottomY, topY);
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u)) {
            // Lying on its side: as wide as it is tall standing, and as high as it is wide; carried ones higher up
            float liftX = 0.0f, liftY = 0.0f;
            const float lift = w.has<Carried>(u) ? (unitModel(w, u) ? CARRIED_MODEL_LIFT : CARRIED_LIFT) : 0.0f;
            if (!project(tr->x, tr->y + lift, tr->z, liftX, liftY)) continue;
            const float lyingH = heightPx * aspect;
            halfW = heightPx * 0.5f;
            midX = liftX;
            bottom = liftY;
            top = liftY - lyingH;
        }
        if (mx >= midX - halfW && mx <= midX + halfW && my >= top && my <= bottom) {
            out.push_back(u);
        }
    }
    return out;
}

// The props under a pixel, nearest first. A prop with a model is hit only on the model's own triangles, placed exactly
// as drawModel places them (placeModel), so the mouse finds a prop by its shape and never where it is not drawn. A
// prop whose model did not load is found by its pink stand-in, a square facing the camera.
std::vector<Entity> Renderer::propsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH) {
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    // The ray through the pixel, from the near plane toward the far one
    float o[3] = {}, d[3] = {};
    {
        float inv[16];
        if (!invert4x4(vp.m, inv)) return {};
        const float ndcX = 2.0f * mx / (float)winW - 1.0f, ndcY = 1.0f - 2.0f * my / (float)winH;
        auto unproject = [&](float ndcZ, float out3[3]) {
            const float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
            float r[4];
            for (int i = 0; i < 4; ++i) r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1] + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
            out3[0] = r[0] / r[3];
            out3[1] = r[1] / r[3];
            out3[2] = r[2] / r[3];
        };
        float f[3];
        unproject(-1.0f, o);
        unproject(1.0f, f);
        for (int i = 0; i < 3; ++i) d[i] = f[i] - o[i];
    }
    std::vector<std::pair<float, Entity>> hits;
    for (Entity e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const int kind = w.get<Prop>(e)->kind;
        if (kind < 0 || kind >= PROP_KIND_COUNT) continue;
        const Model& m = propModel(kind);
        const float baseY = propBaseY(w, e);
        if (m.vao) {
            const float t = rayHitModel(m, placeModel(m, tr->x, baseY, tr->z, propInfo(kind).size,
                                                      w.get<Prop>(e)->yaw), o, d);
            if (t >= 0.0f) hits.push_back({ t, e });
            continue;
        }
        // The pink stand-in: the box that holds the square from any side, on the screen
        const float hx = 0.5f * missingPropSide(kind), hz = hx, h = missingPropSide(kind);
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        bool seen = true;
        for (int c = 0; c < 8 && seen; ++c) {
            float sx = 0.0f, sy = 0.0f;
            seen = projectPoint(vp, tr->x + ((c & 1) ? hx : -hx), baseY + ((c & 2) ? h : 0.0f),
                                tr->z + ((c & 4) ? hz : -hz), winW, winH, sx, sy);
            if (c == 0) { x0 = x1 = sx; y0 = y1 = sy; }
            x0 = std::min(x0, sx); x1 = std::max(x1, sx);
            y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        if (seen && mx >= x0 && mx <= x1 && my >= y0 && my <= y1) {
            const float dx = tr->x - o[0], dy = baseY - o[1], dz = tr->z - o[2];
            const float len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            hits.push_back({ len2 > 0.0f ? (dx * d[0] + dy * d[1] + dz * d[2]) / len2 : 0.0f, e });
        }
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Entity> out;
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

// The ground items whose sprite covers the pixel, the same test groundItemAtPixel uses for sprites
std::vector<Entity> Renderer::groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                                 int winW, int winH) {
    std::vector<Entity> out;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        ItemOnScreen os;
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) continue;
        if (!(mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom)) continue;
        if (os.bottom - os.top < 1.0f) continue;
        if (itemPixelSolid(gi->item, (mx - (os.cx - os.halfW)) / (2.0f * os.halfW), (my - os.top) / (os.bottom - os.top))) out.push_back(e);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Renderer::drawHud(const std::vector<HudEntry>& lines, int winW, int winH) {
    if (!hudFont_) return;

    // Rebuild only the lines whose text changed. The text is made white and tinted to its colour when drawn.
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == hudLines_.size()) hudLines_.emplace_back();
        HudLine& hl = hudLines_[i];
        const std::string& text = lines[i].text;
        if (hl.tex.tex && hl.text == text) continue;
        if (hl.tex.tex) glDeleteTextures(1, &hl.tex.tex);

        int tw = 0, th = 0;
        TTF_GetStringSize(hudFont_, text.c_str(), text.size(), &tw, &th);
        hl.text = text;
        hl.tex = makeGlyphTexture(hudFont_, text.c_str(), SDL_Color{ 255, 255, 255, 255 });
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
    y = hudTop_;   // the same top as the panels
    for (size_t i = 0; i < hudLines_.size(); ++i) {
        const HudLine& hl = hudLines_[i];
        float x0 = margin + pad, x1 = x0 + hl.w;
        float y0 = y + pad, y1 = y0 + hl.h;
        float v[4 * 5] = {
            x0, y1, 0.0f, 0.0f, 1.0f,
            x1, y1, 0.0f, 1.0f, 1.0f,
            x1, y0, 0.0f, 1.0f, 0.0f,
            x0, y0, 0.0f, 0.0f, 0.0f
        };
        drawQuadVerts(hl.tex.tex, v, lines[i].color);
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
                         const std::vector<HudEntry>& hud, int winW, int winH) {
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

    for (Entity e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const int kind = w.get<Prop>(e)->kind;
        if (kind < 0 || kind >= PROP_KIND_COUNT) continue;
        const float baseY = propBaseY(w, e);
        if (propModel(kind).vao) {
            drawModel(propModel(kind), tr->x, baseY, tr->z, propInfo(kind).size, w.get<Prop>(e)->yaw);
        } else {
            // No model, and no model_0.glb: a pink square as tall as the prop's size, standing on its spot and turned to the camera
            drawGlyph(white_, tr->x, baseY, tr->z, missingPropSide(kind), MISSING_PROP_PINK);
        }
    }

    // Standing units upright. An Unconscious unit lies on its side, half gray (a corpse all gray), on the ground or, if
    // it is being carried, over its carrier's shoulders and in front of it.
    for (Entity e : w.view<Unit, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // A unit with a model is drawn by it, turned the way it walks; a mob, or a unit whose model is missing, by its sprite
        if (const Model* m = unitModel(w, e)) {
            const float yaw = unitYaw(e, tr->x, tr->z, unitIdle(w, e));
            const bool lying = w.has<Unconscious>(e) || w.has<Carried>(e) || w.has<PlayingDead>(e);
            const bool carried = w.has<Carried>(e);
            if (lying) setSaturation(unitSaturation(w, e));
            const RGBA tint = unitFactionTint(w, e);
            glUniform4f(uFaction_, tint.r, tint.g, tint.b, tint.a);
            // A carried model lies over its carrier's shoulders, higher than a sprite does since the model has depth
            drawUnitModel(*m, tr->x, tr->y + (carried ? CARRIED_MODEL_LIFT : unitHover(w, e)), tr->z, yaw, lying,
                          carried ? 0.15f : 0.0f, carried, unitHeight(w, e));
            glUniform4f(uFaction_, 0.0f, 0.0f, 0.0f, 0.0f);
            if (lying) setSaturation(1.0f);
            continue;
        }
        const TextTex& g = unitGlyph(w, e);
        const RGBA tint = WHITE;   // every unit in its own colours: a hostile one is not tinted
        if (w.has<Unconscious>(e) || w.has<Carried>(e) || w.has<PlayingDead>(e)) {
            setSaturation(unitSaturation(w, e));
            const bool carried = w.has<Carried>(e);
            drawLyingGlyph(g, tr->x, tr->y + (carried ? CARRIED_LIFT : 0.0f), tr->z, unitHeight(w, e), tint,
                           carried ? 0.15f : 0.0f);
            setSaturation(1.0f);
            continue;
        }
        drawGlyph(g, tr->x, tr->y + unitHover(w, e), tr->z, unitHeight(w, e), tint);
    }

    drawGroundItems(w, t, cam);
    drawBuildPreview(t);

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
const RGBA TIP_HEADING{ 0.624f, 0.902f, 0.0f, 1.0f };
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

// The world map window. Every shape on the map is drawn in window pixels with drawUiTris.
void Renderer::drawWorldMap(const WorldMapLayout& L, const std::vector<MapDot>& dots, float cx, float cz, float span,
                            const WorldMapMarks& marks) {
    pollOverview();
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA black{ 0.0f, 0.0f, 0.0f, 1.0f };
    const Rect& w = L.window;
    const Rect& r = L.map;

    // The window: a pink edge, a dark body, and the title bar with its X
    drawRect(Rect{ w.x - 1.5f * s, w.y - 1.5f * s, w.w + 3.0f * s, w.h + 3.0f * s }, theme::alpha(theme::PINK, 0.95f));
    drawRect(w, RGBA{ 0.04f, 0.03f, 0.07f, 0.97f });
    drawTitleBar(L.bar, ICON_NONE, lang("UILABEL_WORLD_MAP"));
    drawCloseButton(L.close);

    drawRect(r, RGBA{ 0.10f, 0.20f, 0.30f, 1.0f });   // sea, where the view is past the edge of the level
    if (span <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return;

    // The view in tiles. Its left edge is the largest x and its top edge the largest z.
    const float across = mapSpanAcross(r, span);
    const float xHi = cx + across * 0.5f, xLo = cx - across * 0.5f;
    const float zHi = cz + span * 0.5f, zLo = cz - span * 0.5f;
    auto sx = [&](float x) { return r.x + (xHi - x) / across * r.w; };
    auto sy = [&](float z) { return r.y + (zHi - z) / span * r.h; };

    if (overviewTex_ && overviewSize_ > 0.0f) {
        const float size = overviewSize_, ext = overviewExtent_;
        auto u = [&](float x) { return (size - x) / ext; };
        auto v = [&](float z) { return (size - z) / ext; };
        // The part of the view inside the level
        const float ax = std::min(xHi, size), bx = std::max(xLo, 0.0f);
        const float az = std::min(zHi, size), bz = std::max(zLo, 0.0f);
        if (ax > bx && az > bz) drawUiQuad(overviewTex_, sx(ax), sy(az), sx(bx), sy(bz), u(ax), v(az), u(bx), v(bz), white);
    }

    // A thick line from one point to another, its ends pushed out by half its width so corners meet
    std::vector<UiVert> tris;
    auto line = [&](float x0, float y0, float x1, float y1, float width, RGBA c) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0f) return;
        const float ux = dx / len, uy = dy / len, h = width * 0.5f;
        const float nx = -uy * h, ny = ux * h;
        x0 -= ux * h; y0 -= uy * h; x1 += ux * h; y1 += uy * h;
        tris.insert(tris.end(), { { x0 + nx, y0 + ny, c }, { x1 + nx, y1 + ny, c }, { x1 - nx, y1 - ny, c },
                                  { x0 + nx, y0 + ny, c }, { x1 - nx, y1 - ny, c }, { x0 - nx, y0 - ny, c } });
    };
    // A ring of the given radius and width
    auto ring = [&](float x, float y, float radius, float width, RGBA c) {
        const int segs = 32;
        const float rIn = radius - width * 0.5f, rOut = radius + width * 0.5f;
        for (int i = 0; i < segs; ++i) {
            const float a0 = 6.2831853f * i / segs, a1 = 6.2831853f * (i + 1) / segs;
            const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            tris.insert(tris.end(), { { x + c0 * rIn, y + s0 * rIn, c }, { x + c0 * rOut, y + s0 * rOut, c },
                                      { x + c1 * rOut, y + s1 * rOut, c }, { x + c0 * rIn, y + s0 * rIn, c },
                                      { x + c1 * rOut, y + s1 * rOut, c }, { x + c1 * rIn, y + s1 * rIn, c } });
        }
    };
    auto square = [&](float x, float y, float side, RGBA c) {
        const float h = side * 0.5f;
        tris.insert(tris.end(), { { x - h, y - h, c }, { x + h, y - h, c }, { x + h, y + h, c },
                                  { x - h, y - h, c }, { x + h, y + h, c }, { x - h, y + h, c } });
    };
    auto inMap = [&](float px, float py) { return r.contains(px, py); };

    // Every prop, of any kind: a purple square with a black edge, under the units
    const RGBA purple{ 0.62f, 0.25f, 0.88f, 1.0f };
    const float propSide = std::max(4.0f, 6.0f * s);
    for (const MapDot& dot : dots) {
        if (dot.kind != MapDotKind::Prop) continue;
        const float px = sx(dot.at.x), py = sy(dot.at.z);
        if (!inMap(px, py)) continue;
        square(px, py, propSide + 2.0f * std::max(1.0f, s), black);
        square(px, py, propSide, purple);
    }
    drawUiTris(tris);
    tris.clear();

    // The units: a green dot for the player's, the white dot tinted red or gray, with its black ring, for the rest
    const float d = std::max(6.0f, 9.0f * s);
    for (const MapDot& dot : dots) {
        if (dot.kind == MapDotKind::Prop) continue;
        const float px = sx(dot.at.x), py = sy(dot.at.z);
        if (!inMap(px, py)) continue;
        if (dot.kind == MapDotKind::Player) {
            drawDot(px, py, d);
            continue;
        }
        if (!dotWhiteTex_) dotWhiteTex_ = MakeDotTexture(true);
        const RGBA tint = dot.kind == MapDotKind::Hostile ? RGBA{ 1.0f, 0.25f, 0.25f, 1.0f } : RGBA{ 0.6f, 0.6f, 0.6f, 1.0f };
        const float h = d * 0.5f;
        drawUiQuad(dotWhiteTex_, px - h, py - h, px + h, py + h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
    }

    const float edge = std::max(1.0f, 1.0f * s);   // the black edge around each line
    // The green X where the last move order went, fading out
    if (marks.moveAlpha > 0.0f) {
        const float px = sx(marks.moveX), py = sy(marks.moveZ);
        if (inMap(px, py)) {
            const float h = 6.0f * s, lw = std::max(2.0f, 2.5f * s);
            const RGBA k{ 0.0f, 0.0f, 0.0f, marks.moveAlpha };
            const RGBA g{ 0.30f, 1.0f, 0.30f, marks.moveAlpha };
            line(px - h, py - h, px + h, py + h, lw + 2.0f * edge, k);
            line(px - h, py + h, px + h, py - h, lw + 2.0f * edge, k);
            line(px - h, py - h, px + h, py + h, lw, g);
            line(px - h, py + h, px + h, py - h, lw, g);
        }
    }

    // The camera's pivot: a hollow ring round the point the camera turns about, and a hollow triangle over it pointing
    // the way the camera looks. On the map the larger x is to the left and the larger z to the top, so the camera's
    // ground direction (-sin yaw, -cos yaw) points along (sin yaw, cos yaw) in window pixels.
    {
        const float px = sx(marks.camX), py = sy(marks.camZ);
        if (inMap(px, py)) {
            const float rad = 7.0f * s, lw = std::max(1.5f, 1.5f * s);
            const float fx = std::sin(marks.camYaw), fy = std::cos(marks.camYaw);   // ahead
            const float qx = -fy, qy = fx;                                            // to one side
            auto at = [&](float ahead, float side, float& ox, float& oy) {
                ox = px + fx * ahead * rad + qx * side * rad;
                oy = py + fy * ahead * rad + qy * side * rad;
            };
            float ax, ay, bx, by, cx2, cy2;
            at(2.1f, 0.0f, ax, ay);      // the tip
            at(-0.45f, 1.8f, bx, by);    // the base corners
            at(-0.45f, -1.8f, cx2, cy2);
            const RGBA yellow{ 1.0f, 0.86f, 0.18f, 1.0f };
            for (int pass = 0; pass < 2; ++pass) {
                const RGBA c = pass == 0 ? black : yellow;
                const float wdt = pass == 0 ? lw + 2.0f * edge : lw;
                line(ax, ay, bx, by, wdt, c);
                line(bx, by, cx2, cy2, wdt, c);
                line(cx2, cy2, ax, ay, wdt, c);
                ring(px, py, rad, wdt, c);
            }
        }
    }
    drawUiTris(tris);
}

// The X at the right end of a window's title bar, framed like the [-] and [+] buttons
void Renderer::drawCloseButton(const Rect& r) {
    const float s = uiH_ / 720.0f;
    const bool hot = r.contains(mouseX_, mouseY_);
    if (hot) noteHover(r);
    drawRect(r, theme::alpha(theme::PINK, 0.9f));
    drawRect(Rect{ r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f },
             hot ? theme::alpha(theme::PINK_DEEP, 0.95f) : theme::alpha(theme::PINK_INK, 0.95f));
    const float m = r.w * 0.3f, lw = std::max(1.5f, 1.5f * s);
    const RGBA c{ 1.0f, 1.0f, 1.0f, 1.0f };
    const float x0 = r.x + m, y0 = r.y + m, x1 = r.x + r.w - m, y1 = r.y + r.h - m;
    auto line = [&](float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / len * lw * 0.5f, ny = dx / len * lw * 0.5f;
        drawUiTris({ { ax + nx, ay + ny, c }, { bx + nx, by + ny, c }, { bx - nx, by - ny, c },
                     { ax + nx, ay + ny, c }, { bx - nx, by - ny, c }, { ax - nx, ay - ny, c } });
    };
    line(x0, y0, x1, y1);
    line(x0, y1, x1, y0);
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
    const RGBA rowBg{ 0.09f, 0.06f, 0.12f, 0.92f };
    const RGBA plate{ 0.478f, 0.043f, 0.278f, 1.0f };
    const Rect& b = L.box;
    drawRect(Rect{ b.x - 1.5f * s, b.y - 1.5f * s, b.w + 3.0f * s, b.h + 3.0f * s }, RGBA{ 0.878f, 0.129f, 0.541f, 1.0f });
    drawRect(b, RGBA{ 0.04f, 0.03f, 0.07f, 0.96f });

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

// The item's picture at exactly ITEM_PICTURE_PX square, its top left on a whole pixel, centered on (cx, cy)
void Renderer::drawItemPictureAt(float cx, float cy, int item, float alpha) {
    const float x = std::floor(cx - ITEM_PICTURE_PX * 0.5f + 0.5f), y = std::floor(cy - ITEM_PICTURE_PX * 0.5f + 0.5f);
    const float w = ITEM_PICTURE_PX, h = ITEM_PICTURE_PX;
    const float v[4 * 5] = { x, y + h, 0.0f, 0.0f, 1.0f, x + w, y + h, 0.0f, 1.0f, 1.0f,
                             x + w, y, 0.0f, 1.0f, 0.0f, x, y, 0.0f, 0.0f, 0.0f };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(itemTexture(item)->tex, v, RGBA{ 1.0f, 1.0f, 1.0f, alpha });
}

void Renderer::drawItemPicture(const Rect& cell, int item, float alpha) {
    drawItemPictureAt(cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f, item, alpha);
}

void Renderer::drawItemCell(const Rect& r, int item, int count) {
    if (item == 0) return;
    const float s = uiH_ / 720.0f;
    drawItemPicture(r, item, 1.0f);
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
float Renderer::pauseBarHeight(bool hint) {
    const float s = uiH_ / 720.0f;
    const float pad = 10.0f * s;
    const float bigH = uiTextHeight(lang("UILABEL_PAUSED")) * 2.6f;
    const float tipH = hint ? uiTextHeight(lang("UILABEL_PAUSE_HINT")) : 0.0f;
    return pad + bigH + (hint ? pad + tipH : 0.0f) + pad;
}

void Renderer::drawPauseBar(bool hint) {
    const float s = uiH_ / 720.0f;
    const float pad = 10.0f * s;
    const float bigH = uiTextHeight(lang("UILABEL_PAUSED")) * 2.6f;
    const std::string tip = lang("UILABEL_PAUSE_HINT");
    const float h = pauseBarHeight(hint);
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, h }, theme::alpha(theme::NIGHT, 0.55f));
    drawRectGradientH(Rect{ 0.0f, h - 2.0f * s, uiW_ * 0.5f, 2.0f * s }, theme::alpha(theme::PINK, 0.0f), theme::PINK);
    drawRectGradientH(Rect{ uiW_ * 0.5f, h - 2.0f * s, uiW_ * 0.5f, 2.0f * s }, theme::PINK, theme::alpha(theme::PINK, 0.0f));
    drawBigText(uiUpper(lang("UILABEL_PAUSED")), uiW_ * 0.5f, pad + bigH * 0.5f, bigH, theme::BONE);
    if (hint) drawUiText(tip, uiW_ * 0.5f - uiTextWidth(tip) * 0.5f, pad + bigH + pad, theme::ASH);
}

// The value bar's colour at t from 0 to 1: red at the start, yellow halfway, green at the end
static RGBA valueBarColor(float t) {
    const RGBA red{ 0.85f, 0.20f, 0.20f, 1.0f }, yellow{ 0.95f, 0.85f, 0.20f, 1.0f }, green{ 0.25f, 0.85f, 0.30f, 1.0f };
    const RGBA a = t < 0.5f ? red : yellow;
    const RGBA b = t < 0.5f ? yellow : green;
    const float u = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    return RGBA{ a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, 1.0f };
}

void Renderer::drawTrade(const TradeLayout& L, const std::string& npcName, const std::string& yourName,
                         const Inventory* npcInv, const std::vector<TradeGhost>& theirs,
                         const std::vector<TradeGhost>& yours, int dragFrom, const TradeStatus& status, bool showValue,
                         float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.624f, 0.902f, 0.0f, 1.0f };
    const RGBA panel{ 0.05f, 0.035f, 0.08f, 0.95f };
    const RGBA bar{ 0.478f, 0.043f, 0.278f, 1.0f };
    const RGBA well{ 0.03f, 0.02f, 0.045f, 1.0f };
    const RGBA plate{ 0.09f, 0.06f, 0.12f, 1.0f };
    const RGBA plateHot{ 0.14f, 0.21f, 0.0f, 1.0f };
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
    // Whose side is whose: each unit's name over its box
    auto nameOver = [&](const Rect& r, const std::string& name) {
        const std::string shown = fitText(name, r.w, false);
        if (shown != name) noteTip(name, r);
        drawListText(shown, r.x, r.y + (r.h - th) * 0.5f, heading);
    };
    nameOver(L.theirName, npcName);
    nameOver(L.yourName, yourName);
    side(L.theirBox, L.theirCells, theirs, lang("UILABEL_TRADE_DROP_THEIRS"), L.theirTotal);
    side(L.yourBox, L.yourCells, yours, lang("UILABEL_TRADE_DROP_YOURS"), L.yourTotal);

    // The value bar: red while the offer is far from the price, yellow, then green once it is met. A free trade between
    // your own units has no price, so no bar.
    if (showValue) drawRect(L.valueBar, well);
    const float fillW = L.valueBar.w * std::clamp(status.progress, 0.0f, 1.0f);
    const int slices = showValue ? 24 : 0;
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

void Renderer::drawStorage(const StorageLayout& L, const std::string& propName, const std::string& hint,
                           const Storage* storage, const Output* output, const std::string& status, int dragFrom,
                           float mx, float my, const std::string& toggleLabel) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.624f, 0.902f, 0.0f, 1.0f };
    const RGBA amber{ 0.91f, 0.65f, 0.24f, 1.0f };
    const RGBA bar{ 0.478f, 0.043f, 0.278f, 1.0f };
    const RGBA well{ 0.03f, 0.02f, 0.045f, 1.0f };
    const RGBA plate{ 0.09f, 0.06f, 0.12f, 1.0f };
    const RGBA plateHot{ 0.14f, 0.21f, 0.0f, 1.0f };
    const float th = listTextHeight("Ag");

    drawThemedPanel(L.panel);
    drawRect(L.bar, bar);
    drawListText(fitText(lang("UILABEL_INTERACTION_PANEL"), L.bar.w - 12.0f * s, false), L.bar.x + 6.0f * s,
                 L.bar.y + (L.bar.h - th) * 0.5f, white);
    const std::string name = fitText(propName, L.name.w, false);
    if (name != propName) noteTip(propName, L.name);
    drawListText(name, L.name.x, L.name.y, white);
    drawListText(fitText(lang(L.output ? "UILABEL_INPUT" : "UILABEL_INVENTORY"), L.heading.w, false), L.heading.x,
                 L.heading.y, heading);
    // One grid of cells. first is the panel slot of its first cell.
    ItemStack hoverStack{};
    auto grid = [&](const Rect& area, const std::vector<Rect>& cells, const ItemStack* stacks, int first) {
        drawRect(area, well);
        for (size_t i = 0; i < cells.size(); ++i) {
            const Rect& r = cells[i];
            const bool hot = r.contains(mx, my);
            drawRect(r, hot ? plateHot : plate);
            if (!stacks || i >= (size_t)STORAGE_SLOTS) continue;
            const ItemStack& st = stacks[i];
            if (st.item == 0 || first + (int)i == dragFrom) continue;
            drawItemCell(r, st.item, st.count);
            if (hot) hoverStack = st;
        }
    };
    grid(L.grid, L.slots, storage ? storage->slots.data() : nullptr, 0);
    if (L.output) {
        drawListText(fitText(lang("UILABEL_OUTPUT"), L.outHeading.w, false), L.outHeading.x, L.outHeading.y, heading);
        grid(L.outGrid, L.outSlots, output ? output->slots.data() : nullptr, STORAGE_SLOTS);
        const std::string line = fitText(status, L.status.w, false);
        if (line != status) noteTip(status, L.status);
        drawListText(line, L.status.x, L.status.y + (L.status.h - th) * 0.5f, amber);
    }
    float ty = L.hint.y;
    for (const std::string& ln : wrapListText(hint, L.hint.w)) {
        if (ty + th > L.hint.y + L.hint.h + 0.5f) break;
        drawListText(ln, L.hint.x, ty, gray);
        ty += th;
    }
    if (L.lockToggle && !toggleLabel.empty()) drawButton(L.lockBtn, toggleLabel, true, L.lockBtn.contains(mx, my));
    drawButton(L.closeBtn, lang("UILABEL_CLOSE"), true, L.closeBtn.contains(mx, my));
    if (hoverStack.item != 0 && dragFrom < 0) drawItemTooltip(hoverStack.item, hoverStack.count, mx, my);
}

void Renderer::drawLoot(const LootLayout& L, const std::string& name, const Equipment* worn, const Inventory* inv,
                        float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.624f, 0.902f, 0.0f, 1.0f };
    const RGBA bar{ 0.478f, 0.043f, 0.278f, 1.0f };
    const RGBA well{ 0.03f, 0.02f, 0.045f, 1.0f };
    const RGBA plate{ 0.09f, 0.06f, 0.12f, 1.0f };
    const RGBA plateHot{ 0.14f, 0.21f, 0.0f, 1.0f };
    const float th = listTextHeight("Ag");
    drawThemedPanel(L.panel);
    drawRect(L.bar, bar);
    drawListText(fitText(lang("UILABEL_INTERACTION_PANEL"), L.bar.w - 12.0f * s, false), L.bar.x + 6.0f * s,
                 L.bar.y + (L.bar.h - th) * 0.5f, white);
    const std::string shown = fitText(name, L.name.w, false);
    if (shown != name) noteTip(name, L.name);
    drawListText(shown, L.name.x, L.name.y, white);
    int hoverItem = 0, hoverCount = 0;
    // Gear: each slot shows its item, or its name dimmed when empty
    drawListText(lang("UILABEL_EQUIPMENT"), L.gearHeading.x, L.gearHeading.y, heading);
    drawRect(L.gearGrid, well);
    for (size_t i = 0; i < L.gear.size() && i < (size_t)GEAR_SLOTS; ++i) {
        const Rect& r = L.gear[i];
        const int item = worn ? worn->items[i] : 0;
        const bool hot = item != 0 && r.contains(mx, my);
        drawRect(r, hot ? plateHot : plate);
        if (hot) noteHover(r);
        if (item == 0) {
            const std::string slot = fitText(gearSlotName((int)i), r.w - 4.0f, false);
            drawListText(slot, r.x + (r.w - listTextWidth(slot)) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 0.72f, 0.72f, 0.76f, 0.4f });
            continue;
        }
        drawItemCell(r, item, 1);
        if (hot) { hoverItem = item; hoverCount = 1; }
    }
    // Inventory
    drawListText(lang("UILABEL_INVENTORY"), L.invHeading.x, L.invHeading.y, heading);
    drawRect(L.invGrid, well);
    for (size_t i = 0; i < L.slots.size() && i < (size_t)INVENTORY_SLOTS; ++i) {
        const Rect& r = L.slots[i];
        const ItemStack st = inv ? inv->slots[i] : ItemStack{};
        const bool hot = st.item != 0 && r.contains(mx, my);
        drawRect(r, hot ? plateHot : plate);
        if (hot) noteHover(r);
        if (st.item == 0 || st.count <= 0) continue;
        drawItemCell(r, st.item, st.count);
        if (hot) { hoverItem = st.item; hoverCount = st.count; }
    }
    float ty = L.hint.y;
    for (const std::string& ln : wrapListText(lang("UILABEL_LOOT_HINT"), L.hint.w)) {
        if (ty + th > L.hint.y + L.hint.h + 0.5f) break;
        drawListText(ln, L.hint.x, ty, gray);
        ty += th;
    }
    drawButton(L.takeAllBtn, lang("UILABEL_TAKE_ALL"), true, L.takeAllBtn.contains(mx, my));
    drawButton(L.closeBtn, lang("UILABEL_CLOSE"), true, L.closeBtn.contains(mx, my));
    if (hoverItem != 0) drawItemTooltip(hoverItem, hoverCount, mx, my);
}

void Renderer::drawRing(float cx, float cy, float outer, float inner, float from, float to, RGBA color) {
    from = std::clamp(from, 0.0f, 1.0f);
    to = std::clamp(to, 0.0f, 1.0f);
    if (to <= from) return;
    constexpr int FULL_SEGMENTS = 48;   // segments in a whole ring: smooth at this size
    const int n = std::max(1, (int)std::ceil(FULL_SEGMENTS * (to - from)));
    constexpr float TWO_PI = 6.28318531f;
    Batch b;
    for (int i = 0; i < n; ++i) {
        // Angle 0 is straight up, and it grows clockwise on screen (y grows downward)
        const float a0 = TWO_PI * (from + (to - from) * (float)i / n);
        const float a1 = TWO_PI * (from + (to - from) * (float)(i + 1) / n);
        const float s0 = std::sin(a0), c0 = std::cos(a0), s1 = std::sin(a1), c1 = std::cos(a1);
        b.quad({ cx + s0 * inner, cy - c0 * inner, 0 }, { cx + s0 * outer, cy - c0 * outer, 0 },
               { cx + s1 * outer, cy - c1 * outer, 0 }, { cx + s1 * inner, cy - c1 * inner, 0 }, color);
    }
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, b.v.size() * sizeof(Vertex), b.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b.v.size());
}

// The ring sits over the top of the prop's model, in window pixels, and fills as the next item nears. When an item is
// finished, the ring hops: up and back down once, quickly. It stays up for the hop even if the work stopped with that
// item, so the last one shows too.
void Renderer::drawPropProgress(World& w, const Camera& cam, int winW, int winH, bool names) {
    const float s = uiH_ / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float outer = 11.0f * s, inner = 4.3f * s;          // the hole is about two fifths of the ring
    const RGBA back{ 0.31f, 0.24f, 0.17f, 1.0f };             // dark brown
    const RGBA fill{ 0.91f, 0.65f, 0.24f, 1.0f };             // orange-yellow
    constexpr float HOP_SECONDS = 0.22f, HOP_HEIGHT = 9.0f;
    const float now = (float)SDL_GetTicks() / 1000.0f;

    std::map<Entity, PropHop> seen;
    for (Entity e : w.view<PropWork, Prop, Transform>()) {
        const PropWork* pw = w.get<PropWork>(e);
        PropHop hop;
        if (auto it = propHops_.find(e); it != propHops_.end() && it->second.made <= pw->made) hop = it->second;
        else hop.made = pw->made;   // first seen (or a new world): no hop for items made before
        if (pw->made != hop.made) {
            hop.made = pw->made;
            hop.start = now;
        }
        seen[e] = hop;
        const float t = hop.start >= 0.0f ? (now - hop.start) / HOP_SECONDS : 1.0f;
        const bool hopping = t >= 0.0f && t < 1.0f;
        if (pw->workers <= 0 && !hopping) continue;

        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const int kind = w.get<Prop>(e)->kind;
        const float top = propTop(kind);
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, propBaseY(w, e) + top + 0.25f, tr->z, winW, winH, sx, sy)) continue;
        // Above the prop's name tag when names are shown
        // Above the prop's block of name lines when names are shown (the block's top, from drawPropNames this frame)
        float above = sy - (names ? listTextHeight("Ag") + 2.0f * s : 0.0f);
        if (names) {
            if (const auto it = propLabelTop_.find(e); it != propLabelTop_.end()) above = std::min(above, it->second - 2.0f * s);
        }
        const float cy = above - outer - (hopping ? std::sin(t * 3.14159265f) * HOP_HEIGHT * s : 0.0f);
        // Just finished: the ring shows full for the hop, then the next item's progress
        const float frac = hopping ? 1.0f : (float)std::clamp(pw->progress, 0.0, 1.0);
        drawRing(sx, cy, outer, inner, 0.0f, 1.0f, back);
        drawRing(sx, cy, outer, inner, 0.0f, frac, fill);
    }
    propHops_.swap(seen);   // props no longer worked, or gone, are forgotten
}

namespace {
// A shape with its bottom right corner cut off at 45 degrees, filled top to bottom from top to bottom colour
void cutCornerShape(std::vector<UiVert>& v, const Rect& r, float cut, RGBA top, RGBA bottom) {
    cut = std::min(cut, std::min(r.w, r.h));
    auto at = [&](float x, float y) { return UiVert{ x, y, mixColor(top, bottom, r.h > 0.0f ? (y - r.y) / r.h : 0.0f) }; };
    const UiVert a = at(r.x, r.y), b = at(r.x + r.w, r.y), c = at(r.x + r.w, r.y + r.h - cut);
    const UiVert d = at(r.x + r.w - cut, r.y + r.h), e = at(r.x, r.y + r.h);
    v.insert(v.end(), { a, b, c, a, c, d, a, d, e });
}
} // namespace

void Renderer::drawThemedPanel(const Rect& r) {
    const float s = uiH_ / 720.0f;
    const float frame = std::max(1.0f, 2.0f * s), cut = 18.0f * s;
    std::vector<UiVert> v;
    cutCornerShape(v, r, cut, theme::PINK, theme::PINK_DEEP);
    cutCornerShape(v, Rect{ r.x + frame, r.y + frame, r.w - 2.0f * frame, r.h - 2.0f * frame }, cut - frame * 0.6f,
                   theme::alpha(rgb255(16, 10, 22, 1.0f), 0.97f), theme::alpha(rgb255(6, 5, 10, 1.0f), 0.98f));
    drawUiTris(v);
}

void Renderer::drawTitleHeader(const TitleLayout& L, const std::string& firstWord, const std::string& secondWord,
                               const std::string& eyebrow, const std::vector<std::string>& news) {
    const float s = uiH_ / 720.0f;
    const float sway = -titleMouse_ * 5.0f * s;   // the words lean against the backdrop's sway
    const unsigned long long now = SDL_GetTicks();

    // "Early Access": a short glowing green dash, then the words in small capitals
    {
        const float th = listTextHeight("Ag");
        const float cy = L.eyebrow.y + L.eyebrow.h * 0.5f;
        const float dx = L.eyebrow.x + sway + 3.0f * s;
        drawRect(Rect{ dx - 3.0f * s, cy - 3.0f * s, 32.0f * s, 6.0f * s }, theme::alpha(theme::GREEN, 0.18f));
        drawRect(Rect{ dx, cy - 1.0f * s, 26.0f * s, 2.0f * s }, theme::GREEN);
        drawListText(uiUpper(eyebrow), dx + 26.0f * s + 10.0f * s, cy - th * 0.5f, theme::alpha(theme::GREEN, 0.85f));
    }

    // The name: the first word green, a pink bar, the second word bone-white, in capitals, as large as the line allows.
    // The words are pictures drawn once in the very large title font and shrunk to size, so they stay sharp; without
    // that font they fall back to the UI font, enlarged.
    {
        const std::string a = uiUpper(firstWord), b = uiUpper(secondWord);
        const TextTex& ta = titleWord(a);
        const TextTex& tb = titleWord(b);
        const bool big = ta.tex && tb.tex;
        float lineH = L.name.h * 1.3f;                       // the height of a line of the name, as drawn
        const float baseH = std::max(1.0f, uiTextHeight("Ag"));
        auto wordW = [&](const TextTex& t, const std::string& text, float h) {
            return big ? t.aspect * h : uiTextWidth(text) * h / baseH;
        };
        float em = lineH / 1.36f;                            // the font's size, near enough
        const float total = wordW(ta, a, lineH) + wordW(tb, b, lineH) + em * (0.09f + 0.44f);
        if (total > L.name.w) {
            lineH *= L.name.w / total;
            em = lineH / 1.36f;
        }
        const float wa = wordW(ta, a, lineH);
        const float barW = std::max(2.0f, em * 0.09f), barH = em * 0.72f, barGap = em * 0.22f;
        // Now and then, for a moment, the name glitches: a jolt sideways and a pink and green ghost either side
        const float phase = (float)(now % 11000ULL) / 11000.0f;
        float jolt = 0.0f, pinkOff = 0.0f, greenOff = 0.0f;
        if (phase >= 0.974f && phase < 0.978f) { jolt = -3.0f; pinkOff = 3.0f; greenOff = -3.0f; }
        else if (phase >= 0.978f && phase < 0.982f) { jolt = 3.0f; pinkOff = -4.0f; greenOff = 2.0f; }
        else if (phase >= 0.986f && phase < 0.990f) { jolt = -1.0f; pinkOff = 2.0f; }
        const float x0 = L.name.x + sway + jolt * s;
        const float cy = L.name.y + L.name.h * 0.5f;
        const float top = cy - lineH * 0.5f;
        const float barX = x0 + wa + barGap;
        const float xb = barX + barW + barGap;
        // One word at its left edge and the line's top
        auto word = [&](const TextTex& t, const std::string& text, float left, float dx, float dy, RGBA tint) {
            if (big) {
                const float w = t.aspect * lineH, x = left + dx, y = top + dy;
                const float v[4 * 5] = { x, y + lineH, 0.0f, 0.0f, 1.0f, x + w, y + lineH, 0.0f, 1.0f, 1.0f,
                                         x + w, y, 0.0f, 1.0f, 0.0f, x, y, 0.0f, 0.0f, 0.0f };
                glUseProgram(glyphProgram_);
                glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
                drawQuadVerts(t.tex, v, tint);
            } else {
                drawUiTextLarge(text, left + wordW(t, text, lineH) * 0.5f + dx, cy + dy, lineH / baseH, tint);
            }
        };
        auto words = [&](float dx, float dy, RGBA ca, RGBA cb) {
            word(ta, a, x0, dx, dy, ca);
            word(tb, b, xb, dx, dy, cb);
        };
        // A dark drop below, then the glitch ghosts, a green glow round the first word, and the words
        words(0.0f, 6.0f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.55f }, RGBA{ 0.0f, 0.0f, 0.0f, 0.55f });
        if (pinkOff != 0.0f) words(pinkOff * s, 0.0f, theme::alpha(theme::PINK, 0.8f), theme::alpha(theme::PINK, 0.8f));
        if (greenOff != 0.0f) words(greenOff * s, 0.0f, theme::alpha(theme::GREEN, 0.8f), theme::alpha(theme::GREEN, 0.8f));
        for (int k = 0; k < 4; ++k) {
            const float gx = (k == 0 ? -2.0f : k == 1 ? 2.0f : 0.0f) * s, gy = (k == 2 ? -2.0f : k == 3 ? 2.0f : 0.0f) * s;
            word(ta, a, x0, gx, gy, theme::alpha(theme::GREEN, 0.14f));
        }
        words(0.0f, 0.0f, theme::GREEN, theme::BONE);
        // The bar: hot pink, with a glow, standing on the words' baseline
        const float barY = cy + barH * 0.5f - barH * 0.98f + em * 0.06f;
        drawRect(Rect{ barX - 4.0f * s, barY - 4.0f * s, barW + 8.0f * s, barH + 8.0f * s }, theme::alpha(theme::PINK, 0.22f));
        drawRect(Rect{ barX, barY, barW, barH }, theme::PINK);
    }

    // The rule: pink, fading out to the right
    drawRectGradientH(Rect{ L.rule.x + sway, L.rule.y, L.rule.w, L.rule.h }, theme::PINK, theme::alpha(theme::PINK, 0.0f));

    // The notice board: a pink frame, cut at the bottom right, over a dark pink inside with faint lines across it
    const Rect board{ L.news.x + sway, L.news.y, L.news.w, L.news.h };
    {
        const float frame = 3.0f * s, cut = 22.0f * s;
        std::vector<UiVert> v;
        cutCornerShape(v, Rect{ board.x + 2.0f * s, board.y + 14.0f * s, board.w, board.h }, cut, RGBA{ 0, 0, 0, 0.55f },
                       RGBA{ 0, 0, 0, 0.55f });   // its shadow
        cutCornerShape(v, board, cut, theme::PINK, theme::PINK_DEEP);
        const Rect in{ board.x + frame, board.y + frame, board.w - 2.0f * frame, board.h - 2.0f * frame };
        cutCornerShape(v, in, cut - frame, theme::PINK_DARK, theme::PINK_INK);
        const RGBA faint{ 1.0f, 1.0f, 1.0f, 0.035f };
        for (float y = in.y; y < in.y + in.h; y += 4.0f * s) {
            // The lines stop at the cut corner
            const float into = y - (in.y + in.h - (cut - frame));
            const float w = into > 0.0f ? in.w - into : in.w;
            if (w <= 0.0f) break;
            v.insert(v.end(), { { in.x, y, faint }, { in.x + w, y, faint }, { in.x + w, y + 1.0f, faint },
                                { in.x, y, faint }, { in.x + w, y + 1.0f, faint }, { in.x, y + 1.0f, faint } });
        }
        drawUiTris(v);
    }
    const float th = listTextHeight("Ag");
    const float textX = L.newsText.x + sway;
    float y = L.newsText.y;
    const float bottom = L.newsText.y + L.newsText.h;
    for (const std::string& raw : news) {
        if (raw.empty()) {
            y += th * 0.6f;
            continue;
        }
        const bool heading = raw.rfind("# ", 0) == 0;
        const bool bullet = raw.rfind("- ", 0) == 0;
        const std::string text = heading || bullet ? raw.substr(2) : raw;
        if (heading) {
            // A heading: pink capitals, with a pink line under them
            if (y + th > bottom) break;
            drawListText(uiUpper(text), textX, y, theme::PINK);
            y += th + 5.0f * s;
            drawRect(Rect{ textX, y, L.newsText.w, std::max(1.0f, 1.0f * s) }, theme::alpha(theme::PINK, 0.45f));
            y += 9.0f * s;
            continue;
        }
        // A point: a small green arrowhead, then the text, wrapped under itself. A point that doesn't fit whole is left
        // off, with the rest after it.
        const float indent = bullet ? th * 1.2f : 0.0f;
        const std::vector<std::string> wrapped = wrapListText(text, L.newsText.w - indent);
        if (y + th * (float)wrapped.size() > bottom) return;
        bool first = true;
        for (const std::string& ln : wrapped) {
            if (bullet && first) {
                const float cx = textX + 2.0f * s, cyy = y + th * 0.5f, h = 4.0f * s;
                drawUiTris({ { cx, cyy - h, theme::alpha(theme::GREEN, 0.9f) }, { cx + h * 1.4f, cyy, theme::alpha(theme::GREEN, 0.9f) },
                             { cx, cyy + h, theme::alpha(theme::GREEN, 0.9f) } });
            }
            drawListText(ln, textX + indent, y, theme::NOTICE_TEXT);
            y += th;
            first = false;
        }
        y += th * 0.35f;
    }
}

void Renderer::drawMenuItem(const Rect& r0, const std::string& label, const std::string& sub, bool selected, bool enabled,
                            float appear) {
    const float s = uiH_ / 720.0f;
    appear = std::clamp(appear, 0.0f, 1.0f);
    const float fade = appear;
    if (selected && enabled) noteHover(r0);
    Rect r = r0;
    r.x += (selected && enabled ? -14.0f * s : 0.0f) + (1.0f - appear) * 34.0f * s;   // lit items step out; new ones slide in
    auto A = [&](RGBA c) { return theme::alpha(c, c.a * fade); };
    // The band: dark, clearing to nothing on the left; lit, it turns green
    const float split = r.x + r.w * (selected ? 0.40f : 0.45f);
    const RGBA left = selected ? rgb255(159, 230, 0, 0.0f) : rgb255(10, 8, 16, 0.0f);
    const RGBA mid = selected ? rgb255(20, 30, 0, 0.75f) : rgb255(10, 8, 16, 0.72f);
    const RGBA right = selected ? rgb255(45, 66, 0, 0.9f) : rgb255(10, 8, 16, 0.9f);
    drawRectGradientH(Rect{ r.x, r.y, split - r.x, r.h }, A(left), A(mid));
    drawRectGradientH(Rect{ split, r.y, r.x + r.w - split, r.h }, A(mid), A(right));
    if (selected) {
        // A green glow along its right end, and a soft green halo
        const float gw = r.w * 0.46f;
        drawRectGradientH(Rect{ r.x + r.w - gw, r.y, gw, r.h }, A(rgb255(159, 230, 0, 0.0f)), A(rgb255(159, 230, 0, 0.16f)));
        drawRect(Rect{ r.x - 4.0f * s, r.y - 3.0f * s, r.w + 8.0f * s, r.h + 6.0f * s }, A(rgb255(159, 230, 0, 0.05f)));
    }
    // The edge on the left
    const RGBA edge = selected ? theme::GREEN : theme::alpha(theme::ASH, enabled ? 0.35f : 0.15f);
    drawRect(Rect{ r.x, r.y, 3.0f * s, r.h }, A(edge));
    if (selected) drawRect(Rect{ r.x - 1.0f * s, r.y, 1.0f * s, r.h }, A(theme::GREEN));
    // The words, in capitals
    const RGBA ink = !enabled ? theme::alpha(theme::ASH, 0.4f) : selected ? theme::BONE : theme::ASH;
    const std::string up = uiUpper(label);
    const float lh = uiTextHeight(up);
    const float subH = sub.empty() ? 0.0f : listTextHeight("Ag");
    const float block = lh + (sub.empty() ? 0.0f : 2.0f * s + subH);
    const float tx = r.x + 3.0f * s + 16.0f * s;
    const float ty = r.y + (r.h - block) * 0.5f;
    const float maxW = r.w - 3.0f * s - 28.0f * s;
    if (uiTextWidth(up) > maxW) {
        drawUiTextInBox(up, Rect{ tx, ty, maxW, lh }, A(ink));
    } else {
        drawUiText(up, tx, ty, A(ink));
    }
    if (!sub.empty()) {
        const RGBA subInk = selected ? rgb255(159, 230, 0, 0.85f) : theme::alpha(theme::ASH, 0.6f);
        drawListText(fitText(sub, maxW, false), tx, ty + lh + 2.0f * s, A(subInk));
    }
}

void Renderer::drawTitleFooter(const Rect& f, const std::string& left, const std::string& right) {
    const float s = uiH_ / 720.0f;
    const RGBA ink = theme::alpha(theme::ASH, 0.75f);
    const float th = listTextHeight("Ag");
    const float y = f.y + (f.h - th) * 0.5f;
    drawListText(uiUpper(left), f.x, y, ink);
    // The arrow keys as two small arrowheads, up and down, then the words
    const std::string words = uiUpper(right);
    const float ww = listTextWidth(words);
    const float h = 4.0f * s, w = 4.5f * s, gap = 4.0f * s;
    const float x = f.x + f.w - ww;
    const float ax = x - (4.0f * w + gap) - 8.0f * s, cy = f.y + f.h * 0.5f;
    drawUiTris({ { ax, cy + h, ink }, { ax + w, cy - h, ink }, { ax + 2.0f * w, cy + h, ink },
                 { ax + 2.0f * w + gap, cy - h, ink }, { ax + 3.0f * w + gap, cy + h, ink }, { ax + 4.0f * w + gap, cy - h, ink } });
    drawListText(words, x, y, ink);
}

void Renderer::drawOptimizeAll(const OptimizeAllLayout& L, int unitCount, int attackShare, const std::array<bool, 3>& toggles,
                               float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    const RGBA accent{ 0.624f, 0.902f, 0.0f, 1.0f };
    const float th = listTextHeight("Ag");
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, (float)uiH_ }, RGBA{ 0.0f, 0.0f, 0.0f, 0.45f });   // dims the game behind it
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_OPTIMIZE_ALL"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, white);
    float ty = L.text.y;
    for (const std::string& ln : wrapListText(langf("MISC_OPTIMIZE_ALL_ASK", { std::to_string(unitCount) }), L.text.w)) {
        if (ty + th > L.text.y + L.text.h + 0.5f) break;
        drawListText(ln, L.text.x, ty, gray);
        ty += th;
    }
    // The slider: all defense at the left, all attack at the right
    const int share = std::clamp(attackShare, 0, 100);
    const std::string sl = langf("UILABEL_OPTIMIZE_BALANCE", { std::to_string(share), std::to_string(100 - share) });
    drawListText(fitText(sl, L.sliderLabel.w, false), L.sliderLabel.x, L.sliderLabel.y + (L.sliderLabel.h - th) * 0.5f, white);
    drawRect(L.track, RGBA{ 0.16f, 0.10f, 0.20f, 1.0f });
    drawRect(Rect{ L.track.x, L.track.y, L.track.w * share / 100.0f, L.track.h }, accent);
    const float kx = L.track.x + L.track.w * share / 100.0f, kw = 8.0f * s, kh = L.track.h + 8.0f * s;
    drawRect(Rect{ kx - kw * 0.5f, L.track.y + (L.track.h - kh) * 0.5f, kw, kh }, white);
    // The toggles: a box, filled when on, and its label
    const char* keys[3] = { "UILABEL_OPTIMIZE_ACCESSORIES", "UILABEL_OPTIMIZE_TWO_HANDED", "UILABEL_OPTIMIZE_VALUE_TIES" };
    for (int i = 0; i < 3; ++i) {
        const Rect& r = L.toggles[(size_t)i];
        const float box = r.h - 6.0f * s;
        const Rect b{ r.x, r.y + 3.0f * s, box, box };
        const bool hot = r.contains(mx, my);
        if (hot) noteHover(r);
        drawRect(b, hot ? RGBA{ 0.30f, 0.42f, 0.06f, 1.0f } : RGBA{ 0.36f, 0.22f, 0.36f, 1.0f });
        drawRect(Rect{ b.x + 2.0f * s, b.y + 2.0f * s, b.w - 4.0f * s, b.h - 4.0f * s },
                 toggles[(size_t)i] ? accent : RGBA{ 0.07f, 0.05f, 0.10f, 1.0f });
        const std::string label = lang(keys[i]);
        const float lx = b.x + box + 8.0f * s;
        const std::string shown = fitText(label, r.x + r.w - lx, false);
        if (shown != label) noteTip(label, r);
        drawListText(shown, lx, r.y + (r.h - th) * 0.5f, white);
    }
    drawButton(L.confirm, lang("UILABEL_CONFIRM"), unitCount > 0, unitCount > 0 && L.confirm.contains(mx, my));
    drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mx, my));
}

void Renderer::drawDisband(World& w, const DisbandLayout& L, Entity unit, float held, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    const float th = listTextHeight("Ag");
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, (float)uiH_ }, RGBA{ 0.0f, 0.0f, 0.0f, 0.45f });   // dims the game behind it
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_DISBAND"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, white);

    // The portrait, as on the Profile tab
    drawRect(L.portrait, RGBA{ 0.10f, 0.07f, 0.13f, 1.0f });
    const TextTex& g = unitPortrait(w, unit);
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
        setSaturation(unitSaturation(w, unit));
        drawQuadVerts(g.tex, v, WHITE);
        setSaturation(1.0f);
    }

    // Beside it: the name, then its kind or body, its health, and its hunger (a person) or temperament (a mob)
    const Name* nm = w.get<Name>(unit);
    const std::string name = nm && !nm->text.empty() ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
    const float rowH = L.info.h / (float)(1 + DISBAND_INFO_LINES);
    drawUiText(fitText(name, L.info.w, true), L.info.x, L.info.y + (rowH - uiTextHeight("Ag")) * 0.5f, white);
    std::vector<std::string> lines;
    const Mob* mb = w.get<Mob>(unit);
    if (mb) lines.push_back(lang(mobInfo(mb->kind).nameKey));
    else if (const Body* bd = w.get<Body>(unit)) lines.push_back(lang(bodyTypeKey(bd->type)));
    if (const Needs* n = w.get<Needs>(unit)) {
        lines.push_back(lang("UILABEL_HEALTH") + ": " + std::to_string(n->hp) + " / " + std::to_string(n->maxHp));
        if (!mb) lines.push_back(lang("UILABEL_HUNGER") + ": " + std::to_string(n->hunger) + " / " + std::to_string(n->maxHunger));
    }
    if (mb) lines.push_back(langf("UILABEL_TEMPERAMENT", { lang(temperamentKey(mobTemperament(mb->kind))) }));
    for (size_t i = 0; i < lines.size() && i < (size_t)DISBAND_INFO_LINES; ++i) {
        const float ry = L.info.y + rowH * (float)(i + 1);
        drawListText(fitText(lines[i], L.info.w, false), L.info.x, ry + (rowH - th) * 0.5f, gray);
    }

    // What it does, and how to confirm
    const std::string what = mb ? langf("MISC_DISBAND_ASK_MOB", { name }) : langf("MISC_DISBAND_ASK", { name });
    float ty = L.text.y;
    for (const std::string& ln : wrapListText(what, L.text.w)) {
        if (ty + th > L.text.y + L.text.h + 0.5f) break;
        drawListText(ln, L.text.x, ty, gray);
        ty += th;
    }
    const std::string hint = langf("UILABEL_DISBAND_HOLD", { std::to_string(DISBAND_HOLD_MS / 1000) });
    drawListText(fitText(hint, L.hint.w, false), L.hint.x + (L.hint.w - std::min(L.hint.w, listTextWidth(hint))) * 0.5f,
                 L.hint.y + (L.hint.h - th) * 0.5f, white);

    // Disband fills from the left with red while it is held
    drawButton(L.disband, lang("UILABEL_DISBAND"), true, L.disband.contains(mx, my));
    const float f = std::clamp(held, 0.0f, 1.0f);
    if (f > 0.0f) {
        drawRect(Rect{ L.disband.x, L.disband.y, L.disband.w * f, L.disband.h }, RGBA{ 0.85f, 0.12f, 0.12f, 0.55f });
        const std::string label = lang("UILABEL_DISBAND");
        drawListText(label, L.disband.x + (L.disband.w - listTextWidth(label)) * 0.5f, L.disband.y + (L.disband.h - th) * 0.5f, white);
    }
    drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mx, my));
    (void)s;
}

void Renderer::drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                                  float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA well{ 0.03f, 0.02f, 0.045f, 1.0f };
    const float th = listTextHeight("Ag");
    drawThemedPanel(L.box);
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

void Renderer::drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units, bool running,
                             int repView) {
    const float s = uiH_ / 720.0f;
    const float bgAlpha = 0.5f;
    drawRect(L.panel, RGBA{ 0.05f, 0.035f, 0.075f, bgAlpha });
    drawTitleBar(L.title, ICON_SQUAD_PANEL, lang("UILABEL_SQUAD_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    // The tabs: the one shown is lit
    {
        const float th = listTextHeight("Ag");
        auto tab = [&](const Rect& r, const std::string& label, bool on) {
            if (r.w <= 0.0f) return;
            const bool hot = !on && r.contains(mouseX_, mouseY_);
            if (hot) noteHover(r);
            drawRect(r, on ? RGBA{ 0.176f, 0.259f, 0.0f, 0.95f } : RGBA{ 0.10f, 0.07f, 0.13f, hot ? 1.0f : 0.9f });
            if (on) drawRect(Rect{ r.x, r.y + r.h - 2.0f, r.w, 2.0f }, theme::GREEN);
            const std::string t = fitText(label, r.w - 6.0f * s, false);
            drawListText(t, r.x + (r.w - listTextWidth(t)) * 0.5f, r.y + (r.h - th) * 0.5f,
                         on ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.72f, 0.72f, 0.76f, 1.0f });
        };
        tab(L.tabSquad, lang("UILABEL_SQUAD_TAB"), L.tab == FACTION_TAB_SQUAD);
        tab(L.tabReputation, lang("UILABEL_REPUTATION"), L.tab == FACTION_TAB_REPUTATION);
    }
    if (L.tab == FACTION_TAB_REPUTATION) {
        drawReputationList(L, repView);
        return;
    }

    // Walk and run: the lit one is the pace the squad is moving at
    auto mode = [&](const Rect& r, const std::string& text, bool on) {
        if (r.contains(mouseX_, mouseY_)) noteHover(r);
        drawRect(r, on ? RGBA{ 0.40f, 0.60f, 0.0f, 1.0f } : RGBA{ 0.12f, 0.08f, 0.15f, bgAlpha });
        const float tw = listTextWidth(text), th = listTextHeight(text);
        drawListText(text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    };
    // One toggle for the pace, lit while the squad runs, and the View Map button beside it
    mode(L.paceButton, running ? lang("UILABEL_PACE_RUN") : lang("UILABEL_PACE_WALK"), running);
    // Optimize All, beside it: opens a dialog to confirm
    {
        const std::string label = lang("UILABEL_OPTIMIZE_ALL");
        const std::string shown = fitText(label, L.optimizeAllButton.w - 6.0f * s, false);
        if (shown != label) noteTip(label, L.optimizeAllButton);
        mode(L.optimizeAllButton, shown, false);
    }

    for (const UnitCell& c : L.cells) {
        const Entity e = units[c.index];
        const bool selected = w.has<Selected>(e);
        const Rect frame{ c.rect.x - 2.0f, c.rect.y - 2.0f, c.rect.w + 4.0f, c.rect.h + 4.0f };
        if (c.rect.contains(mouseX_, mouseY_)) noteHover(c.rect);
        // A selected unit's frame is solid purple, so it stands out from the see-through frames
        drawRect(frame, selected ? RGBA{ 0.40f, 0.60f, 0.0f, 1.0f } : RGBA{ 0.30f, 0.20f, 0.32f, bgAlpha });
        drawRect(Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, RGBA{ 0.10f, 0.07f, 0.13f, bgAlpha });

        // Portrait fitted inside the square, keeping its shape
        const TextTex& g = unitPortrait(w, e);
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
            setSaturation(unitSaturation(w, e));
            drawQuadVerts(g.tex, v, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
            setSaturation(1.0f);
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
        drawRect(Rect{ c.rect.x, barY, c.rect.w, barH }, RGBA{ 0.16f, 0.11f, 0.18f, bgAlpha });
        drawRect(Rect{ c.rect.x, barY, c.rect.w * frac, barH }, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });

        // Name under the bar, cut at a word with "..." to fit the cell
        const Name* nm = w.get<Name>(e);
        const std::string full = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
        const std::string label = fitText(full, c.rect.w, false);
        if (label != full) noteTip(full, c.rect);
        drawListText(label, c.rect.x, c.rect.y + c.rect.w + 8.0f * s, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    // The scroll bar: the track and its thumb, with an arrow button at each end that moves the list a row. The arrows
    // dim at the end of the list.
    if (L.track.h > 0.0f || L.scrollUp.h > 0.0f) {
        drawRect(L.track, RGBA{ 0.16f, 0.10f, 0.20f, bgAlpha });
        if (L.scrollable()) drawRect(L.thumb, L.thumb.contains(mouseX_, mouseY_) ? RGBA{ 0.85f, 0.85f, 0.95f, 1.0f }
                                                                                : RGBA{ 0.7f, 0.7f, 0.8f, 1.0f });
        auto arrow = [&](const Rect& r, bool up, bool can) {
            if (r.h <= 0.0f) return;
            const bool hot = can && r.contains(mouseX_, mouseY_);
            if (hot) noteHover(r);
            drawRect(r, hot ? RGBA{ 0.30f, 0.20f, 0.36f, 1.0f } : RGBA{ 0.20f, 0.13f, 0.25f, 0.9f });
            const RGBA c = can ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.5f, 0.5f, 0.55f, 0.6f };
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, hw = r.w * 0.3f, hh = r.h * 0.22f;
            if (up) drawUiTris({ { cx, cy - hh, c }, { cx + hw, cy + hh, c }, { cx - hw, cy + hh, c } });
            else drawUiTris({ { cx - hw, cy - hh, c }, { cx + hw, cy - hh, c }, { cx, cy + hh, c } });
        };
        arrow(L.scrollUp, true, L.scrollRow > 0);
        arrow(L.scrollDown, false, L.scrollRow < L.maxScroll());
    }
}

// The Reputation tab: the faction shown, with its colour, then each faction's reputation with it. The faction shown is
// always at 100 with itself, so its own row reads (...). Each value is red where the two fight on sight, green where
// they are allies, and gray between; its tooltip says which. A row is pressed to show that faction's reputations.
void Renderer::drawReputationList(const UnitPanelLayout& L, int repView) {
    const float s = uiH_ / 720.0f;
    const float th = listTextHeight("Ag");
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    auto swatch = [&](float x, float cy, float d, int faction) {
        const FactionColor c = factionColor(faction);
        drawRect(Rect{ x - 1.0f, cy - d * 0.5f - 1.0f, d + 2.0f, d + 2.0f }, RGBA{ 0.0f, 0.0f, 0.0f, 0.9f });
        drawRect(Rect{ x, cy - d * 0.5f, d, d }, RGBA{ c.r, c.g, c.b, 1.0f });
    };
    // The heading: the faction shown
    {
        const Rect& h = L.repHeading;
        drawRect(h, RGBA{ 0.0f, 0.0f, 0.0f, 0.25f });
        const float d = 12.0f * s;
        swatch(h.x + 6.0f * s, h.y + h.h * 0.5f, d, repView);
        const std::string name = fitText(factionName(repView), h.w - d - 16.0f * s, false);
        drawListText(name, h.x + d + 12.0f * s, h.y + (h.h - th) * 0.5f, RGBA{ 0.624f, 0.902f, 0.0f, 1.0f });
    }
    const std::vector<FactionDef>& list = reputationFactions();
    for (size_t i = 0; i < L.repRows.size(); ++i) {
        const size_t idx = (size_t)L.repFirst + i;
        if (idx >= list.size()) break;
        const Rect& r = L.repRows[i];
        const int other = list[idx].id;
        const bool self = other == repView;
        const bool hot = r.contains(mouseX_, mouseY_);
        if (hot && !self) noteHover(r);
        drawRect(r, self ? RGBA{ 0.176f, 0.259f, 0.0f, 0.55f } : RGBA{ 0.10f, 0.07f, 0.13f, hot ? 0.95f : 0.6f });
        const float d = 10.0f * s;
        swatch(r.x + 6.0f * s, r.y + r.h * 0.5f, d, other);
        const int value = reputation(repView, other);
        const std::string valueText = self ? std::string("(...)") : std::to_string(value);
        const RGBA valueTint = self ? gray
                             : value <= REP_HOSTILE ? RGBA{ 1.0f, 0.42f, 0.42f, 1.0f }
                             : value >= REP_ALLIED ? RGBA{ 0.5f, 0.95f, 0.5f, 1.0f } : gray;
        const float vw = listTextWidth(valueText);
        const float nameX = r.x + d + 12.0f * s;
        const std::string name = fitText(factionName(other), r.x + r.w - 8.0f * s - vw - 8.0f * s - nameX, false);
        drawListText(name, nameX, r.y + (r.h - th) * 0.5f, white);
        drawListText(valueText, r.x + r.w - 8.0f * s - vw, r.y + (r.h - th) * 0.5f, valueTint);
        if (!self) {
            noteTip(lang(value <= REP_HOSTILE ? "UILABEL_REP_HOSTILE" : value >= REP_ALLIED ? "UILABEL_REP_ALLIED" : "UILABEL_REP_NEUTRAL"),
                    Rect{ r.x + r.w - 8.0f * s - vw, r.y, vw + 8.0f * s, r.h });
        }
    }
}

// The lock at the left of a panel's [-] or [+]: closed while the panel's size and place are fixed, open (and green)
// while it can be moved and resized
void Renderer::drawLockButton(const Rect& r, bool locked) {
    const float s = uiH_ / 720.0f;
    if (r.contains(mouseX_, mouseY_)) noteHover(r);
    drawRect(r, locked ? theme::alpha(theme::PINK, 0.9f) : theme::GREEN);
    drawRect(Rect{ r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f },
             locked ? theme::alpha(theme::PINK_INK, 0.95f) : RGBA{ 0.12f, 0.22f, 0.0f, 0.95f });
    const RGBA c{ 1.0f, 1.0f, 1.0f, 1.0f };
    // The body: a small block in the lower half
    const float bw = r.w * 0.52f, bh = r.h * 0.34f;
    const float bx = r.x + (r.w - bw) * 0.5f, by = r.y + r.h * 0.50f;
    drawRect(Rect{ bx, by, bw, bh }, c);
    // The shackle: a hoop over the body, its right leg lifted out when open
    const float lw = std::max(1.0f, 1.3f * s);
    const float sx0 = bx + bw * 0.18f, sx1 = bx + bw * 0.82f;
    const float top = r.y + r.h * 0.18f + (locked ? 0.0f : -r.h * 0.06f);
    const float legBottom = locked ? by : by - r.h * 0.12f;
    drawRect(Rect{ sx0, top, sx1 - sx0, lw }, c);                 // across the top
    drawRect(Rect{ sx0, top, lw, by - top }, c);                  // left leg, down into the body
    drawRect(Rect{ sx1 - lw, top, lw, legBottom - top }, c);      // right leg
}

// The Stats tab: every stat's level under the heading of its group, with its progress bar. Returns the stat under
// the mouse, or -1. The hover tooltip is drawn later, outside the panel's clip (drawStatTip).
int Renderer::drawStatsTab(const SidebarLayout& L, const Stats& stats, float mx, float my, bool mob) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA heading{ 0.624f, 0.902f, 0.0f, 1.0f };
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
        if (mob && group != StatGroup::Attributes && group != StatGroup::Combat) continue;
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
            drawRect(Rect{ nameX, barY, barW, barH }, RGBA{ 0.16f, 0.11f, 0.18f, 1.0f });
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
    rows[0].tint = RGBA{ 0.624f, 0.902f, 0.0f, 1.0f };
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
        const std::string names[UNIT_TAB_COUNT] = { lang("UILABEL_INVENTORY"), lang("UILABEL_PROFILE"), lang("UILABEL_STATS"), lang("UILABEL_BEHAVIOR"), lang("UILABEL_BUILD") };
        for (int i = 0; i < UNIT_TAB_COUNT; ++i) {
            const Rect& t = L.tabs[i];
            if (t.w <= 0.0f) continue;
            // A unit not in your squad shows only its Profile: the other tabs are dimmed and do nothing
            const bool on = unitTabEnabled(w, unit, i);
            drawRect(t, i == L.tab ? RGBA{ 0.176f, 0.259f, 0.0f, 0.95f } : RGBA{ 0.10f, 0.07f, 0.13f, on ? 0.9f : 0.45f });
            if (i == L.tab) drawRect(Rect{ t.x, t.y + t.h - 2.0f, t.w, 2.0f }, theme::GREEN);
            if (on && t.contains(mx, my)) noteHover(t);
            const float tw = listTextWidth(names[i]), th = listTextHeight("Ag");
            drawListText(names[i], t.x + (t.w - tw) * 0.5f, t.y + (t.h - th) * 0.5f,
                         on ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.55f, 0.52f, 0.60f, 0.6f });
        }
    };
    drawRect(L.panel, RGBA{ 0.04f, 0.025f, 0.06f, bgAlpha });
    if (!L.open) {
        frame();
        return;
    }
    if (L.tab == UNIT_TAB_BUILD) {
        // The Build tab needs no unit: it lists the props to pick and place. Its rows scroll like the other tabs.
        beginClip(L.panel);
        drawBuildSection(L, mx, my);
        if (L.maxScroll > 0.0f) {
            const Rect track{ L.panel.x + L.panel.w - 9.0f * s, L.body.y + 4.0f * s, 5.0f * s, L.body.h - 8.0f * s };
            const float totalH = L.maxScroll + L.body.h;
            const float thumbH = std::max(16.0f * s, track.h * L.body.h / totalH);
            const float thumbY = track.y + (track.h - thumbH) * (L.scroll / L.maxScroll);
            drawRect(track, RGBA{ 0.13f, 0.08f, 0.17f, 0.9f });
            drawRect(Rect{ track.x, thumbY, track.w, thumbH }, RGBA{ 0.878f, 0.129f, 0.541f, 0.9f });
        }
        endClip();
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
    // ---- Profile card: the unit's name, health, hunger and carry, then its picture under them ----
    drawRect(L.profile, RGBA{ 0.07f, 0.05f, 0.10f, bgAlpha });
    drawRect(L.portrait, RGBA{ 0.10f, 0.07f, 0.13f, bgAlpha });
    const TextTex& g = unitPortrait(w, unit);
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
        setSaturation(unitSaturation(w, unit));
        drawQuadVerts(g.tex, v, WHITE);
        setSaturation(1.0f);
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

    // The text across the whole card, over the picture
    const float tx = L.profile.x + 10.0f * s;
    const float rightEdge = L.profile.x + L.profile.w - 10.0f * s;
    const float colW = rightEdge - tx;

    const std::string name = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
    float y = L.profile.y + 10.0f * s;
    drawUiText(fitText(name, L.profile.x + L.profile.w - 10.0f * s - tx, true), tx, y, white);

    y += uiTextHeight("Ag") + 6.0f * s;
    // Under the name: the faction the unit belongs to, then the body type the name was drawn for, a line each
    auto infoLine = [&](const std::string& line) {
        const std::string shown = fitText(line, colW, false);
        if (shown != line) noteTip(line, Rect{ tx, y, colW, listTextHeight("Ag") });
        drawListText(shown, tx, y, gray);
        y += listTextHeight("Ag") + 6.0f * s;
    };
    if (const Faction* fa = w.get<Faction>(unit)) infoLine(factionName(fa->id));
    if (const Body* bd = w.get<Body>(unit)) infoLine(lang(bodyTypeKey(bd->type)));
    // A wild mob has no reputation with anyone: its temperament says how it takes to outsiders (mobs.h)
    if (const Mob* mb = w.get<Mob>(unit); mb && w.has<Faction>(unit) && isMobFaction(w.get<Faction>(unit)->id)) {
        infoLine(langf("UILABEL_TEMPERAMENT", { lang(temperamentKey(mobTemperament(mb->kind))) }));
    }

    // A stat: its label and value on one line, and a bar under them. Returns the y for the next stat.
    auto statRow = [&](float rowY, const std::string& label, const std::string& value, float frac, RGBA fill) {
        drawListText(label, tx, rowY, gray);
        drawListText(value, rightEdge - listTextWidth(value), rowY, white);
        const float barY = rowY + listTextHeight("Ag") + 3.0f * s;
        const float barH = 8.0f * s;
        drawRect(Rect{ tx, barY, colW, barH }, RGBA{ 0.16f, 0.11f, 0.18f, bgAlpha });
        drawRect(Rect{ tx, barY, colW * std::clamp(frac, 0.0f, 1.0f), barH }, fill);
        return barY + barH + 9.0f * s;
    };
    y = statRow(y, lang("UILABEL_HEALTH"), std::to_string(needs.hp) + " / " + std::to_string(needs.maxHp),
                needs.maxHp > 0 ? (float)needs.hp / (float)needs.maxHp : 0.0f, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });
    // Doom, under the health, while the unit is Unconscious (or dead, at 100): it dies when the bar is full. One
    // knocked out well rested has no Doom: it just lies out until it wakes.
    if (const Unconscious* un = w.get<Unconscious>(unit); un && !un->rested) {
        const int doom = (int)std::floor(std::clamp(un->doom, 0.0f, 100.0f));
        y = statRow(y, lang("UILABEL_DOOM"), std::to_string(doom) + " / 100", un->doom / 100.0f,
                    RGBA{ 0.56f, 0.20f, 0.78f, 1.0f });
    }
    // A mob has no hunger, Rested, Comfort or carry: it can't feed itself, sleeps in no bed and carries nothing
    if (!w.has<Mob>(unit)) {
    y = statRow(y, lang("UILABEL_HUNGER"), std::to_string(needs.hunger) + " / " + std::to_string(needs.maxHunger),
                needs.maxHunger > 0 ? (float)needs.hunger / (float)needs.maxHunger : 0.0f, RGBA{ 0.86f, 0.62f, 0.22f, 1.0f });
    y = statRow(y, lang("UILABEL_RESTED"), std::to_string(needs.rested) + " / " + std::to_string(needs.maxRested),
                needs.maxRested > 0 ? (float)needs.rested / (float)needs.maxRested : 0.0f, RGBA{ 0.38f, 0.55f, 0.95f, 1.0f });
    y = statRow(y, lang("UILABEL_COMFORT"), std::to_string(needs.comfort) + " / " + std::to_string(needs.maxComfort),
                needs.maxComfort > 0 ? (float)needs.comfort / (float)needs.maxComfort : 0.0f, RGBA{ 0.35f, 0.78f, 0.62f, 1.0f });

    // Carried weight against what Strength lets the unit carry
    const float carried = carriedWeightOf(w, unit);
    const float limit = carryLimit(stats);
    char carry[48];
    std::snprintf(carry, sizeof(carry), "%.1f / %.0f", carried, limit);
    statRow(y, lang("UILABEL_CARRY"), carry, limit > 0.0f ? carried / limit : 0.0f, RGBA{ 0.45f, 0.65f, 0.88f, 1.0f });
    }
    }

    if (L.tab == UNIT_TAB_INVENTORY) {
    // ---- Gear: what the unit wears, four slots to a row, then the totals ----
    const bool offLocked = worn && offHandBlocked(*worn);
    const ItemDef* dragDef = dragItem != 0 ? findItem(dragItem) : nullptr;
    // A two-handed weapon, held or dragged, dims the off hand
    const bool dimOff = offLocked || (dragDef && dragDef->equipSlot == "two_hand");
    drawRect(L.gearPanel, RGBA{ 0.06f, 0.04f, 0.085f, bgAlpha });
    drawIconText(L.gearPanel.x + 10.0f * s, L.gearPanel.y + 6.0f * s, listTextHeight("Ag"), ICON_EQUIPMENT, lang("UILABEL_EQUIPMENT"), white);
    // Optimize and Remove All, for a unit of yours. A label too long for its button is shortened, with the whole of it
    // in a tooltip.
    auto gearButton = [&](const Rect& r, const std::string& label) {
        const std::string shown = fitText(label, r.w - 6.0f * s, false);
        if (shown != label) noteTip(label, r);
        drawSmallButton(r, shown, editable, editable && r.contains(mx, my));
    };
    // A mob has no gear slots: the panel says so in their place
    if (w.has<Mob>(unit)) {
        drawListText(lang("UILABEL_MOB_NO_GEAR"), L.gearPanel.x + 10.0f * s, L.gearPanel.y + L.gearPanel.h * 0.5f - listH * 0.5f,
                     RGBA{ 0.72f, 0.72f, 0.76f, 1.0f });
    } else {
        gearButton(L.optimizeButton, lang("UILABEL_OPTIMIZE"));
        gearButton(L.removeAllButton, lang("UILABEL_REMOVE_ALL"));
    }

    // While an item is dragged, each gear slot shows whether it may go there: green if so, red if not.
    // A gear item leaving its own slot is not in the way of itself.
    Equipment eval = worn ? *worn : Equipment{};
    if (dimGear >= 0) eval.items[dimGear] = 0;

    const float inset = 5.0f * s;
    for (size_t gi = 0; gi < L.gear.size() && gi < GEAR_SLOTS; ++gi) {
        const Rect& r = L.gear[gi];
        RGBA edge{ 0.36f, 0.22f, 0.36f, bgAlpha };
        if (dragItem != 0) {
            std::string why;
            const bool fits = canEquip(eval, dragItem, (int)gi, why);
            edge = fits ? RGBA{ 0.35f, 0.90f, 0.45f, 1.0f } : RGBA{ 0.92f, 0.30f, 0.30f, 1.0f };
        }
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, edge);
        drawRect(r, RGBA{ 0.10f, 0.07f, 0.12f, bgAlpha });
        if (r.contains(mx, my)) hoverGear = (int)gi;

        const int item = worn ? worn->items[gi] : 0;
        const bool locked = (int)gi == GEAR_OFF_HAND && offLocked;
        if (item == 0) {
            // An empty slot shows its name, or lang("UILABEL_TWO_HANDED") for a locked off hand, centred, dimmed and
            // wrapped onto two lines if it needs them. The full name is in the hover tooltip.
            const std::string text = locked ? lang("UILABEL_TWO_HANDED") : gearSlotName((int)gi);
            std::vector<std::string> lines = wrapListText(text, r.w - 2.0f * inset);
            if (lines.size() > 2) lines.resize(2);
            const float lh = listH;
            float ty = r.y + (r.h - lh * lines.size()) * 0.5f;
            for (const std::string& ln : lines) {
                const std::string shown = fitText(ln, r.w - 2.0f * inset, false);
                drawListText(shown, r.x + (r.w - listTextWidth(shown)) * 0.5f, ty,
                             locked ? gray : RGBA{ 0.72f, 0.72f, 0.76f, 0.45f });
                ty += lh;
            }
            continue;
        }

        // The item's 64 x 64 picture fills the cell, as in the inventory. The slot's and the item's names are in the
        // hover tooltip.
        float alpha = 1.0f;
        if ((int)gi == dimGear) alpha = 0.30f;
        else if ((int)gi == GEAR_OFF_HAND && dimOff) alpha = 0.35f;
        drawItemPictureAt(r.x + r.w * 0.5f, r.y + r.h * 0.5f, item, alpha);
    }

    int attack = 0, defense = 0;
    if (worn) gearTotals(*worn, attack, defense);
    const std::string totals = langf("UILABEL_ATTACK_DEFENSE", { std::to_string(attack), std::to_string(defense) });
    drawListText(totals, L.gearPanel.x + 10.0f * s, L.gearPanel.y + L.gearPanel.h - listH - 6.0f * s, white);

    }
    // ---- Inventory grid on its own background ----
    if (L.tab == UNIT_TAB_INVENTORY) {
    drawRect(L.gridPanel, RGBA{ 0.06f, 0.04f, 0.085f, bgAlpha });
    drawIconText(L.gridPanel.x + 10.0f * s, L.gridPanel.y + 6.0f * s, listTextHeight("Ag"), ICON_INVENTORY, lang("UILABEL_INVENTORY"), white);
    // A mob's inventory is not shown until it is a corpse (mobs.h): the panel says Not available in place of its slots,
    // its slot count and its sort button
    const bool hidden = w.has<Mob>(unit) && !w.has<Corpse>(unit);
    if (hidden) {
        const std::string na = lang("UILABEL_MOB_INVENTORY_HIDDEN");
        drawListText(na, L.gridPanel.x + (L.gridPanel.w - listTextWidth(na)) * 0.5f, L.gridPanel.y + (L.gridPanel.h - listH) * 0.5f, gray);
    } else {
        // The slot count sits left of the sort button, which is at the right of the heading
        const std::string used = std::to_string(inv ? occupiedSlots(*inv) : 0) + " / " + std::to_string(INVENTORY_SLOTS) + " slots";
        drawListText(used, L.sortButton.x - 8.0f * s - listTextWidth(used), L.gridPanel.y + 6.0f * s, gray);
        drawSmallButton(L.sortButton, lang("UILABEL_SORT"), editable, editable && L.sortButton.contains(mx, my));
    }

    for (size_t i = 0; !hidden && inv && i < L.slots.size() && i < INVENTORY_SLOTS; ++i) {
        const Rect& r = L.slots[i];
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, RGBA{ 0.36f, 0.22f, 0.36f, bgAlpha });
        drawRect(r, RGBA{ 0.10f, 0.07f, 0.12f, bgAlpha });

        const ItemStack& st = inv->slots[i];
        if (st.item == 0 || st.count <= 0) continue;

        // An item shows its picture, dimmed while it is being dragged. The count sits in the corner.
        drawItemPicture(r, st.item, (int)i == dimSlot ? 0.30f : 1.0f);
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
        const Equipment* worn = w.get<Equipment>(unit);
        const std::array<bool, 4> modes{ beh && beh->passive, beh && beh->block, beh && beh->sneak,
                                         worn && isRangedWeapon(worn->items[GEAR_MAIN_HAND]) };
        drawBehaviorSection(L, beh ? beh->rules : std::vector<BehaviorRule>(), taskNameOf(w, unit), mx, my, editable, modes);
    }
    if (L.tab == UNIT_TAB_STATS) hoverStat = drawStatsTab(L, stats, mx, my, w.has<Mob>(unit));
    if (L.maxScroll > 0.0f) {
        // The scroll bar down the right side: its thumb is the part of the tab in view
        const Rect track{ L.panel.x + L.panel.w - 9.0f * s, L.body.y + 4.0f * s, 5.0f * s, L.body.h - 8.0f * s };
        const float totalH = L.maxScroll + L.body.h;
        const float thumbH = std::max(16.0f * s, track.h * L.body.h / totalH);
        const float thumbY = track.y + (track.h - thumbH) * (L.scroll / L.maxScroll);
        drawRect(track, RGBA{ 0.13f, 0.08f, 0.17f, 0.9f });
        drawRect(Rect{ track.x, thumbY, track.w, thumbH }, RGBA{ 0.878f, 0.129f, 0.541f, 0.9f });
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

// The Build tab's body. The heading says what is picked; each row has a select button (which picks the kind, or puts it
// down if it is picked already) and the prop's name. Row 0 is Destroy, the tool that takes buildings down.
void Renderer::drawBuildSection(const SidebarLayout& L, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    auto kindName = [](int kind) {
        return kind == BUILD_DESTROY ? lang("UILABEL_BUILD_DESTROY") : lang(propInfo(kind).nameKey);
    };
    const std::string head = buildSelected_ != BUILD_NONE
        ? langf("UILABEL_BUILD_SELECTED", { kindName(buildSelected_) })
        : lang("UILABEL_BUILD_NONE");
    const float headTextH = listTextHeight("Ag");
    drawListText(head, L.buildHeading.x + 4.0f * s, L.buildHeading.y + (L.buildHeading.h - headTextH) * 0.5f,
                 buildSelected_ != BUILD_NONE ? theme::GREEN : RGBA{ 0.72f, 0.72f, 0.76f, 1.0f });
    const float btnW = 64.0f * s, gap = 8.0f * s;
    for (size_t i = 0; i < L.buildRows.size(); ++i) {
        const Rect& row = L.buildRows[i];
        const int kind = buildRowKind((int)i);
        if (kind == BUILD_NONE) continue;
        const bool picked = kind == buildSelected_;
        const bool hovered = row.contains(mx, my);
        if (hovered) drawRect(row, RGBA{ 1.0f, 1.0f, 1.0f, 0.06f });
        const Rect btn{ row.x + 2.0f * s, row.y + 2.0f * s, btnW, row.h - 4.0f * s };
        drawSmallButton(btn, lang(picked ? "UILABEL_BUILD_PICKED" : "UILABEL_BUILD_SELECT"), true, hovered);
        const float th = listTextHeight("Ag");
        drawListText(kindName(kind), btn.x + btnW + gap, row.y + (row.h - th) * 0.5f, picked ? theme::GREEN : white);
    }
}

// The placement preview: the footprint of the picked prop, as a grid of quarter-tile squares on the ground, lined in
// red, and filled green where it can be placed or red where it can't. Only squares whose centre lies in the shape are
// drawn, so the grid follows the shape, turned as the prop will be.
void Renderer::drawBuildPreview(const Terrain& t) {
    if (!buildPreview_.on && buildPreview_.ringR <= 0.0f) return;
    const float lift = 0.04f;      // off the ground, so the grid does not flicker into it
    const float lineW = 0.03f;     // a grid line, in tiles
    auto hgt = [&](float x, float z) { return t.heightAt(x, z) + lift; };
    Batch b;

    if (buildPreview_.on) {
        const Footprint& f = propInfo(buildPreview_.kind).foot;
        const float x0 = buildPreview_.x, z0 = buildPreview_.z, yaw = buildPreview_.yaw;
        const float reach = footprintReach(f);
        const float cell = 0.25f;      // grid squares are a quarter of a tile
        const RGBA fill = buildPreview_.valid ? RGBA{ 0.25f, 1.0f, 0.35f, 0.25f } : RGBA{ 1.0f, 0.2f, 0.2f, 0.25f };
        const RGBA line{ 1.0f, 0.15f, 0.15f, 0.9f };
        auto strip = [&](float ax, float az, float bx, float bz) {
            const float dx = bx - ax, dz = bz - az;
            const float len = std::hypot(dx, dz);
            if (len <= 0.0f) return;
            const float px = -dz / len * lineW * 0.5f, pz = dx / len * lineW * 0.5f;
            b.quad(V3{ ax + px, hgt(ax + px, az + pz), az + pz }, V3{ bx + px, hgt(bx + px, bz + pz), bz + pz },
                   V3{ bx - px, hgt(bx - px, bz - pz), bz - pz }, V3{ ax - px, hgt(ax - px, az - pz), az - pz }, line);
        };
        const float startX = std::floor((x0 - reach) / cell) * cell, endX = x0 + reach;
        const float startZ = std::floor((z0 - reach) / cell) * cell, endZ = z0 + reach;
        for (float gx = startX; gx < endX; gx += cell) {
            for (float gz = startZ; gz < endZ; gz += cell) {
                const float cx = gx + cell * 0.5f, cz = gz + cell * 0.5f;
                if (!insideFootprint(f, cx - x0, cz - z0, yaw)) continue;
                const V3 a{ gx, hgt(gx, gz), gz }, bb{ gx + cell, hgt(gx + cell, gz), gz };
                const V3 c{ gx + cell, hgt(gx + cell, gz + cell), gz + cell }, d{ gx, hgt(gx, gz + cell), gz + cell };
                b.quad(a, bb, c, d, fill);
                strip(gx, gz, gx + cell, gz);
                strip(gx + cell, gz, gx + cell, gz + cell);
                strip(gx + cell, gz + cell, gx, gz + cell);
                strip(gx, gz + cell, gx, gz);
            }
        }
    }

    // The claim circle: a band a third of a tile wide, on the ground around the flag's spot. While it shows, every
    // other flag's circle is drawn too, in white.
    auto ring = [&](float cx, float cz, float r, const RGBA& tone) {
        const float ro = r, ri = r - 0.3f;
        const int segments = 96;
        const float twoPi = 6.2831853f;
        for (int i = 0; i < segments; ++i) {
            const float a0 = twoPi * i / segments, a1 = twoPi * (i + 1) / segments;
            const float o0x = cx + ro * std::cos(a0), o0z = cz + ro * std::sin(a0);
            const float o1x = cx + ro * std::cos(a1), o1z = cz + ro * std::sin(a1);
            const float i0x = cx + ri * std::cos(a0), i0z = cz + ri * std::sin(a0);
            const float i1x = cx + ri * std::cos(a1), i1z = cz + ri * std::sin(a1);
            b.quad(V3{ o0x, hgt(o0x, o0z), o0z }, V3{ o1x, hgt(o1x, o1z), o1z },
                   V3{ i1x, hgt(i1x, i1z), i1z }, V3{ i0x, hgt(i0x, i0z), i0z }, tone);
        }
    };
    if (buildPreview_.ringR > 0.0f && buildPreview_.ringTone > 0) {
        for (const auto& o : buildPreview_.otherRings) ring(o.first, o.second, buildPreview_.ringR, RGBA{ 0.95f, 0.95f, 1.0f, 0.85f });
        const RGBA tone = buildPreview_.ringTone == 1 ? RGBA{ 0.25f, 1.0f, 0.35f, 0.85f }
                        : buildPreview_.ringTone == 2 ? RGBA{ 1.0f, 0.2f, 0.2f, 0.85f }
                                                      : RGBA{ 0.95f, 0.95f, 1.0f, 0.85f };
        ring(buildPreview_.x, buildPreview_.z, buildPreview_.ringR, tone);
    }
    if (b.v.empty()) return;

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, b.v.size() * sizeof(Vertex), b.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b.v.size());
}

void Renderer::drawSmallButton(const Rect& r, const std::string& label, bool enabled, bool hovered) {
    const float s = uiH_ / 720.0f;
    const bool lit = enabled && hovered;
    if (lit) noteHover(r);
    if (!enabled) drawRect(r, theme::alpha(theme::NIGHT, 0.6f));
    else if (lit) drawRectGradientH(r, rgb255(20, 30, 0, 0.85f), rgb255(45, 66, 0, 0.95f));
    else drawRectGradientH(r, rgb255(18, 14, 26, 0.85f), rgb255(10, 8, 16, 0.95f));
    drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, lit ? theme::GREEN : theme::alpha(theme::ASH, enabled ? 0.45f : 0.15f));
    const RGBA fg = !enabled ? theme::alpha(theme::ASH, 0.5f) : lit ? theme::BONE : rgb255(204, 208, 218, 1.0f);
    const float tw = listTextWidth(label), th = listTextHeight(label);
    drawListText(label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, fg);
}

void Renderer::drawBehaviorSection(const SidebarLayout& L, const std::vector<BehaviorRule>& rules, const std::string& task,
                                   float mx, float my, bool editable, const std::array<bool, 4>& modes) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    const float bgAlpha = 0.5f;
    const float listH = listTextHeight("Ag");
    const Rect& P = L.behaviorPanel;

    drawRect(P, RGBA{ 0.06f, 0.04f, 0.085f, bgAlpha });
    drawIconText(P.x + 10.0f * s, P.y + 6.0f * s, listTextHeight("Ag"), ICON_BEHAVIOR, lang("UILABEL_BEHAVIOR"), white);
    const std::string count = std::to_string(rules.size()) + " / " + std::to_string(BEHAVIOR_LIMIT);
    const float countX = P.x + P.w - 10.0f * s - listTextWidth(count);
    drawListText(count, countX, P.y + 6.0f * s, gray);
    // The unit's task, such as Mining, centred between the heading and the count
    {
        const float left = P.x + 10.0f * s + listTextHeight("Ag") + 6.0f * s + listTextWidth(lang("UILABEL_BEHAVIOR")) + 12.0f * s;
        const float room = countX - 12.0f * s - left;
        const std::string full = langf("UILABEL_TASK", { task });
        const std::string shown = room > 0.0f ? fitText(full, room, false) : std::string();
        if (!shown.empty()) {
            const Rect area{ left, P.y + 6.0f * s, room, listTextHeight("Ag") };
            if (shown != full) noteTip(full, area);
            drawListText(shown, left + (room - listTextWidth(shown)) * 0.5f, P.y + 6.0f * s, RGBA{ 1.0f, 0.86f, 0.55f, 1.0f });
        }
    }

    // The modes: a lit button for each one that is on. Ranged follows the weapon, so it is never pressed. Each tooltip
    // says what its mode does.
    {
        static const char* const names[4] = { "UILABEL_MODE_PASSIVE", "UILABEL_MODE_BLOCK", "UILABEL_MODE_SNEAK", "UILABEL_MODE_RANGED" };
        static const char* const tips[4] = { "UILABEL_MODE_PASSIVE_TIP", "UILABEL_MODE_BLOCK_TIP", "UILABEL_MODE_SNEAK_TIP", "UILABEL_MODE_RANGED_TIP" };
        for (int i = 0; i < 4; ++i) {
            const Rect& r = L.modes[(size_t)i];
            if (r.w <= 0.0f) continue;
            const bool pressable = editable && i < 3;
            const bool hot = pressable && r.contains(mx, my);
            if (hot) noteHover(r);
            const RGBA fill = modes[(size_t)i] ? RGBA{ 0.176f, 0.259f, 0.0f, 0.95f }
                                               : RGBA{ 0.10f, 0.07f, 0.13f, hot ? 1.0f : 0.85f };
            drawRect(r, fill);
            if (modes[(size_t)i]) drawRect(Rect{ r.x, r.y + r.h - 2.0f, r.w, 2.0f }, theme::GREEN);
            if (hot) drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.08f });
            const std::string label = fitText(lang(names[i]), r.w - 6.0f * s, false);
            const bool dim = i == 3;
            const RGBA tint = modes[(size_t)i] ? white : (dim ? RGBA{ 0.55f, 0.52f, 0.60f, 1.0f } : gray);
            drawListText(label, r.x + (r.w - listTextWidth(label)) * 0.5f, r.y + (r.h - listH) * 0.5f, tint);
            noteTip(lang(tips[i]), r);
        }
    }

    const std::string heads[2] = { lang("UILABEL_BEHAVIOR_ON"), lang("UILABEL_BEHAVIOR") };
    for (size_t i = 0; i < 2; ++i) {
        const Rect& h = L.behaviorHeads[i];
        drawListText(fitText(heads[i], h.w, false), h.x, h.y + (h.h - listH) * 0.5f, gray);
    }

    for (size_t i = 0; i < L.behaviorRows.size() && i < rules.size(); ++i) {
        const BehaviorRowLayout& R = L.behaviorRows[i];
        const BehaviorRule& r = rules[i];
        if (editable && R.row.contains(mx, my)) drawRect(R.row, RGBA{ 1.0f, 1.0f, 1.0f, 0.06f });
        // A behavior that is off reads gray, so the list shows at a glance which ones work
        const RGBA text = r.on ? white : gray;

        // The on box: a dark square, with a blue fill while the rule is on
        drawRect(Rect{ R.toggle.x - 1.0f, R.toggle.y - 1.0f, R.toggle.w + 2.0f, R.toggle.h + 2.0f },
                 RGBA{ 0.36f, 0.22f, 0.36f, bgAlpha });
        drawRect(R.toggle, RGBA{ 0.10f, 0.07f, 0.12f, bgAlpha });
        if (r.on) {
            const float inset = 3.0f * s;
            drawRect(Rect{ R.toggle.x + inset, R.toggle.y + inset,
                           R.toggle.w - 2.0f * inset, R.toggle.h - 2.0f * inset },
                     RGBA{ 0.35f, 0.62f, 0.95f, editable ? 1.0f : 0.5f });
        }

        // The behavior's name, fitted to its cell. A behavior waiting after a try that could not be done says so.
        drawRect(R.label, RGBA{ 0.10f, 0.07f, 0.12f, 0.90f });
        std::string label = behaviorName(r);
        if (r.on && r.cooldown > 0.0f) label += "  " + lang("UILABEL_BEHAVIOR_WAITING");
        drawListText(fitText(label, R.label.w - 8.0f * s, false), R.label.x + 4.0f * s, R.label.y + (R.label.h - listH) * 0.5f,
                     text);

        // Eat, Heal and Rest have a # button, which asks for the percent they act below
        if (hasPercent(r.kind)) {
            drawSmallButton(R.number, "#", editable, editable && R.number.contains(mx, my));
            if (editable) {
                const PercentRange range = percentRange(r.kind);
                noteTip(langf("UILABEL_BEHAVIOR_NUMBER_TIP", { std::to_string(range.lo), std::to_string(range.hi) }), R.number);
            }
        }
        // Any behavior can be deleted: Add Behavior puts it back
        const bool upOn = editable && i > 0;
        const bool downOn = editable && i + 1 < rules.size();
        const bool removeOn = editable;
        drawSmallButton(R.up, "^", upOn, upOn && R.up.contains(mx, my));
        drawSmallButton(R.down, "v", downOn, downOn && R.down.contains(mx, my));
        drawSmallButton(R.remove, "x", removeOn, removeOn && R.remove.contains(mx, my));

    }

    // + Add Behavior, lit while its list is open, then the list: a behavior the unit holds already reads gray and
    // says so, and picking it only switches it on
    if (L.behaviorAdd.w > 0.0f) {
        const bool full = rules.size() >= (size_t)BEHAVIOR_LIMIT;
        const bool open = !L.behaviorOptions.empty();
        const std::string label = (open ? "- " : "+ ") + lang("UILABEL_BEHAVIOR_ADD");
        drawSmallButton(L.behaviorAdd, label, editable, editable && L.behaviorAdd.contains(mx, my));
        if (open) drawRect(Rect{ L.behaviorAdd.x, L.behaviorAdd.y + L.behaviorAdd.h - 2.0f * s, L.behaviorAdd.w, 2.0f * s }, theme::GREEN);
        for (size_t i = 0; i < L.behaviorOptions.size() && i < L.behaviorOptionInfo.size(); ++i) {
            const Rect& o = L.behaviorOptions[i];
            const BehaviorOption& opt = L.behaviorOptionInfo[i];
            const bool usable = editable && (opt.have || !full);
            const bool hot = usable && o.contains(mx, my);
            if (hot) noteHover(o);
            drawRect(o, hot ? RGBA{ 0.18f, 0.26f, 0.06f, 0.95f } : RGBA{ 0.10f, 0.07f, 0.12f, 0.90f });
            drawRect(Rect{ o.x, o.y, 2.0f * s, o.h }, hot ? theme::GREEN : theme::alpha(theme::ASH, 0.45f));
            const std::string tag = opt.have ? lang("UILABEL_BEHAVIOR_HAVE") : (full ? lang("UILABEL_BEHAVIOR_FULL") : std::string());
            const float tagW = tag.empty() ? 0.0f : listTextWidth(tag) + 8.0f * s;
            drawListText(fitText(opt.label, o.w - 10.0f * s - tagW, false), o.x + 6.0f * s, o.y + (o.h - listH) * 0.5f,
                         opt.have || !usable ? gray : white);
            if (!tag.empty()) drawListText(tag, o.x + o.w - tagW, o.y + (o.h - listH) * 0.5f, theme::alpha(theme::ASH, 0.8f));
            noteTip(describeBehavior(opt.rule), o);
        }
    }
}

void Renderer::drawPercentPrompt(const PercentPromptLayout& P, const std::string& title, const std::string& typed,
                                 const std::string& range, const std::string& error, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const float listH = listTextHeight("Ag");
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.72f, 0.72f, 0.76f, 1.0f };
    // The rest of the window dims behind it, as it takes every click until it is answered
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, (float)uiH_ }, RGBA{ 0.0f, 0.0f, 0.0f, 0.45f });
    drawRect(Rect{ P.box.x - 1.0f, P.box.y - 1.0f, P.box.w + 2.0f, P.box.h + 2.0f }, theme::alpha(theme::ASH, 0.6f));
    drawRect(P.box, RGBA{ 0.07f, 0.05f, 0.10f, 0.97f });
    drawRect(Rect{ P.box.x, P.box.y, P.box.w, 2.0f * s }, theme::GREEN);
    const std::string t = fitText(title, P.box.w - 20.0f * s, false);
    drawListText(t, P.box.x + (P.box.w - listTextWidth(t)) * 0.5f, P.titleY, white);
    // The box: what is typed, a blinking caret, and the % sign
    const Rect& in = P.input;
    drawRect(Rect{ in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f }, theme::GREEN);
    drawRect(in, RGBA{ 0.03f, 0.02f, 0.05f, 1.0f });
    std::string shown = typed;
    shown += (SDL_GetTicks() / 500) % 2 == 0 ? "|" : " ";
    shown += "%";
    drawListText(shown, in.x + (in.w - listTextWidth(shown)) * 0.5f, in.y + (in.h - listH) * 0.5f, white);
    drawListText(range, P.box.x + (P.box.w - listTextWidth(range)) * 0.5f, P.rangeY, gray);
    if (!error.empty()) {
        const std::string e = fitText(error, P.box.w - 20.0f * s, false);
        drawListText(e, P.box.x + (P.box.w - listTextWidth(e)) * 0.5f, P.errorY, RGBA{ 1.0f, 0.45f, 0.40f, 1.0f });
    }
    drawSmallButton(P.ok, lang("UILABEL_BEHAVIOR_PROMPT_SET"), true, P.ok.contains(mx, my));
    drawSmallButton(P.cancel, lang("UILABEL_CANCEL"), true, P.cancel.contains(mx, my));
}

void Renderer::drawBehaviorTip(const SidebarLayout& L, World& w, Entity unit, float mx, float my) {
    const BehaviorHit hit = behaviorHitAt(L, mx, my);
    const Behaviors* b = w.get<Behaviors>(unit);
    if (hit.row < 0 || !b || hit.row >= (int)b->rules.size()) return;
    const float s = uiH_ / 720.0f;
    // What it does, then why it is waiting, when it is
    const BehaviorRule& r = b->rules[(size_t)hit.row];
    std::vector<TipRow> rows(1);
    rows[0].text = describeBehavior(r);
    const std::string why = r.on ? waitReason(r, b->seek != NULL_ENTITY ? nameOf(w, b->seek) : std::string()) : std::string();
    if (!why.empty()) {
        rows.push_back(TipRow{});
        rows.back().text = langf("UILABEL_BEHAVIOR_WAIT_TIP", { why });
    }
    const float width = tipWidth(rows), height = tipHeight(rows.size());
    // Beside the mouse, moved to the other side or up when the window has no room for it
    float x = mx + 14.0f * s, y = my + 14.0f * s;
    if (x + width > uiW_) x = mx - 14.0f * s - width;
    if (y + height > uiH_) y = uiH_ - height;
    drawTip(rows, std::max(0.0f, x), std::max(0.0f, y), width);
}

void Renderer::drawDraggedStack(float mx, float my, int item, int count) {
    const float s = uiH_ / 720.0f;
    const float sz = ITEM_PICTURE_PX;
    const Rect r{ std::floor(mx - sz * 0.5f + 0.5f), std::floor(my - sz * 0.5f + 0.5f), sz, sz };
    drawItemPicture(r, item, 0.85f);
    if (count > 1) {
        const std::string n = std::to_string(count);
        drawListText(n, r.x + r.w - listTextWidth(n) - 3.0f * s, r.y + r.h - listTextHeight(n) - 1.0f * s, WHITE);
    }
    const std::string name = itemName(item);
    drawListText(name, r.x + r.w + 6.0f * s, my - listTextHeight(name) * 0.5f, WHITE);
}

void Renderer::drawGroundItems(World& w, const Terrain& t, const Camera& cam) {
    // Each item stands on its ground point, turned to the camera, half as tall as a unit
    for (Entity e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const GroundItem* gi = w.get<GroundItem>(e);
        const TextTex* pic = itemTexture(gi->item);
        TextTex square = *pic;
        square.aspect = 1.0f;   // item pictures are square, whatever size the file is
        drawGlyph(square, tr->x, tr->y, tr->z, ITEM_GLYPH_H, WHITE);
    }
}

const TextTex* Renderer::itemTexture(int item) {
    auto it = itemPics_.find(item);
    if (it == itemPics_.end()) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/item/item_%d.png", item);
        it = itemPics_.emplace(item, loadOptionalImage(path)).first;
    }
    if (it->second.tex) return &it->second;
    return itemNone_.tex ? &itemNone_ : &pink_;
}

// The alpha of a picture in the pak, row by row; empty if it is not there
Renderer::ItemMask Renderer::loadMask(const std::string& path) {
    ItemMask m;
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(path, bytes, size)) return m;
    SDL_Surface* s = IMG_Load_IO(SDL_IOFromConstMem(bytes, size), true);
    if (!s) return m;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (!conv) return m;
    m.w = conv->w;
    m.h = conv->h;
    m.alpha.resize((size_t)m.w * (size_t)m.h);
    for (int y = 0; y < m.h; ++y) {
        const unsigned char* row = static_cast<const unsigned char*>(conv->pixels) + (size_t)y * (size_t)conv->pitch;
        for (int x = 0; x < m.w; ++x) m.alpha[(size_t)y * (size_t)m.w + (size_t)x] = row[(size_t)x * 4 + 3];
    }
    SDL_DestroySurface(conv);
    return m;
}

const Renderer::ItemMask& Renderer::itemMask(int item) {
    auto it = itemMasks_.find(item);
    if (it != itemMasks_.end()) return it->second;
    ItemMask m = loadMask("data/item/item_" + std::to_string(item) + ".png");
    if (m.alpha.empty() && item != 0) m = itemMask(0);   // the stand-in picture, or a solid square without it
    return itemMasks_.emplace(item, std::move(m)).first->second;
}

bool Renderer::itemPixelSolid(int item, float u, float v) {
    const ItemMask& m = itemMask(item);
    if (m.alpha.empty()) return true;   // no picture at all: the pink square, solid all over
    const int x = std::clamp((int)(u * (float)m.w), 0, m.w - 1);
    const int y = std::clamp((int)(v * (float)m.h), 0, m.h - 1);
    return m.alpha[(size_t)y * (size_t)m.w + (size_t)x] >= 26;   // as the glyph shader: under 0.1 is not drawn
}

void Renderer::drawGroundItemName(World& w, const Camera& cam, int winW, int winH, Entity item) {
    const GroundItem* gi = w.get<GroundItem>(item);
    const Transform* tr = w.get<Transform>(item);
    if (!gi || !tr) return;
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    ItemOnScreen os;
    if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) return;
    const std::string name = gi->count > 1 ? std::to_string(gi->count) + "x " + itemName(gi->item) : itemName(gi->item);
    const float padX = 7.0f * s, padY = 3.0f * s, lineH = listTextHeight("Ag");
    const float boxW = listTextWidth(name) + 2.0f * padX, boxH = lineH + 2.0f * padY;
    const Rect box{ std::floor(os.cx - boxW * 0.5f), std::floor(os.top - 4.0f * s - boxH), boxW, boxH };
    drawRect(Rect{ box.x - 1.0f, box.y - 1.0f, box.w + 2.0f, box.h + 2.0f }, LABEL_EDGE);
    drawRect(box, LABEL_HOT_FILL);
    drawListText(name, box.x + padX, box.y + padY, LABEL_HOT_TEXT);
}

// An emote's picture from data/emote (emote_<n>.png), loaded once. Without one, emote_0.png, and the pink square when that
// is missing too.
const TextTex* Renderer::emoteTexture(int id) {
    auto it = emotePics_.find(id);
    if (it == emotePics_.end()) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/emote/emote_%d.png", id);
        it = emotePics_.emplace(id, loadOptionalImage(path)).first;
    }
    if (it->second.tex) return &it->second;
    return emoteFallback_.tex ? &emoteFallback_ : &pink_;
}

const Model& Renderer::propModel(int kind) const {
    const Model& own = propModels_[(size_t)kind];
    return own.vao ? own : fallbackModel_;
}

void Renderer::drawEmotes(World& w, const Camera& cam, int winW, int winH, bool playerNames, bool otherNames) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float size = 28.0f * s;
    const unsigned long long ticks = (unsigned long long)SDL_GetTicks();
    const unsigned long long turn = ticks / EMOTE_CYCLE_MS;
    // The hop: up and back down once, at the start of every EMOTE_HOP_EVERY_MS. Every emote hops together.
    const unsigned long long sinceHop = ticks % EMOTE_HOP_EVERY_MS;
    const float hop = sinceHop < EMOTE_HOP_MS
        ? std::sin(3.14159265f * (float)sinceHop / (float)EMOTE_HOP_MS) * EMOTE_HOP_HEIGHT * s : 0.0f;
    for (Entity u : w.view<Unit, Transform>()) {
        const std::vector<int> emotes = emotesOf(w, u);
        if (emotes.empty()) continue;
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // Above the head, where the name tag is, and above the tag when names are shown
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        float bottom = sy - 2.0f * s;
        const bool named = w.has<Name>(u) && (w.has<Commandable>(u) ? playerNames : otherNames);
        if (named) bottom -= listTextHeight("Ag") + 2.0f * s;
        if (sx < -size || sx > winW + size || bottom < 0.0f || bottom - size > winH) continue;
        const TextTex* pic = emoteTexture(emotes[(size_t)(turn % emotes.size())]);
        if (!pic) continue;
        drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size - hop, size, size }, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

void Renderer::drawPropEmotes(World& w, const Camera& cam, int winW, int winH, bool propNames) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float size = 28.0f * s;
    const unsigned long long sinceHop = (unsigned long long)SDL_GetTicks() % EMOTE_HOP_EVERY_MS;
    const float hop = sinceHop < EMOTE_HOP_MS
        ? std::sin(3.14159265f * (float)sinceHop / (float)EMOTE_HOP_MS) * EMOTE_HOP_HEIGHT * s : 0.0f;
    std::vector<Claim> claims;
    bool haveClaims = false;
    for (Entity e : w.view<Prop, Transform>()) {
        const int kind = w.get<Prop>(e)->kind;
        if (kind != PROP_VAULT && kind != PROP_FLAG) continue;
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        int emote = EMOTE_NONE;
        if (kind == PROP_VAULT && !vaultHasUpkeep(w, e)) emote = EMOTE_NO_UPKEEP;
        if (kind == PROP_FLAG) {
            if (!haveClaims) {
                claims = claimsOf(w);
                haveClaims = true;
            }
            if (propDecaying(w, e, claims)) emote = EMOTE_DECAYING;
        }
        if (emote == EMOTE_NONE) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, propBaseY(w, e) + propTop(kind) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        float bottom = sy - 2.0f * s;
        if (propNames) bottom -= listTextHeight("Ag") + 2.0f * s;
        if (sx < -size || sx > winW + size || bottom < 0.0f || bottom - size > winH) continue;
        if (const TextTex* pic = emoteTexture(emote)) {
            drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size - hop, size, size }, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }
}

void Renderer::drawHoverCue(World& w, const Camera& cam, int winW, int winH, Entity u, int emote, bool namesShown) {
    const Transform* tr = w.get<Transform>(u);
    if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) return;
    const float s = winH / 720.0f;
    const float size = 28.0f * s;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    float sx = 0.0f, sy = 0.0f;
    if (!projectPoint(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) return;
    // Above its name, and above its own emote if it shows one, so both can be seen
    float bottom = sy - 2.0f * s;
    if (namesShown && w.has<Name>(u)) bottom -= listTextHeight("Ag") + 2.0f * s;
    if (!emotesOf(w, u).empty()) bottom -= size + 4.0f * s;
    const TextTex* pic = emoteTexture(emote);
    if (pic) drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size, size, size }, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

// The UI icon with this id: its picture from data/icon (icon_<n>.png), loaded once. An id with no picture uses
// icon_0.png; with neither, the texture is empty and the pink placeholder (with the id) is drawn.
const TextTex& Renderer::uiIconTexture(int icon) {
    auto it = uiIcons_.find(icon);
    if (it == uiIcons_.end()) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/icon/icon_%d.png", icon);
        it = uiIcons_.emplace(icon, loadOptionalImage(path)).first;
    }
    return it->second.tex ? it->second : iconFallback_;
}

void Renderer::drawUiIcon(int icon, const Rect& r) {
    const TextTex& t = uiIconTexture(icon);
    if (t.tex) {
        drawFitted(t, r, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        return;
    }
    // The placeholder until the picture is packed: a pink square with its id in it
    drawRect(r, RGBA{ 1.0f, 0.42f, 0.78f, 1.0f });
    const std::string num = std::to_string(icon);
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
    // A panel's bar: deep pink fading to near black, with a hot pink line along its foot
    drawRectGradientH(bar, theme::alpha(theme::PINK_DEEP, 0.95f), theme::alpha(theme::PINK_INK, 0.92f));
    drawRect(Rect{ bar.x, bar.y + bar.h - std::max(1.0f, 1.0f * s), bar.w, std::max(1.0f, 1.0f * s) }, theme::alpha(theme::PINK, 0.85f));
    if (icon == ICON_NONE) {
        drawListText(title, bar.x + 6.0f * s, bar.y + (bar.h - listTextHeight("Ag")) * 0.5f, white);
        return;
    }
    drawIconText(bar.x + 6.0f * s, bar.y + 3.0f * s, std::max(0.0f, bar.h - 6.0f * s), icon, title, white);
}

void Renderer::drawCollapseButton(const Rect& r, bool collapsed) {
    if (r.contains(mouseX_, mouseY_)) noteHover(r);
    drawRect(r, theme::alpha(theme::PINK, 0.9f));
    drawRect(Rect{ r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f }, theme::alpha(theme::PINK_INK, 0.95f));
    const char* sign = collapsed ? "+" : "-";
    const float tw = listTextWidth(sign), th = listTextHeight("Ag");
    drawListText(sign, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

void Renderer::drawNotice(const std::string& text, int winW, int winH) {
    if (text.empty()) return;
    const float s = winH / 720.0f;
    const float pad = 12.0f * s, top = 14.0f * s;
    const float w = uiTextWidth(text) + 2.0f * pad;
    const float h = uiTextHeight("Ag") + pad;
    const Rect box{ (winW - w) * 0.5f, top, w, h };
    // A dark band with a green edge on its left, like a lit menu item
    drawRectGradientH(box, rgb255(20, 30, 0, 0.85f), theme::alpha(theme::NIGHT, 0.85f));
    drawRect(Rect{ box.x, box.y, 3.0f * s, box.h }, theme::GREEN);
    drawUiText(text, box.x + pad, top + pad * 0.5f, theme::BONE);
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
    const RGBA heading{ 0.624f, 0.902f, 0.0f, 1.0f };
    const float bgAlpha = 0.5f;
    const float rowH = fontTextHeight(statsRow_, "Ag") + 1.0f * s;
    const float pad = L.left.x - L.box.x;
    const float iconH = fontTextHeight(statsRow_, "Ag");
    const float nameIndent = iconH + 4.0f * s;   // the stat names sit after their icons

    drawRect(L.box, RGBA{ 0.04f, 0.03f, 0.07f, bgAlpha });
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
    drawRect(L.close, hot ? RGBA{ 0.176f, 0.259f, 0.0f, bgAlpha } : RGBA{ 0.07f, 0.055f, 0.10f, bgAlpha });
    const float tw = fontTextWidth(statsRow_, lang("UILABEL_CLOSE")), th = fontTextHeight(statsRow_, lang("UILABEL_CLOSE"));
    drawFontText(statsRow_, lang("UILABEL_CLOSE"), L.close.x + (L.close.w - tw) * 0.5f, L.close.y + (L.close.h - th) * 0.5f, white);
}

ItemMenuLayout Renderer::itemMenuLayout(float x, float y, int buttonCount, int winW, int winH) {
    return layoutItemMenu(x, y, buttonCount, listTextHeight("Ag"), winW, winH);
}

void Renderer::drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my) {
    const float s = uiH_ / 720.0f;
    drawRect(L.box, theme::alpha(theme::PINK, 0.8f));
    drawRect(Rect{ L.box.x + 1.0f, L.box.y + 1.0f, L.box.w - 2.0f, L.box.h - 2.0f }, theme::alpha(theme::NIGHT, 0.92f));
    for (size_t i = 0; i < L.buttons.size() && i < labels.size(); ++i) {
        const Rect& r = L.buttons[i];
        const bool hot = r.contains(mx, my);
        if (hot) noteHover(r);
        if (hot) drawRectGradientH(r, rgb255(20, 30, 0, 0.85f), rgb255(45, 66, 0, 0.95f));
        drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, hot ? theme::GREEN : theme::alpha(theme::ASH, 0.35f));
        const float tw = listTextWidth(labels[i]), th = listTextHeight(labels[i]);
        drawListText(labels[i], r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, hot ? theme::BONE : rgb255(204, 208, 218, 1.0f));
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
        if (seen.dist2 > LOD_MID_TILES * LOD_MID_TILES || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, seen.os)) continue;
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
    drawFitted(unitPicture(sprite), r, WHITE);
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

void Renderer::drawUnitNames(World& w, const Camera& cam, int winW, int winH, bool players, bool others) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA player{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA hostile{ 1.0f, 0.55f, 0.50f, 1.0f };
    const RGBA neutral{ 0.6f, 0.6f, 0.6f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    for (Entity u : w.view<Unit, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        const Name* nm = w.get<Name>(u);
        if (!(w.has<Commandable>(u) ? players : others)) continue;
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // The tag sits just above the top of the unit's picture
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) continue;

        const std::string tag = "[" + nm->text + "]";
        const float tw = listTextWidth(tag), th = listTextHeight(tag);
        if (sx < -tw || sx > winW + tw || sy < -th || sy > winH + th) continue;
        const float x = sx - tw * 0.5f, y = sy - th;
        // The drop shadow is the same text in black, a little down and to the right
        drawListText(tag, x + 1.5f * s, y + 1.5f * s, shadow);
        drawListText(tag, x, y, w.has<Hostile>(u) ? hostile : w.has<Neutral>(u) ? neutral : player);
    }
}

void Renderer::drawPropNames(World& w, const Camera& cam, int winW, int winH, bool showInfo) {
    // Stacked as the Alt item names are, without their box: the props that share a spot (a foundation and what stands
    // on it, or props on one tile) share one block of lines over them, each prop's name in purple with its info lines
    // (Show Prop Info) under it in a paler purple, the lines left-aligned, the last prop nearest the props. Blocks are
    // drawn farthest first, so a nearer one is drawn over the ones behind it.
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA purple{ 0.80f, 0.56f, 1.0f, 1.0f };
    const RGBA pale{ 0.88f, 0.82f, 0.96f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    const float lineGap = 2.0f * s;
    const float lineH = listTextHeight("Ag");
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    propLabelTop_.clear();

    struct Seen { Entity e; float sx, sy, dist2; bool base; };
    // The group a prop joins: the foundation it stands on (or is), otherwise its tile
    std::map<std::pair<int, long long>, std::vector<Seen>> groups;
    for (Entity e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const int kind = w.get<Prop>(e)->kind;
        if (kind < 0 || kind >= PROP_KIND_COUNT) continue;
        // Just above the top of the prop, as a unit's tag is above its picture
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, propBaseY(w, e) + propTop(kind) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        const float dx = tr->x - ex, dy = tr->y - ey, dz = tr->z - ez;
        const bool base = propInfo(kind).foot.foundation;
        std::pair<int, long long> key{ 1, ((long long)std::floor(tr->x) << 32) ^ (long long)(unsigned)(int)std::floor(tr->z) };
        if (base) key = { 0, (long long)e };
        else if (const OnBase* ob = w.get<OnBase>(e); ob && w.has<Prop>(ob->foundation)) key = { 0, (long long)ob->foundation };
        groups[key].push_back(Seen{ e, sx, sy, dx * dx + dy * dy + dz * dz, base });
    }

    struct Block {
        float x = 0.0f, y = 0.0f, dist2 = 0.0f;
        std::vector<std::pair<std::string, RGBA>> lines;
    };
    std::vector<Block> blocks;
    for (auto& entry : groups) {
        std::vector<Seen>& seen = entry.second;
        // Entity order, so the names keep their places from frame to frame; the foundation last, nearest the props
        std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) {
            if (a.base != b.base) return !a.base;
            return a.e < b.e;
        });
        Block b;
        float cx = 0.0f, top = 0.0f, maxW = 0.0f;
        b.dist2 = seen[0].dist2;
        for (size_t i = 0; i < seen.size(); ++i) {
            const Seen& sn = seen[i];
            cx += sn.sx;
            top = i == 0 ? sn.sy : std::min(top, sn.sy);
            b.dist2 = std::min(b.dist2, sn.dist2);
            b.lines.push_back({ "[" + lang(propInfo(w.get<Prop>(sn.e)->kind).nameKey) + "]", purple });
            if (showInfo) {
                for (const std::string& line : propInfoLines(w, sn.e)) b.lines.push_back({ line, pale });
            }
        }
        for (const auto& line : b.lines) maxW = std::max(maxW, listTextWidth(line.first));
        cx /= (float)seen.size();
        const float n = (float)b.lines.size();
        const float blockH = n * lineH + (n - 1.0f) * lineGap;
        b.x = cx - maxW * 0.5f;
        b.y = top - blockH;
        if (b.x + maxW < 0.0f || b.x > winW || b.y + blockH < 0.0f || b.y > winH) continue;
        for (const Seen& sn : seen) propLabelTop_[sn.e] = b.y;
        blocks.push_back(std::move(b));
    }
    std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.dist2 > b.dist2; });
    for (const Block& b : blocks) {
        for (size_t i = 0; i < b.lines.size(); ++i) {
            const float y = b.y + (float)i * (lineH + lineGap);
            drawListText(b.lines[i].first, b.x + 1.5f * s, y + 1.5f * s, shadow);
            drawListText(b.lines[i].first, b.x, y, b.lines[i].second);
        }
    }
}

float Renderer::propBaseY(World& w, Entity e) const {
    const Transform* tr = w.get<Transform>(e);
    float y = tr ? tr->y : 0.0f;
    if (const OnBase* ob = w.get<OnBase>(e)) {
        const Transform* ft = w.get<Transform>(ob->foundation);
        const Prop* fp = w.get<Prop>(ob->foundation);
        if (ft && fp && fp->kind >= 0 && fp->kind < PROP_KIND_COUNT) {
            // The foundation's top, held to a slab's height in case its model is missing (the pink stand-in is tall)
            const float top = std::clamp(propTop(fp->kind), 0.0f, 0.6f);
            y = std::max(y, ft->y + top);
        }
    }
    return y;
}

float Renderer::propTop(int kind) const {
    float top = missingPropSide(kind);   // the pink stand-in's height, if the kind has no model
    if (kind >= 0 && kind < PROP_KIND_COUNT) {
        const Model& m = propModel(kind);
        const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
        if (m.vao && extent > 0.0f) top = (m.maxY - m.minY) * propInfo(kind).size / extent;
    }
    return top;
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
    // A unit with several labels at once (an item's Health and Hunger) shows them stacked, the newest on top
    std::map<Entity, int> stacked;
    for (const TradeLabel& l : labels) {
        const Transform* tr = w.get<Transform>(l.unit);
        if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!projectPoint(vp, tr->x, tr->y + GLYPH_H * 2.2f, tr->z, winW, winH, sx, sy)) continue;
        const int row = stacked[l.unit]++;
        const float t = std::clamp(l.age / TRADE_LABEL_LIFE, 0.0f, 1.0f);
        const float alpha = t < 0.7f ? 1.0f : (1.0f - t) / 0.3f;
        const float lineH = uiTextHeight("Ag") + 2.0f * s;
        const float x = sx - uiTextWidth(l.text) * 0.5f, y = sy - uiTextHeight(l.text) - row * lineH;
        drawUiText(l.text, x + 1.5f * s, y + 1.5f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f * alpha });
        drawUiText(l.text, x, y, l.red ? RGBA{ 1.0f, 0.35f, 0.32f, alpha } : RGBA{ 1.0f, 0.90f, 0.45f, alpha });
    }
}

// The rows of the Game tab's filter menu, in order. A heading names a group of kinds and is not clickable.
struct FilterEntry {
    const char* label;     // a language key, looked up when the menu is drawn
    LogKind kind;
    bool heading;
};
static const FilterEntry kFilterEntries[] = {
    { "UILABEL_NPC_MESSAGES", LogKind::Other, true },
    { "UILABEL_SHOW_NPC_INFO", LogKind::Npc, false },
    { "UILABEL_COMBAT_MESSAGES", LogKind::Other, true },
    { "UILABEL_SEEN", LogKind::Seen, false },
    { "UILABEL_HIT", LogKind::Hit, false },
    { "UILABEL_MISS", LogKind::Miss, false },
    { "UILABEL_DAMAGE", LogKind::Damage, false },
    { "UILABEL_EXPERIENCE_MESSAGES", LogKind::Other, true },
    { "UILABEL_EXPERIENCE", LogKind::Xp, false },
    { "UILABEL_XP_ATHLETICS", LogKind::XpAthletics, false },
    { "UILABEL_XP_ENDURANCE", LogKind::XpEndurance, false },
    { "UILABEL_XP_CARRYING", LogKind::XpCarrying, false },
    { "UILABEL_ITEM_MESSAGES", LogKind::Other, true },
    { "UILABEL_PICKUP", LogKind::Pickup, false },
    { "UILABEL_DROP", LogKind::Drop, false },
    { "UILABEL_LOOT", LogKind::Loot, false },
};
constexpr int kFilterRowCount = (int)(sizeof(kFilterEntries) / sizeof(kFilterEntries[0]));

CombatLogLayout Renderer::combatLogLayout(int winW, int winH, int tab, bool filterOpen, int offset, int lineCount,
                                          bool collapsed, const PanelPlace& place) {
    const float s = winH / 720.0f;
    const float pad = 8.0f * s, scrollW = 10.0f * s, gap = 4.0f * s;
    const float barH = PANEL_BAR_H * s, tabH = 20.0f * s;
    const float lineH = listTextHeight("Ag") + 2.0f * s;
    // Its open size and place: the top left corner by default. As many lines as its height has room for, under the tabs.
    const Rect open = panelRect(PANEL_INFO, place, winW, winH);
    const float bodyH = open.h;
    const int linesVisible = std::max(1, (int)std::floor((bodyH - barH - tabH - 2.0f * pad) / lineH));

    CombatLogLayout L;
    L.linesVisible = linesVisible;
    L.lineCount = lineCount;
    L.lineH = lineH;
    L.offset = std::clamp(offset, 0, L.maxOffset());
    L.tab = std::clamp(tab, 0, 2);
    L.filterOpen = filterOpen && L.tab == 1 && !collapsed;

    // Its bar runs along its top or its bottom; folded, it is the bar alone. y and h are the body's: the panel less its bar.
    const float w = open.w;
    L.panel = panelShownRect(open, collapsed, place.barBottom, barH);
    L.bar = panelBarRect(open, place.barBottom, barH);
    const Rect bodyR = panelBodyRect(open, place.barBottom, barH);
    L.bodyTop = bodyR.y;
    L.bodyBottom = bodyR.y + bodyR.h;
    const float x = open.x, y = bodyR.y - barH;   // laid out as if the bar were above the body
    const float h = bodyR.h + barH;
    L.toggle = BarToggle(L.bar, s);
    L.lock = BarLock(L.bar, s);
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
    // Each tab is as wide as its name needs
    auto tabW = [&](const char* key) { return std::max(48.0f * s, listTextWidth(lang(key)) + 16.0f * s); };
    L.tabMessage = Rect{ x + pad * 0.5f, tabY + 2.0f * s, tabW("UILABEL_MESSAGE"), tabH - 2.0f * s };
    L.tabGame = Rect{ L.tabMessage.x + L.tabMessage.w + 4.0f * s, tabY + 2.0f * s, tabW("UILABEL_GAME"), tabH - 2.0f * s };
    L.tabTime = Rect{ L.tabGame.x + L.tabGame.w + 4.0f * s, tabY + 2.0f * s, tabW("UILABEL_TIME"), tabH - 2.0f * s };
    // The Filter button: at the right of the row, but never over the Time tab
    {
        const float tabsEnd = L.tabTime.x + L.tabTime.w + 4.0f * s;
        const float fw = listTextWidth(lang("UILABEL_FILTER")) + 14.0f * s;
        const float fx = std::max(tabsEnd, x + w - pad - scrollW - fw);
        L.filterBtn = Rect{ fx, tabY + 2.0f * s, std::max(0.0f, std::min(fw, x + w - 2.0f * s - fx)), tabH - 2.0f * s };
    }

    // The filter menu opens downward from the Filter button, or upward where the window has no room below it
    if (L.filterOpen) {
        const float rowH = listTextHeight("Ag") + 6.0f * s;
        // As wide as its longest row needs: the indented name, a gap, then On or Off
        float longest = 0.0f;
        for (const FilterEntry& fe : kFilterEntries) {
            longest = std::max(longest, listTextWidth(lang(fe.label)) + (fe.heading ? 6.0f : 16.0f) * s);
        }
        const float stateW = std::max(listTextWidth(lang("UILABEL_ON")), listTextWidth(lang("UILABEL_OFF")));
        const float menuW = std::min((float)winW, std::max({ 160.0f * s, L.filterBtn.w, longest + 20.0f * s + stateW + 10.0f * s }));
        const float menuH = kFilterRowCount * rowH + 8.0f * s;
        const float below = tabY + tabH;
        const float menuY = below + menuH <= winH ? below : std::max(0.0f, tabY - menuH);
        const float menuX = std::clamp(L.filterBtn.x + L.filterBtn.w - menuW, 0.0f, std::max(0.0f, winW - menuW));
        L.filterMenu = Rect{ menuX, menuY, menuW, menuH };
        for (int i = 0; i < kFilterRowCount; ++i) {
            const Rect r{ L.filterMenu.x, L.filterMenu.y + 4.0f * s + i * rowH, menuW, rowH };
            L.filterRows.push_back(CombatLogLayout::FilterRow{ r, kFilterEntries[i].kind, kFilterEntries[i].heading,
                                                               lang(kFilterEntries[i].label) });
        }
    }
    if (collapsed) {
        L.track = L.thumb = Rect{};
        L.tabMessage = L.tabGame = L.tabTime = L.filterBtn = Rect{};
    }
    return L;
}

void Renderer::drawCombatLog(const CombatLogLayout& L, const std::vector<const LogLine*>& rows, const LogFilter& filter,
                             float mx, float my, const InfoTime& time) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA lit{ 0.14f, 0.21f, 0.0f, 0.95f };
    const RGBA dim{ 0.09f, 0.06f, 0.12f, 0.90f };

    drawRect(L.panel, RGBA{ 0.05f, 0.035f, 0.08f, 0.86f });
    drawTitleBar(L.bar, ICON_COMBAT_LOG, lang("UILABEL_INFORMATION_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    // The two tabs: the one shown is lit
    auto tab = [&](const Rect& r, const std::string& label, bool selected) {
        if (r.contains(mx, my)) noteHover(r);
        drawRect(r, selected ? lit : dim);
        drawListText(label, r.x + (r.w - listTextWidth(label)) * 0.5f, r.y + (r.h - listTextHeight("Ag")) * 0.5f,
                     selected ? white : gray);
    };
    tab(L.tabMessage, lang("UILABEL_MESSAGE"), L.tab == 0);
    tab(L.tabGame, lang("UILABEL_GAME"), L.tab == 1);
    tab(L.tabTime, lang("UILABEL_TIME"), L.tab == 2);
    if (L.tab == 1) drawButton(L.filterBtn, lang("UILABEL_FILTER"), true, L.filterBtn.contains(mx, my) || L.filterOpen);

    // The rows are centred in the space under the tabs
    const float tabH = 20.0f * s;
    const float bodyTop = L.bodyTop + tabH;

    // The Time tab: the time of day large, then the date written out, the date as numbers and the days passed, each
    // line centred, the block centred in the space under the tabs
    if (L.tab == 2) {
        const RGBA gray{ 0.78f, 0.78f, 0.82f, 1.0f };
        const float bigH = uiTextHeight("Ag"), smallH = listTextHeight("Ag"), gap = 4.0f * s;
        const float blockH = bigH + 3.0f * (gap + smallH);
        const float bodyH = L.bodyBottom - bodyTop;
        float y = bodyTop + std::max(0.0f, (bodyH - blockH) * 0.5f);
        const float midX = L.panel.x + L.panel.w * 0.5f, maxW = L.panel.w - 16.0f * s;
        drawClockText(time.clock, midX - clockTextWidth(time.clock) * 0.5f, y, white);
        y += bigH + gap;
        for (const std::string* line : { &time.date, &time.ymd, &time.days }) {
            const std::string t = fitText(*line, maxW, false);
            drawListText(t, midX - listTextWidth(t) * 0.5f, y, line == &time.date ? white : gray);
            y += smallH + gap;
        }
        return;
    }
    const float top = bodyTop + (L.bodyBottom - bodyTop - L.linesVisible * L.lineH) * 0.5f;
    const int count = (int)rows.size();
    for (int k = 0; k < L.shown && L.first + k < count; ++k) {
        const LogLine& ln = *rows[(size_t)(L.first + k)];
        // Bottom-aligned: the newest line is the last row
        const int row = L.linesVisible - L.shown + k;
        drawListText(fitText(ln.text, L.textW, false), L.textX, top + row * L.lineH, logTint(ln.tint));
    }
    if (L.scrollable()) {
        drawRect(L.track, RGBA{ 0.13f, 0.08f, 0.17f, 0.9f });
        drawRect(L.thumb, RGBA{ 0.878f, 0.129f, 0.541f, 0.9f });
    }

    // The filter menu, over the lines. Each kind reads On or Off.
    if (L.filterOpen) {
        drawRect(L.filterMenu, RGBA{ 0.04f, 0.03f, 0.07f, 0.97f });
        const float th = listTextHeight("Ag");
        for (const CombatLogLayout::FilterRow& fr : L.filterRows) {
            if (fr.heading) {
                drawListText(fr.label, fr.r.x + 6.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, RGBA{ 0.624f, 0.902f, 0.0f, 1.0f });
                continue;
            }
            if (fr.r.contains(mx, my)) {
                noteHover(fr.r);
                drawRect(fr.r, RGBA{ 1.0f, 1.0f, 1.0f, 0.10f });
            }
            const bool on = filter.on[(int)fr.kind];
            drawListText(fr.label, fr.r.x + 16.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, white);
            const std::string state = on ? lang("UILABEL_ON") : lang("UILABEL_OFF");
            drawListText(state, fr.r.x + fr.r.w - 10.0f * s - listTextWidth(state), fr.r.y + (fr.r.h - th) * 0.5f,
                         on ? RGBA{ 0.6f, 1.0f, 0.6f, 1.0f } : gray);
        }
    }
}

void Renderer::drawPartySetup(const std::vector<Button>& buttons, const Party& p, const PartyUi& ui,
                              const PartyPanels& P, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.66f, 0.66f, 0.72f, 1.0f };
    const RGBA faint{ 0.45f, 0.45f, 0.50f, 1.0f };
    const RGBA heading{ 0.624f, 0.902f, 0.0f, 1.0f };
    const RGBA hover{ 0.624f, 0.902f, 0.0f, 1.0f };
    const RGBA warn{ 1.0f, 0.72f, 0.55f, 1.0f };
    const RGBA panel{ 0.05f, 0.035f, 0.08f, 0.92f };
    const RGBA plate{ 0.09f, 0.06f, 0.12f, 0.95f };
    const RGBA plateHot{ 0.14f, 0.21f, 0.0f, 0.95f };
    const RGBA plateOn{ 0.176f, 0.259f, 0.0f, 0.95f };    // the purple used for the selected unit
    const RGBA plateOff{ 0.07f, 0.05f, 0.10f, 0.70f };
    const float menuLineH = listTextHeight("Ag");
    float lineH = menuLineH;   // changes to the game's size for the Stats tab's rows
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
    drawRect(P.bottom, RGBA{ 0.03f, 0.02f, 0.045f, 0.95f });
    // The unit list's scroll bar, when it has more units than fit
    const PartyListBar bar = partyListBar((int)uiW_, (int)uiH_, (int)p.size(), ui.listScroll);
    if (bar.track.h > 0.0f) {
        drawRect(bar.track, RGBA{ 0.13f, 0.08f, 0.17f, 0.9f });
        const bool barHot = ui.listDrag || bar.thumb.contains(mx, my);
        drawRect(bar.thumb, barHot ? RGBA{ 1.0f, 0.36f, 0.70f, 1.0f } : RGBA{ 0.878f, 0.129f, 0.541f, 0.9f });
    }

    const bool menuSet = menuText_;
    for (const Button& b : buttons) {
        // The Stats tab's rows (headings, stats and their - and +) are drawn in the game's fonts: there are many of them
        const bool statRow = b.action == Action::PartyStatLabel || b.action == Action::PartyStep
                          || b.action == Action::PartyHeading;
        setMenuText(menuSet && !statRow);
        lineH = statRow ? listTextHeight("Ag") : menuLineH;
        const bool hot = b.enabled && b.rect.contains(mx, my);
        if (hot) noteHover(b.rect);
        switch (b.action) {
        case Action::PartyPick: {
            const PartyMember& u = p[static_cast<size_t>(b.param)];
            const bool chosen = b.param == std::clamp(ui.selected, 0, std::max(0, (int)p.size() - 1));
            drawRect(b.rect, plateFor(hot, chosen, true));
            const float pic = b.rect.h - 14.0f * s;
            const Rect picRect{ b.rect.x + 7.0f * s, b.rect.y + 7.0f * s, pic, pic };
            if (u.mob > 0) drawFitted(mobPicture(u.mob), picRect, WHITE);
            else drawUnitPicture(u.sprite, picRect);
            const float tx = b.rect.x + pic + 16.0f * s, tw = b.rect.x + b.rect.w - tx - 8.0f * s;
            const bool named = !cleanName(u.name).empty();
            const std::string shown = named ? cleanName(u.name) : std::string(lang("UILABEL_NO_NAME"));
            drawListText(fitText(shown, tw, false), tx, b.rect.y + (b.rect.h - lineH) * 0.5f,
                         !named ? warn : chosen ? hover : white);
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
            {
                // The heading is the model label with no number: the word for Model alone
                std::string heading = langf("UILABEL_MODEL_N", { "" });
                while (!heading.empty() && heading.back() == ' ') heading.pop_back();
                drawListText(heading, b.rect.x, b.rect.y - 22.0f * s, gray);
            }
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Sprite, true));
            leftText(b.rect, b.label, white, 10.0f * s);
            break;

        case Action::PartyBodyBox:
            drawListText(lang("UILABEL_BODY_TYPE"), b.rect.x, b.rect.y - 22.0f * s, gray);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::Body, true));
            leftText(b.rect, b.label, white, 10.0f * s);
            break;

        case Action::PartyMobBox:
            drawListText(lang("UILABEL_MOB_KIND"), b.rect.x, b.rect.y - 22.0f * s, gray);
            drawRect(b.rect, plateFor(hot, ui.dropKind == DropKind::MobKind && ui.dropSlot >= 0, true));
            leftText(b.rect, b.label, b.enabled ? white : gray, 10.0f * s);
            break;

        case Action::PartyRandomUnit:
            drawRect(b.rect, plateFor(hot, false, true));
            centerd(b.rect, b.label, white);
            break;

        case Action::PartyAddUnit:
        case Action::PartyRemoveUnit:
        case Action::PartyAddMob:
            drawRect(b.rect, plateFor(hot, false, b.enabled));
            centerd(b.rect, b.label, b.enabled ? white : faint);
            break;

        case Action::PartyGearSlot: {
            const bool open = ui.dropKind == DropKind::Gear && ui.dropSlot == b.param;
            drawRect(b.rect, plateFor(hot, open, true));
            // The item cell on the left, with the item's 64 x 64 picture; the names to its right
            const Rect cell{ b.rect.x + 4.0f, b.rect.y + std::floor((b.rect.h - ITEM_CELL_PX) * 0.5f), ITEM_CELL_PX, ITEM_CELL_PX };
            drawRect(cell, RGBA{ 0.0f, 0.0f, 0.0f, 0.30f });
            const int item = b.param >= 0 && b.param < GEAR_SLOTS ? picked.gear[(size_t)b.param] : 0;
            if (item != 0) drawItemPictureAt(cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f, item, 1.0f);
            const float inset = ITEM_CELL_PX + 4.0f + 8.0f * s;
            leftText(Rect{ b.rect.x, b.rect.y + 6.0f * s, b.rect.w, lineH }, gearSlotName(b.param), gray, inset);
            leftText(Rect{ b.rect.x, b.rect.y + b.rect.h - lineH - 6.0f * s, b.rect.w, lineH }, b.label,
                     b.label == lang("UILABEL_EMPTY") ? faint : white, inset);
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

        case Action::PartyNote: {
            // A note with room for more than one line is wrapped, a line per paragraph and more as needed; a one-line
            // note is shortened to fit
            std::vector<std::string> lines;
            if (b.rect.h >= 2.0f * lineH) {
                size_t from = 0;
                while (from <= b.label.size()) {
                    const size_t nl = b.label.find('\n', from);
                    const std::string para = b.label.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
                    for (const std::string& l : wrapListText(para, b.rect.w)) lines.push_back(l);
                    if (nl == std::string::npos) break;
                    from = nl + 1;
                }
            }
            if (lines.size() <= 1) {
                leftText(b.rect, b.label, gray, 0.0f);
                break;
            }
            const float pitch = lineH * 1.05f;
            const int fit = std::max(1, (int)(b.rect.h / pitch));
            float y = b.rect.y;
            for (int k = 0; k < (int)lines.size() && k < fit; ++k, y += pitch) {
                drawListText(lines[(size_t)k], b.rect.x, y, gray);
            }
            break;
        }

        case Action::PartyInvSlot: {
            const ItemStack& st = picked.items[b.param];
            drawRect(b.rect, plateFor(hot, b.param == ui.stackSlot, true));
            if (st.item > 0) {
                drawItemPicture(b.rect, st.item, 1.0f);
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
            drawRect(b.rect, RGBA{ 0.04f, 0.03f, 0.07f, 0.98f });
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
    setMenuText(menuSet);
    lineH = menuLineH;

    // Middle: the picked unit, large, with its name and totals under it. With no units, only the message.
    if (p.empty()) {
        drawUiTextCentered(fitText(lang("UILABEL_NEEDS_UNITS"), P.center.w - 24.0f * s, true), P.center.x + P.center.w * 0.5f,
                           P.center.y + P.center.h * 0.5f, warn);
        return;
    }
    const float cw = P.center.w;
    // The picked unit's model, turned by dragging in it with the right button
    if (picked.mob > 0) drawMobPreview(picked.mob, partyPreviewRect(uiW_, uiH_), ui.previewYaw);
    else drawUnitModelPreview(picked.sprite, partyPreviewRect(uiW_, uiH_), ui.previewYaw);
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

Entity Renderer::groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH,
                                   bool labels) {
    // A name line in a label box picks its item. The box on top wins, so the nearer item is picked.
    if (labels) {
        const std::vector<GroundStack> stacks = groundStacks(w, t, cam, winW, winH);
        size_t k = 0, j = 0;
        if (topLabelRow(stacks, mx, my, k, j)) return stacks[k].items[j];
    }

    // Otherwise the sprite under the mouse
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;

        ItemOnScreen os;
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) continue;
        const bool onSprite = mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom;
        if (!onSprite || os.bottom - os.top < 1.0f) continue;
        // Only the visible part of its picture counts, not the clear pixels around it
        if (!itemPixelSolid(gi->item, (mx - (os.cx - os.halfW)) / (2.0f * os.halfW), (my - os.top) / (os.bottom - os.top))) continue;

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
