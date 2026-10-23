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
#include "combat.h"
#include "components.h"
#include "ecs.h"
#include "model.h"
#include "resources.h"
#include "terrain.h"
#include "terrain_mesh.h"
#include "ui.h"
#include "party_ui.h"

// A texture plus its width/height ratio
struct TextTex {
    GLuint tex = 0;
    float aspect = 1.0f;
};

// Multiplies the texture color, alpha included.
struct RGBA {
    float r, g, b, a;
};

// The combat log panel in the bottom left corner. The newest line is at the bottom, and offset counts
// the lines scrolled up from it. Lines are drawn bottom-aligned, so the first line sits in the last row.
struct CombatLogLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar track
    Rect thumb{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar thumb
    float textX = 0.0f, textW = 0.0f;
    float lineH = 0.0f;
    int linesVisible = 0;
    int lineCount = 0;
    int offset = 0;                          // lines scrolled up from the newest, within range
    int first = 0;                           // index of the oldest visible line
    int shown = 0;                           // how many lines are visible

    bool scrollable() const { return lineCount > linesVisible; }
    int maxOffset() const { return lineCount > linesVisible ? lineCount - linesVisible : 0; }
};

// One line of an item tooltip: its text, and optionally a change against the item it is compared with,
// such as "(+3)" in green when better or red when worse
struct TipRow {
    std::string text;
    RGBA tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string change;
    RGBA changeTint{ 1.0f, 1.0f, 1.0f, 1.0f };
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

    // In-game time of day in minutes, 0..1440. Sets the sun position and sky colors.
    void setDayMinutes(float m) { dayMinutes_ = m; }
    // Sun, moon, star and sky color settings from the dev tools menu
    void setSky(const SkyTuning& s) { sky_ = s; }

    // Sets up the terrain's chunks for a map, replacing any earlier one. Each chunk's mesh is built
    // when it is first drawn, at the detail its distance from the camera needs.
    void uploadTerrain(const Terrain& t);

    // Loading screen: black, with a progress bar (progress 0..1) along the bottom 4%
    void drawLoadingScreen(float progress, int winW, int winH, const std::string& label = std::string());

