#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include "camera.h"
#include "gui.h"
#include "sky.h"
#include "components.h"
#include "ecs.h"
#include "model.h"
#include "resources.h"
#include "terrain.h"
#include "terrain_mesh.h"
#include "ui.h"

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
    // Creates the GL programs, then loads every image and model. progress (0..1) is
    // called between steps so the loading screen can be drawn while this runs.
    // font: large font for the symbol fallbacks. hudFont: small font for the hover label.
    // uiFont: font for menu buttons and headings.
    bool init(TTF_Font* font, TTF_Font* hudFont, TTF_Font* uiFont,
              const std::function<void(float)>& progress = {});
    void shutdown();

    // In-game time of day in minutes, 0..1440. Sets the sun position and sky colours.
    void setDayMinutes(float m) { dayMinutes_ = m; }
    // Sun, moon, star and sky colour settings from the dev tools menu
    void setSky(const SkyTuning& s) { sky_ = s; }

    // Sets up the terrain's chunks for a map, replacing any earlier one. Each chunk's mesh is built
    // when it is first drawn, at the detail its distance from the camera needs.
    void uploadTerrain(const Terrain& t);

    // Loading screen: the start image plus a progress bar (progress 0..1) along the bottom 4%
    void drawLoadingScreen(float progress, int winW, int winH, const std::string& label = std::string());

    // Units whose sprite covers the pixel (mx, my), in entity order
    std::vector<Entity> unitsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);

    // overlayAlpha: 0 hides the level-start image, 1 shows it fully
    // hud: text lines drawn in the top-left corner (empty for none)
    void drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                   float overlayAlpha, const std::vector<std::string>& hud, int winW, int winH);

    // Tile-sized ring on the ground under the camera's pivot point, drawn over drawFrame.
    // alpha 0..1 fades it. Shown while the middle mouse button orbits the camera.
    void drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH);

    // Crystal marking where a right-click order was given, standing on the ground at (x, z).
    // alpha 0..1 fades it.
    void drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha, int winW, int winH);

    // In-game panels, drawn between beginUi and endUi. The bottom panel shows each unit in
    // units with its portrait and name, and a yellow frame around the selected ones.
    void drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units);
    // Right sidebar with the 32 inventory slots of one unit
    void drawInventorySidebar(const SidebarLayout& L, const Inventory& inv, const std::string& unitName);

    // Menu drawing. Call beginUi before the first menu draw and endUi after the last.
    void beginUi(int winW, int winH);
    void endUi();
    void drawTitleBackground();                  // clears, then draws title.png covering the screen
    void drawRect(const Rect& r, RGBA c);
    void drawButton(const Rect& r, const std::string& label, bool enabled, bool hovered);
    void drawUiText(const std::string& s, float x, float y, RGBA tint);
    void drawUiTextCentered(const std::string& s, float cx, float y, RGBA tint);
    float uiTextWidth(const std::string& s);

    // Smaller text for dense lists, such as the save list. The font is owned by the caller.
    void setListFont(TTF_Font* font);
    void drawListText(const std::string& s, float x, float y, RGBA tint);
    float listTextHeight(const std::string& s);
    // A small button with list-font text, for the rows of the save list
    void drawListButton(const Rect& r, const std::string& label, bool hovered);
    // A button whose background fills from left to right (fill 0..1), for the hold-to-delete button
    void drawHoldButton(const Rect& r, const std::string& label, float fill);

private:
    // Text line with its texture and size in pixels
    struct HudLine {
        std::string text;
        TextTex tex;
        float w = 0.0f, h = 0.0f;
    };

    // Draws a textured quad: 4 vertices of (x, y, z, u, v), in the order BL, BR, TR, TL
    void drawQuadVerts(GLuint tex, const float* v, RGBA tint);

    // Symbol-style billboard that stands on (x, y, z) and turns to face a point 200 units behind the camera
    void drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint);

    // 3D model standing on (x, y, z), scaled so its largest side is targetSize
    void drawModel(const Model& m, float x, float y, float z, float targetSize);

    // Text lines in the top-left corner, each on a dark panel
    void drawHud(const std::vector<std::string>& lines, int winW, int winH);

    // The sprite for a unit, or the fallback symbol if its image is missing
    const TextTex& unitGlyph(World& w, Entity e) const;

    // Cached white text texture for a menu string, created on first use
    HudLine& uiLine(const std::string& s);

    // Cached white text texture for a list string, using the list font
    HudLine& listLine(const std::string& s);

    GLuint colorProgram_ = 0, glyphProgram_ = 0;
    GLint uMVPColor_ = -1, uMVPGlyph_ = -1, uTint_ = -1;

    GLuint dynVao_ = 0, dynVbo_ = 0;          // colour overlays
    GLuint glyphVao_ = 0, glyphVbo_ = 0;      // one textured quad at a time
    // One chunk of the terrain on the GPU. Its buffers are reused when its detail changes.
    // One slot per chunk of the level, indexed by chunk id. Buffers exist only while the chunk is loaded.
    struct TerrainChunk {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei indexCount = 0;
        int step = 0;              // detail step the buffers hold, 0 until first built
        uint32_t version = 0;      // version of the chunk the buffers and bounds were made from
        ChunkBounds bounds{};
    };
    std::vector<TerrainChunk> chunks_;
    int terrainSize_ = 0;
    int chunksPerSide_ = 0;
    ChunkMesh chunkScratch_;       // reused while building chunks
    void releaseTerrain();
    void releaseChunk(TerrainChunk& c);
    void buildChunk(const Terrain& t, const ChunkData& c, int id, int step);
    void drawTerrain(const Terrain& t, const Camera& cam);

    TextTex overlay_, title_;
    std::vector<TextTex> unitSprites_;
    // Text symbols, used only when a sprite file could not be loaded
    TextTex unitFallback_;
    Model wagon_;

    // Hover label: one cached texture per line, rebuilt only when its text changes
    TTF_Font* font_ = nullptr;
    TTF_Font* hudFont_ = nullptr;
    std::vector<HudLine> hudLines_;

    // Menus
    TTF_Font* uiFont_ = nullptr;
    std::map<std::string, HudLine> uiText_;

    // Smaller font for dense lists
    TTF_Font* listFont_ = nullptr;
    std::map<std::string, HudLine> listText_;
    Mat4 pixels_{};
    int uiW_ = 0, uiH_ = 0;

    Mat4 viewProj_{};

    // Sky: a gradient and sun drawn as a full-screen quad, coloured by the time of day
    GLuint skyProgram_ = 0, skyVao_ = 0, skyVbo_ = 0;
    GLint uSkyInvVP_ = -1, uSunDir_ = -1, uSunAlpha_ = -1, uZenith_ = -1, uHorizon_ = -1, uSunCol_ = -1;
    GLint uMoonDir_ = -1, uMoonAlpha_ = -1, uMoonCol_ = -1, uStarAlpha_ = -1;
    float dayMinutes_ = 360.0f;
    SkyTuning sky_;
    // The point sprites turn to face: 200 units behind the camera
    float billX_ = 0.0f, billZ_ = 0.0f;
};
