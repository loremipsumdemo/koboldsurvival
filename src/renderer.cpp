#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const float GLYPH_HEIGHT = 1.0f;   // world units for a glyph's height

const char* VS_SRC = R"(
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

const char* FS_SRC = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform float uAlphaCut;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < uAlphaCut) discard;
    FragColor = c;
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

GLuint makeProgram(const char* vs, const char* fs) {
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

} // namespace

// ---------------------------------------------------------------------------
// Asset loading
// ---------------------------------------------------------------------------

GLuint loadTexture(const char* path) {
    SDL_Surface* s = SDL_LoadBMP(path);
    if (!s) return 0;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    SDL_DestroySurface(conv);
    return t;
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

void freeAssets(Assets& a) {
    GLuint textures[] = {
        a.floorTex, a.playerTex, a.keyTex, a.doorTex, a.backgroundTex,
        a.glyphPlayer.tex, a.glyphKey.tex, a.glyphDoor.tex,
        a.glyphWagon.tex, a.glyphFloor.tex
    };
    for (GLuint t : textures) if (t) glDeleteTextures(1, &t);
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

bool Renderer::init() {
    program_ = makeProgram(VS_SRC, FS_SRC);
    uMVP_ = glGetUniformLocation(program_, "uMVP");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uAlphaCut_ = glGetUniformLocation(program_, "uAlphaCut");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 4 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return program_ != 0;
}

void Renderer::shutdown() {
    glDeleteProgram(program_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    program_ = vao_ = vbo_ = 0;
}

void Renderer::drawQuad(GLuint tex, const Mat4& mvp, const Quad& q) {
    float rx = q.rx * q.hw, ry = q.ry * q.hw, rz = q.rz * q.hw;
    float ux = q.ux * q.hh, uy = q.uy * q.hh, uz = q.uz * q.hh;

    float p[4][3] = {
        { q.cx - rx - ux, q.cy - ry - uy, q.cz - rz - uz },
        { q.cx + rx - ux, q.cy + ry - uy, q.cz + rz - uz },
        { q.cx + rx + ux, q.cy + ry + uy, q.cz + rz + uz },
        { q.cx - rx + ux, q.cy - ry + uy, q.cz - rz + uz }
    };
    static const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };

    float verts[20];
    for (int i = 0; i < 4; i++) {
        verts[i * 5 + 0] = p[i][0];
        verts[i * 5 + 1] = p[i][1];
        verts[i * 5 + 2] = p[i][2];
        verts[i * 5 + 3] = uv[i][0];
        verts[i * 5 + 4] = uv[i][1];
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Quad that always faces the camera, standing on (wx, wz)
void Renderer::drawBillboard(GLuint tex, float wx, float wz, float w, float h) {
    float dx = camX_ - wx;
    float dz = camZ_ - wz;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    drawQuad(tex, viewProj_, Quad{ wx, h * 0.5f, wz,  dz, 0.0f, -dx,  0.0f, 1.0f, 0.0f,  w * 0.5f, h * 0.5f });
}

// Quad lying flat on the ground at height y
void Renderer::drawFlat(GLuint tex, float wx, float wz, float y, float hw, float hh) {
    drawQuad(tex, viewProj_, Quad{ wx, y, wz,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  hw, hh });
}

void Renderer::drawModel(const Model& m, float wx, float wz, float targetSize) {
    float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
    if (extent <= 0.0f) return;
    float s = targetSize / extent;
    float cx = (m.minX + m.maxX) * 0.5f;
    float cz = (m.minZ + m.maxZ) * 0.5f;

    Mat4 model{};
    model.m[0] = s;
    model.m[5] = s;
    model.m[10] = s;
    model.m[15] = 1.0f;
    model.m[12] = wx - s * cx;
    model.m[13] = -s * m.minY;
    model.m[14] = wz - s * cz;

    Mat4 mvp = mul(viewProj_, model);
    glBindVertexArray(m.vao);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT,
                       (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
}

void Renderer::drawBackground(GLuint tex) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    drawQuad(tex, ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f),
             Quad{ 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f });
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawFrame(World& world, const Assets& a, const Camera& cam,
                         RenderMode mode, int winW, int winH) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // The camera follows whichever entity is the player
    float targetX = 0.0f, targetZ = 0.0f;
    for (Entity e : world.view<Player, Transform>()) {
        Transform* t = world.get<Transform>(e);
        targetX = t->x;
        targetZ = t->z;
    }

    viewProj_ = cam.viewProj(targetX, targetZ, (float)winW / (float)winH);
    float ex, ey, ez;
    cam.eye(targetX, targetZ, ex, ey, ez);
    camX_ = ex;
    camZ_ = ez;

    glUseProgram(program_);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);

    bool ascii = (mode == RenderMode::Ascii);
    glUniform1f(uAlphaCut_, ascii ? 0.1f : 0.0f);

    if (!ascii) drawBackground(a.backgroundTex);

    // Floor tiles
    for (Entity e : world.view<Floor, Transform, Appearance>()) {
        Transform* t = world.get<Transform>(e);
        Appearance* ap = world.get<Appearance>(e);
        if (ascii) {
            const float dotHalfH = 0.15f;
            drawFlat(ap->glyph.tex, t->x, t->z, 0.01f, dotHalfH * ap->glyph.aspect, dotHalfH);
        } else {
            drawFlat(ap->spriteTex, t->x, t->z, 0.0f, 0.5f, 0.5f);
        }
    }

    // Everything else that has a position and an appearance
    for (Entity e : world.view<Transform, Appearance>()) {
        if (world.has<Floor>(e)) continue;
        Transform* t = world.get<Transform>(e);
        Appearance* ap = world.get<Appearance>(e);

        if (ascii) {
            drawBillboard(ap->glyph.tex, t->x, t->z, GLYPH_HEIGHT * ap->glyph.aspect, GLYPH_HEIGHT);
        } else if (ap->model) {
            if (ap->model->vao) drawModel(*ap->model, t->x, t->z, ap->modelSize);
        } else {
            drawBillboard(ap->spriteTex, t->x, t->z, ap->spriteW, ap->spriteH);
        }
    }
}
