#pragma once
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include "camera.h"
#include "components.h"
#include "ecs.h"
#include "model.h"
#include "resources.h"
#include "terrain.h"

// A texture plus its width/height ratio
struct TextTex {
    GLuint tex = 0;
    float aspect = 1.0f;
};

// Multiplies the texture colour. Alpha also fades the overlay.
struct RGBA {
    float r, g, b, a;
};

class Renderer {
public:
    bool init(TTF_Font* font);
    void shutdown();

    // Builds and uploads the terrain mesh. Frees the previous mesh if there is one.
    void uploadTerrain(const Terrain& t);

    // overlayAlpha: 0 hides the level-start image, 1 shows it fully
    void drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                   float overlayAlpha, int winW, int winH);

private:
    // Draws a textured quad: 4 vertices of (x, y, z, u, v), in the order BL, BR, TR, TL
    void drawQuadVerts(GLuint tex, const float* v, RGBA tint);

    // Symbol-style billboard that faces the camera and stands on (x, y, z)
    void drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint);

    // 3D model standing on (x, y, z), scaled so its largest side is targetSize
    void drawModel(const Model& m, float x, float y, float z, float targetSize);

    GLuint colorProgram_ = 0, glyphProgram_ = 0;
    GLint uMVPColor_ = -1, uMVPGlyph_ = -1, uTint_ = -1;

    GLuint dynVao_ = 0, dynVbo_ = 0;          // colour overlays
    GLuint glyphVao_ = 0, glyphVbo_ = 0;      // one textured quad at a time
    GLuint terrainVao_ = 0, terrainVbo_ = 0, terrainEbo_ = 0;
    GLsizei terrainIndexCount_ = 0;

    TextTex wall_, keySprite_, doorSprite_, overlay_;
    std::vector<TextTex> unitSprites_;
    // Text symbols, used only when a sprite file could not be loaded
    TextTex unitFallback_, keyFallback_, doorFallback_;
    Model wagon_;

    Mat4 viewProj_{};

    // Skybox: a cube map drawn as a full-screen quad, sampled by view direction
    GLuint skyProgram_ = 0, skyVao_ = 0, skyVbo_ = 0, skyCube_ = 0;
    GLint uSkyInvVP_ = -1;
    float camX_ = 0.0f, camZ_ = 0.0f;
};
