#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include "camera.h"
#include "ecs.h"
#include "resources.h"

enum class RenderMode { Ascii, Sprite };

GLuint loadTexture(const char* path);
TextTex makeGlyphTexture(TTF_Font* font, const char* text, SDL_Color color);
void freeAssets(Assets& a);

class Renderer {
public:
    bool init();
    void shutdown();
    void drawFrame(World& world, const Assets& assets, const Camera& cam,
                   RenderMode mode, int winW, int winH);

private:
    struct Quad {
        float cx, cy, cz;     // center
        float rx, ry, rz;     // right axis
        float ux, uy, uz;     // up axis
        float hw, hh;         // half extents
    };

    void drawQuad(GLuint tex, const Mat4& mvp, const Quad& q);
    void drawBillboard(GLuint tex, float wx, float wz, float w, float h);
    void drawFlat(GLuint tex, float wx, float wz, float y, float hw, float hh);
    void drawModel(const Model& m, float wx, float wz, float targetSize);
    void drawBackground(GLuint tex);

    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint uMVP_ = -1, uTex_ = -1, uAlphaCut_ = -1;

    // Per-frame state
    Mat4 viewProj_{};
    float camX_ = 0.0f, camZ_ = 0.0f;
};
