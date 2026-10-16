#include "renderer.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr float GLYPH_H = 1.2f;          // world height of unit, key, and door sprites
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

// Height bands: lowest tan-yellow, middle green, highest dark gray
RGBA bandColor(float h) {
    float n = h / HEIGHT_SCALE;   // back to the -1..1 range
    if (n < -1.0f / 3.0f) return { 0.86f, 0.76f, 0.46f, 1.0f };
    if (n < 1.0f / 3.0f)  return { 0.27f, 0.55f, 0.24f, 1.0f };
    return { 0.27f, 0.27f, 0.29f, 1.0f };
}

// One vertex per tile corner, two triangles per tile, split along a-d
void buildTerrainMesh(const Terrain& t, std::vector<Vertex>& verts, std::vector<uint32_t>& idx) {
    const std::vector<float>& h = t.heights();
    auto H = [&](int x, int z) {
        x = std::clamp(x, 0, MAP_SIZE);
        z = std::clamp(z, 0, MAP_SIZE);
        return h[(size_t)z * VERT_SIZE + x];
    };

    // Directional light used only for slope shading
    const float lx = 0.35f, ly = 0.8f, lz = 0.25f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);

    verts.resize((size_t)VERT_SIZE * VERT_SIZE);
    for (int z = 0; z < VERT_SIZE; ++z) {
        for (int x = 0; x < VERT_SIZE; ++x) {
            float hh = H(x, z);
            float dhx = (H(x + 1, z) - H(x - 1, z)) * 0.5f;
            float dhz = (H(x, z + 1) - H(x, z - 1)) * 0.5f;
            float nl = std::sqrt(dhx * dhx + 1.0f + dhz * dhz);
            float nx = -dhx / nl, ny = 1.0f / nl, nz = -dhz / nl;
            float lit = (nx * lx + ny * ly + nz * lz) / ll;
            float shade = std::clamp(0.45f + 0.55f * lit, 0.4f, 1.0f);

            RGBA c = bandColor(hh);
            verts[(size_t)z * VERT_SIZE + x] = {
                { (float)x, hh, (float)z },
                { c.r * shade, c.g * shade, c.b * shade, 1.0f }
            };
        }
    }

    idx.clear();
    idx.reserve((size_t)MAP_SIZE * MAP_SIZE * 6);
    for (int z = 0; z < MAP_SIZE; ++z) {
        for (int x = 0; x < MAP_SIZE; ++x) {
            uint32_t a = (uint32_t)(z * VERT_SIZE + x);
            uint32_t b = a + 1;
            uint32_t c = a + VERT_SIZE;
            uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(d);
            idx.push_back(a); idx.push_back(d); idx.push_back(b);
        }
    }
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
    SDL_Surface* s = IMG_Load(path);
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
uniform samplerCube uSky;
out vec4 FragColor;
void main() {
    FragColor = texture(uSky, normalize(vDir));
}
)";

// Loads a skybox laid out as a horizontal cross: 4 columns by 3 rows of square faces.
//           col 0  col 1  col 2  col 3
//   row 0      .    +Y      .      .
//   row 1     -X    -Z     +X     +Z
//   row 2      .    -Y      .      .
// Reading the middle row left to right: left, front, right, back (turning right).
GLuint loadSkyboxCross(const char* path) {
    SDL_Surface* s = IMG_Load(path);
    if (!s) {
        SDL_Log("Could not load %s: %s", path, SDL_GetError());
        return 0;
    }
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (!conv) return 0;

    const int fw = conv->w / 4, fh = conv->h / 3;

    // Cell (column, row) for each face, in GL's cube map face order
    const int cells[6][2] = { {2, 1}, {0, 1}, {1, 0}, {1, 2}, {3, 1}, {1, 1} };
    const GLenum targets[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    std::vector<unsigned char> face((size_t)fw * fh * 4);
    for (int f = 0; f < 6; ++f) {
        const int x0 = cells[f][0] * fw, y0 = cells[f][1] * fh;
        for (int y = 0; y < fh; ++y) {
            const unsigned char* src = (const unsigned char*)conv->pixels
                                     + (size_t)(y0 + y) * conv->pitch + (size_t)x0 * 4;
            std::memcpy(&face[(size_t)y * fw * 4], src, (size_t)fw * 4);
        }
        glTexImage2D(targets[f], 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, face.data());
    }

    SDL_DestroySurface(conv);
    return tex;
}

} // namespace

bool Renderer::init(TTF_Font* font) {
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
    unitSprites_.reserve(38);
    for (int i = 1; i <= 38; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "assets/unit/%02d.png", i);
        unitSprites_.push_back(loadImage(path, false));
    }
    keySprite_   = loadImage("assets/environment/key.png", false);
    doorSprite_  = loadImage("assets/environment/door.png", false);
    skyCube_ = loadSkyboxCross("assets/skybox/01.png");

    skyProgram_ = linkProgram(SKY_VS, SKY_FS);
    uSkyInvVP_ = glGetUniformLocation(skyProgram_, "uInvVP");

    const float skyQuad[8] = { -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, 1.0f,  -1.0f, 1.0f };
    glGenVertexArrays(1, &skyVao_);
    glGenBuffers(1, &skyVbo_);
    glBindVertexArray(skyVao_);
    glBindBuffer(GL_ARRAY_BUFFER, skyVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyQuad), skyQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    overlay_     = loadImage("assets/background/overlay_01.png", false);
    wall_        = makeGlyphTexture(font, "#", SDL_Color{ 170, 160, 140, 255 });
    unitFallback_ = makeGlyphTexture(font, "@", SDL_Color{ 80, 160, 255, 255 });
    keyFallback_  = makeGlyphTexture(font, "K", SDL_Color{ 255, 215, 0, 255 });
    doorFallback_ = makeGlyphTexture(font, "D", SDL_Color{ 230, 80, 60, 255 });

    if (!loadGLBModel("assets/3d/wagon.glb", wagon_)) {
        SDL_Log("Wagon model unavailable, the wagon will not be drawn");
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return colorProgram_ != 0 && glyphProgram_ != 0;
}

