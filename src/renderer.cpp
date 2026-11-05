// The renderer's core: start-up and shutdown, the GL programs, flat shapes and textured quads, clipping, and the
// pictures loaded on demand (items, emotes, icons). The rest of Renderer is split by job:
//   renderer_text.cpp      fonts, text and its caches, fitting and wrapping
//   renderer_widgets.cpp   buttons, icons, title bars, item cells and item tooltips
//   renderer_units.cpp     units and props in the world: sprites, models, facing, picking
//   renderer_world.cpp     the 3D frame, the hover list, ground items and the build preview
//   renderer_overlays.cpp  what floats over the world: names, emotes, labels, damage numbers, work rings
//   renderer_menus.cpp     the loading screen, the title screen and the party menu
//   renderer_panels.cpp    the Faction Panel, the Information Panel and the world map
//   renderer_sidebar.cpp   the Unit Panel's tabs: stats, inventory, build and behaviors
//   renderer_popups.cpp    dialogs: talk, trade, storage, loot, prompts and menus
//   renderer_terrain.cpp   terrain, sea and seabed
#include "renderer_internal.h"

using namespace render_detail;

namespace {

constexpr float TWO_PI = 6.28318530f;

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

namespace render_detail {

bool itemOnScreen(const Mat4& vp, int W, int H, const Transform& tr, float aspect, ItemOnScreen& out) {
    float bx, by, tx, ty, mx, my;
    if (!worldToScreen(vp, tr.x, tr.y, tr.z, W, H, bx, by)) return false;
    if (!worldToScreen(vp, tr.x, tr.y + ITEM_GLYPH_H, tr.z, W, H, tx, ty)) return false;
    if (!worldToScreen(vp, tr.x, tr.y + ITEM_GLYPH_H * 0.5f, tr.z, W, H, mx, my)) return false;
    out.cx = mx;
    out.top = std::min(by, ty);
    out.bottom = std::max(by, ty);
    out.halfW = (out.bottom - out.top) * 0.5f * aspect;
    return true;
}

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

GLuint MakeRgbaTexture(int w, int h, const uint8_t* rgba) {
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

GLuint MakeDotTexture(bool white) {
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

float unitSaturation(World& w, Entity u) {
    if (w.has<Corpse>(u)) return 0.0f;
    if (w.has<Unconscious>(u)) return 0.5f;
    return 1.0f;
}

RGBA mixColor(RGBA a, RGBA b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return RGBA{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

RGBA rgb255(int r, int g, int b, float a) { return RGBA{ r / 255.0f, g / 255.0f, b / 255.0f, a }; }

std::string withCommas(long long n) {
    const std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return n < 0 ? "-" + out : out;
}

void cutCornerShape(std::vector<UiVert>& v, const Rect& r, float cut, RGBA top, RGBA bottom) {
    cut = std::min(cut, std::min(r.w, r.h));
    auto at = [&](float x, float y) { return UiVert{ x, y, mixColor(top, bottom, r.h > 0.0f ? (y - r.y) / r.h : 0.0f) }; };
    const UiVert a = at(r.x, r.y), b = at(r.x + r.w, r.y), c = at(r.x + r.w, r.y + r.h - cut);
    const UiVert d = at(r.x + r.w - cut, r.y + r.h), e = at(r.x, r.y + r.h);
    v.insert(v.end(), { a, b, c, a, c, d, a, d, e });
}

RGBA logTint(Tint t) {
    switch (t) {
    case Tint::Green: return RGBA{ 0.42f, 0.95f, 0.45f, 1.0f };
    case Tint::Red:   return RGBA{ 1.0f, 0.38f, 0.36f, 1.0f };
    case Tint::Gray:  break;
    }
    return RGBA{ 0.85f, 0.78f, 0.70f, 1.0f };
}

} // namespace render_detail

bool Renderer::init(TTF_Font* font, TTF_Font* hudFont, TTF_Font* uiFont,
                    const std::function<void(float, const std::string&)>& progress) {
    // How far along it is, and the kind of asset loading now with how many of it are done, named under the loading
    // bar; log.txt gets how long each kind took. Nothing is shown until the fonts and shaders the loading screen draws
    // with are ready.
    LoadStages stages("Start-up", progress);
    font_ = font;
    hudFont_ = hudFont;
    ui_.font = uiFont;

    if (!initGl()) return false;
    loadLoadingArt();
    // A picture or model that is missing is drawn as the pink square (pink_)
    loadPictures(stages);
    loadUnitModels(stages);
    loadMobArt(stages);
    stages.stage(0.55f, "LOADING_SKY");
    initSky();
    makeTexels();
    loadPropModels(stages);
    stages.finish();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return colorProgram_ != 0 && glyphProgram_ != 0;
}

// The flat-colour and textured-quad programs and their buffers, and the terrain's
bool Renderer::initGl() {
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
    return true;
}

// The start-up loading picture, one of data/illustration/loading1.png to loading3.png at random, loaded before
// the first loading frame is drawn. Only the ones in data.pak are picked from; with none, the bar is drawn alone.
void Renderer::loadLoadingArt() {
    std::vector<std::string> found;
    for (int i = 1; i <= 3; ++i) {
        const std::string path = "data/illustration/loading" + std::to_string(i) + ".png";
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (pakFind(path.c_str(), bytes, size)) found.push_back(path);
    }
    if (!found.empty()) {
        // Not SDL_rand: unseeded, it seeds itself from the performance counter, and its first number barely moves
        // between launches a few seconds apart, so it picked the same picture nearly every time. The system's
        // random source, mixed with the clock, gives a fair pick each launch.
        std::random_device device;
        std::seed_seq seed{ device(), device(), (unsigned)SDL_GetPerformanceCounter(),
                            (unsigned)(SDL_GetPerformanceCounter() >> 32) };
        std::mt19937 rng(seed);
        const std::string& pick = found[std::uniform_int_distribution<size_t>(0, found.size() - 1)(rng)];
        loadingArt_ = loadOptionalImage(pick.c_str());
        if (loadingArt_.tex) {
            // Drawn well under its own size, so it gets mipmaps to stay smooth
            glBindTexture(GL_TEXTURE_2D, loadingArt_.tex);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            SDL_Log("Loading picture: %s", pick.c_str());
        }
    }
}

// The title screen's logo and each unit model's profile picture
void Renderer::loadPictures(LoadStages& stages) {
    const int pictureCount = 1 + UNIT_SPRITE_COUNT;
    stages.stage(0.0f, "LOADING_PICTURES", pictureCount);
    // The title screen's logo is drawn well under its own size, so it gets mipmaps to stay smooth
    titleLogo_ = loadOptionalImage("data/illustration/title.png");
    if (titleLogo_.tex) {
        glBindTexture(GL_TEXTURE_2D, titleLogo_.tex);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    stages.step(0.03f, 1);
    // Each unit model's profile picture, numbered as the models are
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/profile/profile_%02d.png", i);
        profilePics_[(size_t)i - 1] = loadOptionalImage(path);
        stages.step(0.03f + 0.02f * (float)i / (float)UNIT_SPRITE_COUNT, 1 + i);
    }
}

// Each unit model: data/model/unit/unit_<nn>.glb. A file with the same bytes as an earlier one is not loaded
// again: its sprite number shares the earlier model. A missing or unreadable one leaves its units pink squares.
void Renderer::loadUnitModels(LoadStages& stages) {
    stages.stage(0.05f, "LOADING_UNIT_MODELS", UNIT_SPRITE_COUNT);
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
}

// Each mob kind's model, when it has one: data/model/mob/mob_<nn>.glb. A missing one leaves it drawn by its picture.
// Then every mob kind's picture, data/mob/mob_<n>.png.
void Renderer::loadMobArt(LoadStages& stages) {
    int mobModelCount = 0;
    for (int k = 1; k <= MOB_KIND_COUNT; ++k) mobModelCount += mobInfo((MobKind)k).model > 0 ? 1 : 0;
    stages.stage(0.4f, "LOADING_MOB_MODELS", mobModelCount);
    int mobModelsDone = 0;
    for (int k = 1; k <= MOB_KIND_COUNT; ++k) {
        const MobKind kind = (MobKind)k;
        const int n = mobInfo(kind).model;
        if (n <= 0) continue;
        ++mobModelsDone;
        char path[64];
        std::snprintf(path, sizeof(path), "data/model/mob/mob_%02d.glb", n);
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        Model& m = mobModels_[(size_t)k - 1];
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
    stages.stage(0.5f, "LOADING_MOB_PICTURES", MOB_KIND_COUNT);
    mobSprites_.reserve(MOB_KIND_COUNT);
    for (int i = 1; i <= MOB_KIND_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/mob/mob_%d.png", i);
        mobSprites_.push_back(loadOptionalImage(path));
        stages.step(0.5f + 0.05f * (float)i / (float)MOB_KIND_COUNT, i);
    }
}

// The sky's program and its full-screen quad
void Renderer::initSky() {
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
}

// One-texel textures: plain white, for colored rectangles and glyphs, and the pink square drawn for a picture that is
// missing
void Renderer::makeTexels() {
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

// Each prop kind's model: exactly data/model/model_<n>.glb (props.h). No other name is tried, so a file that is
// missing or misnamed shows up at once: that kind is drawn as a pink square, and log.txt names it.
void Renderer::loadPropModels(LoadStages& stages) {
    stages.stage(0.57f, "LOADING_PROP_MODELS", PROP_KIND_COUNT);
    const char* standIn = "as a pink square";
    for (int k = 0; k < PROP_KIND_COUNT; ++k) {
        const PropKind kind = (PropKind)k;
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
                // Unreadable, or nothing in it to draw: drop what was made, so the pink square shows instead
                destroyModel(m);
                m = Model{};
                SDL_Log("Prop model %s could not be read or has nothing to draw: the %s is drawn %s",
                        info.file, lang(info.nameKey).c_str(), standIn);
            }
        }
        stages.step(0.58f + 0.4f * (float)(k + 1) / (float)PROP_KIND_COUNT, k + 1);
    }
}

void Renderer::shutdown() {
    auto drop = [](TextTex& g) {
        if (g.tex) glDeleteTextures(1, &g.tex);
        g = TextTex{};
    };
    drop(white_);
    drop(pink_);
    drop(titleLogo_);
    drop(loadingArt_);
    for (TextTex& g : profilePics_) drop(g);
    for (TextTex& g : mobSprites_) drop(g);
    mobSprites_.clear();
    for (Model& m : unitModels_) destroyModel(m);
    for (Model& m : mobModels_) destroyModel(m);
    unitModelFor_.fill(nullptr);
    unitFacing_.clear();
    itemPics_.clear();
    emotePics_.clear();
    uiIcons_.clear();
    // Both text sets: the one in use, then the parked one
    clearActiveText();
    swapTextSet();
    clearActiveText();
    swapTextSet();
    for (Model& m : propModels_) destroyModel(m);

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

void Renderer::setSaturation(float sat) {
    glUseProgram(glyphProgram_);
    glUniform1f(uSat_, sat);
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

void Renderer::beginUi(int winW, int winH) {
    // Game log lines are new strings in every fight, so the list cache would grow for the whole session.
    // It is cleared here, once a frame, when no text reference is held: anything drawn again is rebuilt.
    constexpr size_t LIST_CACHE_LIMIT = 512;
    if (list_.lines.size() > LIST_CACHE_LIMIT) clearFont(list_);
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

void Renderer::pollOverview() {
    OverviewPicture pic;
    if (!overview_.take(pic)) return;
    if (overviewTex_) glDeleteTextures(1, &overviewTex_);
    overviewTex_ = pic.px > 0 ? MakeRgbaTexture(pic.px, pic.px, pic.rgba.data()) : 0;
    overviewSize_ = pic.px > 0 ? static_cast<float>(pic.size) : 0.0f;
    overviewExtent_ = pic.px > 0 ? pic.extent() : 1.0f;
}

void Renderer::drawTexture(GLuint tex, float x, float y, float w, float h, RGBA tint) {
    drawUiQuad(tex, x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
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
    drawUiQuad(dotTex_, cx - h, cy - h, cx + h, cy + h, 0.0f, 0.0f, 1.0f, 1.0f, WHITE);
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

const TextTex& PictureCache::get(int n) {
    auto it = pics_.find(n);
    if (it == pics_.end()) {
        const std::string path = prefix_ + std::to_string(n) + ".png";
        it = pics_.emplace(n, loadOptionalImage(path.c_str())).first;
    }
    return it->second;
}

void PictureCache::clear() {
    for (auto& entry : pics_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    pics_.clear();
}

const TextTex* Renderer::itemTexture(int item) {
    const TextTex& t = itemPics_.get(item);
    return t.tex ? &t : &pink_;
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
    // No picture: the mask stays empty, and the pink square drawn for it is solid all over
    return itemMasks_.emplace(item, std::move(m)).first->second;
}

bool Renderer::itemPixelSolid(int item, float u, float v) {
    const ItemMask& m = itemMask(item);
    if (m.alpha.empty()) return true;   // no picture at all: the pink square, solid all over
    const int x = std::clamp((int)(u * (float)m.w), 0, m.w - 1);
    const int y = std::clamp((int)(v * (float)m.h), 0, m.h - 1);
    return m.alpha[(size_t)y * (size_t)m.w + (size_t)x] >= 26;   // as the glyph shader: under 0.1 is not drawn
}

// An emote's picture from data/emote (emote_<n>.png), loaded once. Without one, the pink square.
const TextTex* Renderer::emoteTexture(EmoteId emote) {
    const TextTex& t = emotePics_.get(idx(emote));
    return t.tex ? &t : &pink_;
}

const Model& Renderer::propModel(PropKind kind) const {
    return propModels_[(size_t)kind];
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
    drawTexture(g.tex, x, y, w, h, tint);
}