    // Units whose sprite covers the pixel (mx, my), in entity order
    std::vector<Entity> unitsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);

    // hud: text lines drawn in the top-left corner (empty for none)
    void drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                   const std::vector<std::string>& hud, int winW, int winH);

    // Tile-sized ring on the ground under the camera's pivot point, drawn over drawFrame.
    // alpha 0..1 fades it. Shown while the middle mouse button orbits the camera.
    void drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH);

    // Crystal marking where a right-click order was given, standing on the ground at (x, z).
    // alpha 0..1 fades it.
    void drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha, int winW, int winH);

    // In-game panels, drawn between beginUi and endUi. The bottom panel shows the walk and run toggle
    // (running says which is on), then each unit with its portrait, a health bar and its name, and a purple
    // frame around the selected ones. A portrait goes red from the bottom as its unit loses health.
    void drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units, bool running);
    // Right sidebar for one unit: its profile card, its gear and its 32 inventory slots. (mx, my) is the
    // mouse, for hover. dimSlot is an inventory slot, and dimGear a gear slot, whose item is being
    // dragged out, or -1 for none. dragItem is the item id being dragged (0 for none): gear slots then
    // show green or red for whether it may go there.
    void drawInventorySidebar(const SidebarLayout& L, World& w, Entity unit, float mx, float my,
                              int dimSlot, int dimGear, int dragItem);
    // The stack being dragged, drawn under the mouse
    void drawDraggedStack(float mx, float my, int item, int count);
    // The in-game clock and date in a box at the top right. right is the x the box lines up against:
    // the window's right edge, or the sidebar's left edge while the sidebar shows. Both lines are
    // right aligned, and the clock's digits keep the same width as they change.
    void drawClock(const std::string& clock, const std::string& date, float right, int winH);
    // A notice, drawn centerd at the top of the window
    void drawNotice(const std::string& text, int winW, int winH);
    // The stats popout for one unit, and its layout (which depends on the fonts)
    StatsPanelLayout statsPanelLayout(int winW, int winH);
    void drawStatsPanel(const StatsPanelLayout& L, const std::string& name, const Stats& stats, float mx, float my);
    // The right-click menu of an inventory item, opened at (x, y), and its drawing
    ItemMenuLayout itemMenuLayout(float x, float y, int buttonCount, int winW, int winH);
    void drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my);

    // Items lying on the ground, drawn as billboards over loaded ground (called by drawFrame). Each shows
    // its picture from data/item, or the pink quad when there is none.
    // While Alt is held, drawGroundLabels draws a name box over each one in view (between beginUi
    // and endUi), and groundItemAtPixel finds the item under the mouse, label or sprite.
    void drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH);
    Entity groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH);

    // Each unit's name over its head, in brackets with a drop shadow. Hostile units are shown in red.
    void drawUnitNames(World& w, const Camera& cam, int winW, int winH);

    // Damage numbers over the units they hit, rising and fading from the combat system's list. They draw
    // between beginUi and endUi, after the names.
    void drawFloatingNumbers(const std::vector<FloatingNumber>& floats, const Camera& cam, int winW, int winH);
    // The combat log in the bottom left. offset is how far it is scrolled up, and rightLimit is the x its
    // panel must stay left of, so it clears the unit panel. The result has offset clamped to its range.
    CombatLogLayout combatLogLayout(int winW, int winH, int offset, int lineCount, float rightLimit);
    void drawCombatLog(const CombatLogLayout& L, const CombatLog& log);

    // The starting party menu. The buttons come from layoutPartySetup, and this draws them with the
    // party's pictures, names and totals. Call between beginUi and endUi.
    void drawPartySetup(const std::vector<Button>& buttons, const Party& p, const PartyUi& ui,
                        const PartyPanels& P, const std::string& mapName, float mx, float my);
    // A unit's picture, fitted inside r and kept in its proportions
    void drawUnitPicture(int sprite, const Rect& r);

    // Menu drawing. Call beginUi before the first menu draw and endUi after the last.
    void beginUi(int winW, int winH);
    void endUi();
    void drawTitleBackground();                  // clears, then draws title.png covering the screen
    void drawRect(const Rect& r, RGBA c);
    void drawButton(const Rect& r, const std::string& label, bool enabled, bool hovered);
    void drawUiText(const std::string& s, float x, float y, RGBA tint);
    void drawUiTextCentered(const std::string& s, float cx, float y, RGBA tint);
    float uiTextWidth(const std::string& s);
    float uiTextHeight(const std::string& s);

    // Smaller text for dense lists, such as the save list. The font is owned by the caller.
    void setListFont(TTF_Font* font);
    // Fonts for the Stats popout: NotoSans Regular, or Bold if the Regular file is not packed. Owned by the caller.
    void setStatsFonts(TTF_Font* title, TTF_Font* rows);
    void drawListText(const std::string& s, float x, float y, RGBA tint);
    float listTextHeight(const std::string& s);
    float listTextWidth(const std::string& s);
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

    // Text cut to fit maxW at a word boundary, with "..." added. ui picks the menu font, else the list font.
    std::string fitText(const std::string& text, float maxW, bool ui);
    // A quad that changes color from top to bottom
    void drawRectGradient(const Rect& r, RGBA top, RGBA bottom);
    // Clock text: every digit takes the width of the widest digit, so the numbers don't shift
    float digitCellWidth();
    float clockTextWidth(const std::string& s);
    void drawClockText(const std::string& s, float x, float y, RGBA tint);
    // The name and stats of an item, beside the mouse. count is shown as a stack. When compareTo is given
    // and the item is gear, its stats show how they change from the gear it would replace in compareTo,
    // and a card beside it shows the gear it is compared with.
    void drawItemTooltip(int item, int count, float mx, float my, const Equipment* compareTo = nullptr);
    // A tooltip box with its rows, measured and drawn with its top-left corner at (x, y)
    float tipWidth(const std::vector<TipRow>& rows);
    float tipHeight(size_t rowCount);
    void drawTip(const std::vector<TipRow>& rows, float x, float y, float width);

    // Draws a textured quad: 4 vertices of (x, y, z, u, v), in the order BL, BR, TR, TL
    void drawQuadVerts(GLuint tex, const float* v, RGBA tint);

    // The picture for an item, from data/item/<id>.png. An item without one uses data/item/none.png.
    // Null when neither is packed, and the item is drawn as its colored block instead.
    const TextTex* itemTexture(int item);
    // A picture fitted inside r, keeping its proportions, multiplied by tint
    void drawFitted(const TextTex& g, const Rect& r, RGBA tint);

    // Symbol-style billboard that stands on (x, y, z) and turns to face a point 200 units behind the camera
    void drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint);

    // 3D model standing on (x, y, z), scaled so its largest side is targetSize
    void drawModel(const Model& m, float x, float y, float z, float targetSize);

    // Text lines in the top-left corner, each on a dark panel
    void drawHud(const std::vector<std::string>& lines, int winW, int winH);

    // The sprite for a unit, or the fallback symbol if its image is missing
    const TextTex& unitGlyph(World& w, Entity e) const;

    // Ground items on loaded chunks, as pink sprites
    void drawGroundItems(World& w, const Terrain& t);

    // One name box over the items on one tile. Each item has a row, top row first, and the row is
    // the part of the box that picks that item.
    struct GroundStack {
        Rect box;
        std::vector<Entity> items;
        std::vector<std::string> names;
        std::vector<Rect> rows;
    };
    // The name boxes for the items on loaded, visible ground. Drawing and clicking both use this.
    std::vector<GroundStack> groundStacks(World& w, const Terrain& t, const Camera& cam, int winW, int winH);

    // Cached white text texture for a menu string, created on first use
    HudLine& uiLine(const std::string& s);

    // Cached white text texture for a list string, using the list font
    HudLine& listLine(const std::string& s);
    // A font with its own cache of text textures, for the Stats popout
    struct TextFont {
        TTF_Font* font = nullptr;
        std::map<std::string, HudLine> lines;
    };
    HudLine& fontLine(TextFont& f, const std::string& s);
    void drawFontText(TextFont& f, const std::string& s, float x, float y, RGBA tint);
    float fontTextWidth(TextFont& f, const std::string& s);
    float fontTextHeight(TextFont& f, const std::string& s);

    GLuint colorProgram_ = 0, glyphProgram_ = 0;
    GLint uMVPColor_ = -1, uMVPGlyph_ = -1, uTint_ = -1;

    GLuint dynVao_ = 0, dynVbo_ = 0;          // color overlays
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

    // Terrain, sea and seabed shaders and textures (renderer_terrain.cpp)
    struct TerrainShader {
        GLuint prog = 0;
        GLint viewProj = -1, eye = -1, seaY = -1, depthScale = -1, rockRgb = -1, rockSlope = -1, sunDir = -1;
        GLint sunLight = -1, ambient = -1, underwater = -1, fogColor = -1, fog = -1, tile = -1;
    };
    struct SeaShader {
        GLuint prog = 0;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, sunDir = -1, sunLight = -1, ambient = -1, color = -1;
        GLint fogColor = -1, fog = -1, time = -1, opacity = -1, pattern = -1, speed = -1, fadeStart = -1, fadeEnd = -1, tile = -1;
    };
    struct FloorShader {
        GLuint prog = 0;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, ambient = -1, sunLight = -1, underwater = -1, fogColor = -1, fog = -1;
    };
    TerrainShader groundShader_;
    SeaShader seaShader_;
    FloorShader floorShader_;
    GLuint groundTex_ = 0, seaTex_ = 0;      // the surface tiles of the world being drawn
    uint32_t surfaceVersion_ = 0;            // which world's surface those tiles came from
    GLuint emptyVao_ = 0;                    // for shaders that build their quads from gl_VertexID
    float skySun_[3] = { 0.0f, 1.0f, 0.0f }; // the sun the sky draws, which lights the terrain
    bool initTerrainGl();
    void releaseTerrainGl();
    void uploadSurfaceTiles(const Terrain& t);

    TextTex title_;
    std::vector<TextTex> unitSprites_;
    // Text symbols, used only when a sprite file could not be loaded
    TextTex unitFallback_;
    // Item pictures by item id, loaded on first use. An id with no file holds an empty texture.
    std::map<int, TextTex> itemPics_;
    TextTex itemNone_;   // data/item/none.png, for items without a picture of their own
    // A plain white texel. Items are drawn as this, tinted pink, until they have art.
    TextTex white_;
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
    TextFont statsTitle_, statsRow_;
    Mat4 pixels_{};
    int uiW_ = 0, uiH_ = 0;

    Mat4 viewProj_{};

    // Sky: a gradient and sun drawn as a full-screen quad, colored by the time of day
    GLuint skyProgram_ = 0, skyVao_ = 0, skyVbo_ = 0;
    GLint uSkyInvVP_ = -1, uSunDir_ = -1, uSunAlpha_ = -1, uZenith_ = -1, uHorizon_ = -1, uSunCol_ = -1;
    GLint uMoonDir_ = -1, uMoonAlpha_ = -1, uMoonCol_ = -1, uStarAlpha_ = -1;
    float dayMinutes_ = 360.0f;
    SkyTuning sky_;
    // The point sprites turn to face: 200 units behind the camera
    float billX_ = 0.0f, billZ_ = 0.0f;
};