void Renderer::shutdown() {
    TextTex* textures[] = { &wall_, &keySprite_, &doorSprite_, &overlay_,
                            &unitFallback_, &keyFallback_, &doorFallback_ };
    for (TextTex* g : textures) {
        if (g->tex) glDeleteTextures(1, &g->tex);
        g->tex = 0;
    }
    for (TextTex& g : unitSprites_) {
        if (g.tex) glDeleteTextures(1, &g.tex);
    }
    unitSprites_.clear();
    destroyModel(wagon_);

    glDeleteTextures(1, &skyCube_);
    glDeleteProgram(skyProgram_);
    glDeleteBuffers(1, &skyVbo_);
    glDeleteVertexArrays(1, &skyVao_);

    glDeleteProgram(colorProgram_);
    glDeleteProgram(glyphProgram_);
    glDeleteBuffers(1, &dynVbo_);
    glDeleteVertexArrays(1, &dynVao_);
    glDeleteBuffers(1, &glyphVbo_);
    glDeleteVertexArrays(1, &glyphVao_);
    glDeleteBuffers(1, &terrainVbo_);
    glDeleteBuffers(1, &terrainEbo_);
    glDeleteVertexArrays(1, &terrainVao_);
    colorProgram_ = glyphProgram_ = 0;
    terrainVao_ = terrainVbo_ = terrainEbo_ = 0;
}

void Renderer::uploadTerrain(const Terrain& t) {
    // A new level replaces the old terrain mesh
    if (terrainVao_) {
        glDeleteBuffers(1, &terrainVbo_);
        glDeleteBuffers(1, &terrainEbo_);
        glDeleteVertexArrays(1, &terrainVao_);
        terrainVao_ = terrainVbo_ = terrainEbo_ = 0;
    }

    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    buildTerrainMesh(t, verts, idx);
    terrainIndexCount_ = (GLsizei)idx.size();

    glGenVertexArrays(1, &terrainVao_);
    glGenBuffers(1, &terrainVbo_);
    glGenBuffers(1, &terrainEbo_);

    setupColorLayout(terrainVao_, terrainVbo_);
    glBindVertexArray(terrainVao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrainVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrainEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
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

    // Right vector perpendicular to the direction from the camera
    float dx = camX_ - x, dz = camZ_ - z;
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

void Renderer::drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                         float overlayAlpha, int winW, int winH) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.55f, 0.7f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    viewProj_ = cam.viewProj((float)winW / (float)winH);
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    camX_ = ex;
    camZ_ = ez;

    // 0. Sky: the skybox is sampled by view direction, so it stays fixed while the camera moves
    {
        // View matrix with the eye at the origin, so the sky only follows rotation
        Mat4 proj = perspective(0.9f, (float)winW / (float)winH, 0.5f, 4000.0f);
        Mat4 viewRot = lookAt(0.0f, 0.0f, 0.0f,
                              cam.focusX - ex, cam.focusY - ey, cam.focusZ - ez,
                              0.0f, 1.0f, 0.0f);
        float invSky[16];
        invert4x4(mul(proj, viewRot).m, invSky);

        glUseProgram(skyProgram_);
        glUniformMatrix4fv(uSkyInvVP_, 1, GL_FALSE, invSky);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);
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
    glBindVertexArray(terrainVao_);
    glDrawElements(GL_TRIANGLES, terrainIndexCount_, GL_UNSIGNED_INT, nullptr);

    // 2. Sprites and models, each standing on the terrain at its entity's height
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);

    for (Entity e : w.view<Key, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        const TextTex& g = keySprite_.tex ? keySprite_ : keyFallback_;
        drawGlyph(g, tr->x, tr->y, tr->z, GLYPH_H, WHITE);
    }

    for (Entity e : w.view<Wagon, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        drawModel(wagon_, tr->x, tr->y, tr->z, WAGON_SIZE);
    }

    // Building: a 3x3 block of walls, two rows high. The entrance shows the door.
    for (Entity e : w.view<Door>()) {
        const Door* d = w.get<Door>(e);
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                GridPos tile{ d->origin.x + dx, d->origin.y + dy };
                float cx = tile.x + 0.5f, cz = tile.y + 0.5f;
                float gy = t.heightAt(cx, cz);

                if (tile == d->entrance) {
                    RGBA tint = d->unlocked ? WHITE : RGBA{ 0.45f, 0.45f, 0.45f, 1.0f };
                    const TextTex& g = doorSprite_.tex ? doorSprite_ : doorFallback_;
                    drawGlyph(g, cx, gy, cz, GLYPH_H, tint);
                } else {
                    drawGlyph(wall_, cx, gy, cz, GLYPH_H, WHITE);
                }
                drawGlyph(wall_, cx, gy + GLYPH_H, cz, GLYPH_H, WHITE);
            }
        }
    }

    for (Entity e : w.view<Unit, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        const UnitSprite* us = w.get<UnitSprite>(e);
        int idx = us ? us->id - 1 : 0;
        const bool inRange = idx >= 0 && idx < (int)unitSprites_.size();
        const TextTex& g = (inRange && unitSprites_[idx].tex) ? unitSprites_[idx] : unitFallback_;
        RGBA tint = w.has<Selected>(e) ? RGBA{ 1.0f, 1.0f, 0.6f, 1.0f } : WHITE;
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

    glBindVertexArray(0);
}
