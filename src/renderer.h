#pragma once
#include <array>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include "camera.h"
#include "game_tables.h"
#include "map_theme.h"
#include "sky.h"
#include "ecs.h"
#include "factions.h"
#include "game_log.h"
#include "items.h"
#include "mobs.h"
#include "model.h"
#include "terrain_mesh.h"
#include "ui_types.h"
#include "props.h"
#include "overview.h"
#include "text_cache.h"

class LoadStages;
class Terrain;
struct BuildPlan;
struct FloatingNumber;
struct TradeLabel;
struct Puff;
struct PartyMember;
struct PartyUi;
struct PartyPanels;
struct Stats;
enum class StatId : int;
struct BehaviorRule;
struct SpokenLine;
struct DialogueOption;
struct Equipment;
struct Inventory;
struct ItemStack;
struct Storage;
struct Output;
struct TradeGhost;
struct TradeStatus;
struct SidebarLayout;
struct UnitPanelLayout;
struct WorldMapLayout;
struct DialogueLayout;
struct LootLayout;
struct StorageLayout;
struct TradeLayout;
struct OptimizeAllLayout;
struct QuantityLayout;
struct DisbandLayout;
struct ItemMenuLayout;
struct AltarLayout;
struct AltarTotals;
struct AltarRow;
struct SquadsLayout;
struct UnitCardLayout;
struct TitleLayout;
struct PercentPromptLayout;
namespace render_detail { struct Vertex; struct Batch; }

#include "renderer_types.h"

class Renderer {
public:
    bool init(TTF_Font* hudFont, TTF_Font* uiFont,
              const std::function<void(float, const std::string&)>& progress = {});
    void shutdown();

    void setDayMinutes(float m) { dayMinutes_ = m; }
    // From the Graphics settings: the clouds overhead, their shadows on the ground, and ground and water tiles laid so
    // they don't repeat (off: one plain repeating tile, the cheapest)
    void setGraphicsOptions(bool clouds, bool cloudShadows, bool variedSurfaces) {
        cloudsOn_ = clouds;
        cloudShadowsOn_ = cloudShadows;
        variedSurfaces_ = variedSurfaces;
    }
    // The time the graphics card spends on the ground, the water and the clouds, in milliseconds a frame, smoothed over
    // the last frames. Only measured while switched on.
    struct PassTimes {
        float ground = 0.0f, water = 0.0f, clouds = 0.0f;
    };
    void setPassTimers(bool on) { passTimersOn_ = on; }
    PassTimes passTimes() const { return PassTimes{ groundTimer_.ms, waterTimer_.ms, cloudTimer_.ms }; }
    // From the Graphics settings: the world is drawn at this percent of the window's width and height and stretched to
    // fit, which costs fewer pixels. The interface is always drawn at the window's own size.
    void setRenderScale(int percent) { renderScale_ = percent; }
    // The detail distance from the settings, in tiles: props and units past it are not drawn
    void setDetailRange(int tiles) { detailRange_ = tiles; }
    // The view distance from the settings, in tiles: ground chunks wholly beyond it from the view's centre aren't drawn
    void setViewDistance(int tiles) { viewDistance_ = tiles; }
    std::string renderScaleText() const;
    // The distances the ground's and water's detail is drawn to, in words, for the debug log
    std::string surfaceBandsText() const;
    // For measuring: off draws the ground's and water's detail at every distance, as it was before the distance bands
    void setSurfaceBands(bool on) { surfaceBands_ = on; }
    // Shows a map's theme at once. uploadTerrain sets it from the map when one loads.
    void setMapTheme(const MapTheme& theme);

    void uploadTerrain(const Terrain& t);

    void drawLoadingScreen(float progress, int winW, int winH, const std::string& label = std::string(),
                           const std::string& category = std::string(), bool backdrop = false);
    // Picks one of the loading pictures again at random, for the next loading screen
    void pickLoadingArt();

    std::vector<ObjectId> entitiesAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);
    std::vector<ObjectId> propsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);
    std::vector<ObjectId> groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                           int winW, int winH);
    ObjectId nearestGroundItem(const std::vector<ObjectId>& items, World& w, const Camera& cam, float mx, float my,
                             int winW, int winH);

    void drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                   const std::vector<HudEntry>& hud, int winW, int winH);

    void drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH);
    struct FormationHelpEntry {
        std::string key, label;
        std::vector<Vec2> dots;   // a little picture of what it does, with -z at the top
        bool current = false;
    };
    // The Form button's popout: one entry per row, scrolled when it does not all fit
    struct FormationMenuLayout {
        Rect box;
        std::vector<Rect> rows;   // the shown rows, entry first + i
        int first = 0, count = 0;
        Rect track, thumb;        // empty when everything fits
    };
    FormationMenuLayout formationMenuLayout(const Rect& button, int count, int scroll, int winH);
    void drawFormationMenu(const FormationMenuLayout& L, const std::vector<FormationHelpEntry>& entries, float mx, float my);
    void drawFormationPreview(const Camera& cam, const Terrain& t, const std::vector<Vec2>& spots, int winW, int winH);

    void setBuildSelected(const BuildPick& pick) { buildSelected_ = pick; }
    void setBuildInClaim(bool in) { buildInClaim_ = in; }
    void setDeveloperOpen(bool on) { developerOpen_ = on; }
    void setPlanPopupOpen(bool on) { planPopupOpen_ = on; }
    void setBuildPreview(BuildPreview p) { buildPreview_ = std::move(p); }
    // The cutaway: inside a building, what stands on the floors above the one looked at is left out, drawn and picked
    struct Cutaway {
        bool on = false;
        int level = 0;
        std::unordered_set<long long> tiles;   // the building's tiles, by tileKey
    };
    void setCutaway(Cutaway c) { cutaway_ = std::move(c); }
    // Whether the cutaway leaves out what stands on this tile on this floor
    bool tileCutAway(int x, int z, int level) const { return cutaway_.on && level > cutaway_.level && cutaway_.tiles.count((long long)z * 1000003LL + x) > 0; }   // tileKey
    // Whether the cutaway leaves this out
    bool cutAway(World& w, ObjectId e) const;
    // Whether a click can't reach this: left out by the cutaway, or under the floor looked at
    bool pickHidden(World& w, ObjectId e) const;
    void drawBuildPreview(const Terrain& t);
    void drawBuildSection(const SidebarLayout& L, float mx, float my);

    void drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha, int winW, int winH);

    // renameText: the squad name being typed, or null when no squad is being renamed
    void drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<ObjectId>& units, bool running,
                       int repView, const std::string* renameText = nullptr, int floorLevel = 0, bool floorManual = false, bool formOpen = false);
    void drawWorldMap(const WorldMapLayout& L, const std::vector<MapDot>& dots, float cx, float cz, float span,
                      const WorldMapMarks& marks);
    void drawDialogue(const DialogueLayout& L, const std::vector<SpokenLine>& convo,
                      const std::vector<DialogueOption>& options, float mx, float my);
    // Kept for a text and width asked about again (a panel's help text is wrapped every frame it shows), until the font changes
    std::vector<std::string> wrapListText(const std::string& text, float maxW);
    std::vector<std::string> wrapListTextUncached(const std::string& text, float maxW);
    std::unordered_map<std::string, std::vector<std::string>> wrapListCache_;
    const void* wrapListFont_ = nullptr;
    int drawStatsTab(const SidebarLayout& L, const Stats& stats, float mx, float my, bool mob = false);
    void drawStatTip(const Stats& stats, StatId stat, float mx, float my);
    void drawInventorySidebar(const SidebarLayout& L, World& w, ObjectId unit, float mx, float my,
                              int dimSlot, int dimGear, int dragItem, bool editable);
    void drawBehaviorTip(const SidebarLayout& L, World& w, ObjectId unit, float mx, float my);
    void beginClip(const Rect& r);
    void endClip();
    void drawDraggedStack(float mx, float my, int item, int count);
    void drawItemCell(const Rect& r, int item, int count, float alpha = 1.0f);
    void drawPauseBar(bool hint);
    float pauseBarHeight(bool hint);
    void drawTradeLabels(const std::vector<TradeLabel>& labels, World& w, const Camera& cam, int winW, int winH);
    void drawPuffs(const std::vector<Puff>& puffs, const Camera& cam, int winW, int winH);
    void drawUiTextLarge(const std::string& s, float cx, float cy, float scale, RGBA tint);
    void drawLoot(const LootLayout& L, const std::string& name, const Equipment* worn, const Inventory* inv, float mx, float my);
    void drawStorage(const StorageLayout& L, const std::string& propName, const std::string& hint,
                     const Storage* storage, const Output* output, const std::string& status, int dragFrom,
                     float mx, float my, const std::string& toggleLabel = std::string(), int scroll = 0);
    void drawEmotes(World& w, const Camera& cam, int winW, int winH, bool playerNames, bool otherNames);
    void drawPropEmotes(World& w, const Camera& cam, int winW, int winH);
    void drawHoverCue(World& w, const Camera& cam, int winW, int winH, ObjectId u, EmoteId emote, bool namesShown);
    void drawPropProgress(World& w, const Camera& cam, int winW, int winH);
    void drawTrade(const TradeLayout& L, const std::string& npcName, const std::string& yourName, const Inventory* npcInv,
                   const std::vector<TradeGhost>& theirs, const std::vector<TradeGhost>& yours, int dragFrom,
                   const TradeStatus& status, bool showValue, float mx, float my);
    // The altar's roster. cols: each column's units, ghosts (units moving into it) included
    void drawAltarRoster(World& w, const AltarLayout& L, const std::array<std::vector<ObjectId>, 2>& cols,
                         const std::map<ObjectId, int>& moves, const AltarTotals& totals, ObjectId pinned, float mx, float my);
    // Manage Squads. heads: each column's squad name; totals: the line under the title (warn: drawn as a warning)
    struct SquadsRosterText {
        std::array<std::string, 2> heads, counts;
        std::array<bool, 2> full{};
        std::string totals;
        bool warn = false;
        bool canApply = false;
        int moveCount = 0;
    };
    void drawSquadsRoster(World& w, const SquadsLayout& L, const std::array<std::vector<ObjectId>, 2>& cols,
                          const std::map<ObjectId, int>& moves, const SquadsRosterText& text, ObjectId pinned, float mx, float my);
    // One unit's row in the roster or Manage Squads: portrait, name, levels, a tag at its right, and the move arrow
    // (pointing right, or left). A ghost is a unit moving in, until Apply.
    void drawRosterRow(World& w, const AltarRow& r, ObjectId u, bool ghost, const std::string& tag, bool pointRight,
                       const char* moveTipKey, ObjectId pinned, float mx, float my);
    void drawUnitCard(World& w, ObjectId u, const UnitCardLayout& C);
    // Appease Ghosts. ghosts: the Graveyard's ghosts in the order shown
    void drawGhostsWindow(World& w, const AltarLayout& L, const std::vector<ItemStack>& bones, const std::vector<size_t>& ghosts,
                          int bonesTotal, float mx, float my);
    // Arrows over the units a box would select, while it is dragged
    void drawSelectArrows(World& w, const Camera& cam, const std::vector<ObjectId>& units, int winW, int winH);
    void drawOptimizeAll(const OptimizeAllLayout& L, int unitCount, int attackShare, const std::array<bool, 3>& toggles,
                         float mx, float my);
    void drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                            float mx, float my);
    void drawDisband(World& w, const DisbandLayout& L, ObjectId unit, float held, float mx, float my);
    void drawNotice(const std::string& text, int winW, int winH);
    ItemMenuLayout itemMenuLayout(float x, float y, int buttonCount, int winW, int winH);
    void drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my);

    void drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH, float mx, float my);
    ObjectId groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH,
                             bool labels = true);
    void drawGroundItemName(World& w, const Camera& cam, int winW, int winH, ObjectId item);

    void drawUnitNames(World& w, const Camera& cam, int winW, int winH, bool players, bool others);
    void drawPropTooltip(const std::vector<std::string>& lines, float mx, float my);
    void showItemTooltip(int item, int count, float mx, float my) { drawItemTooltip(item, count, mx, my); }

    void drawFloatingNumbers(const std::vector<FloatingNumber>& floats, const Camera& cam, int winW, int winH);
    GameLogLayout gameLogLayout(int winW, int winH, int tab, bool filterOpen, int offset, int lineCount,
                                    bool collapsed = false, const PanelPlace& place = PanelPlace{});
    void drawGameLog(const GameLogLayout& L, const std::vector<const LogLine*>& rows, const LogFilter& filter,
                       float mx, float my, const InfoTime& time = InfoTime{});

    void drawPartySetup(const std::vector<Button>& buttons, const std::vector<PartyMember>& p, const PartyUi& ui,
                        const PartyPanels& P, float mx, float my);
    void drawUnitPicture(int sprite, const Rect& r);
    void drawUnitModelPreview(int sprite, const Rect& r, float yaw);
    void drawMobPreview(MobKind kind, const Rect& r, float yaw);
    void drawModelPreview(const Model* m, const Rect& r, float yaw);
    // A Building Blueprint's pieces drawn as the game draws them, turned by yaw about the middle of the building and looked down
    // on, in r (the Build tab's Building Blueprint popout). The pieces' places are worked out once for each plan (forgetPlanPreview
    // makes it do so again).
    void drawPlanPreview(const BuildPlan& plan, const Rect& r, float yaw);
    void forgetPlanPreview() { planKey_.clear(); }
    const TextTex& mobPicture(MobKind kind) const;

    void beginUi(int winW, int winH);
    void endUi();
    void drawTitleBackground();
    void drawTitleHeader(const TitleLayout& L, const std::string& first, const std::string& second,
                         const std::vector<std::string>& news);
    void drawMenuItem(const Rect& r, const std::string& label, const std::string& sub, bool selected, bool enabled,
                      float appear);
    void drawTitleFooter(const Rect& footer, const std::string& left, const std::string& right);
    void drawThemedPanel(const Rect& r);
    void setWorldPreview(int px, const std::vector<uint8_t>& rgba);
    void drawWorldPreview(const Rect& r);
    std::string fitListText(const std::string& text, float maxW) { return fitText(text, maxW, false); }
    void drawUiTris(const std::vector<UiVert>& v);
    void drawRectGradientH(const Rect& r, RGBA left, RGBA right);
    void noteHover(const Rect& r);
    void setTitleFont(TTF_Font* font);
    void hoverFrameStart() { hoverAny_ = false; }
    bool hoverFrameEnd();
    void drawRect(const Rect& r, RGBA c);
    // The fires of the props in view this frame (props.txt flag fire): flames, and a warm glow as their light
    struct FireSpot {
        float x, y, z, size, seed;
        float energy = 0.0f;   // 0 to 1: a motivated faction's Altar burns bigger, brighter and quicker
    };
    std::vector<FireSpot> fires_;
    TextTex glow_;   // a soft round spot, bright in the middle
    void drawFires(const Camera& cam);
    // A straight line and a filled circle, in pixels (the Adjust tool's widget)
    void drawLine2D(float x0, float y0, float x1, float y1, float thick, RGBA c);
    void drawDisc(float x, float y, float radius, RGBA c);
    void drawRoundedRect(const Rect& r, float radius, RGBA c);
    void drawUiIcon(int icon, const Rect& r, RGBA tint = RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    void drawIconText(float x, float y, float h, int icon, const std::string& text, RGBA tint);
    void drawIconLabel(const Rect& r, int icon, const std::string& text, RGBA tint, bool uiFont);
    void drawTitleBar(const Rect& bar, int icon, const std::string& title);
    void drawCollapseButton(const Rect& r, bool collapsed);
    void drawCloseButton(const Rect& r);
    void drawReputationList(World& w, const UnitPanelLayout& L, int repView);
    void drawSquadGrid(World& w, const UnitPanelLayout& L, const std::string* renameText);
    void drawLockButton(const Rect& r, bool locked);
    void drawButton(const Rect& r, const std::string& label, bool enabled, bool hovered);
    void drawScrollBar(const Rect& track, const Rect& thumb, bool hot = false);
    void drawScrim();
    void drawUiText(const std::string& s, float x, float y, RGBA tint);
    void drawUiTextCentered(const std::string& s, float cx, float y, RGBA tint);
    void setMouse(float x, float y) { mouseX_ = x; mouseY_ = y; }
    void noteTip(const std::string& full, const Rect& area);
    void drawUiTextInBox(const std::string& s, const Rect& box, RGBA tint);
    void drawUiTextScaled(const std::string& s, float x, float y, float k, RGBA tint);
    std::string fitShrinkText(const std::string& s, float boxW);
    float fitShrinkScale(const std::string& shown, float boxW, float boxH);
    float uiTextWidth(const std::string& s);
    float uiTextHeight(const std::string& s);
    float profileCardHeight(int H);

    void setListFont(TTF_Font* font);
    void setFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list);
    void setMenuFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list);
    void setMenuText(bool on) { text_.setMenuText(on); }
    bool menuText() const { return text_.menuText(); }
    void drawBigText(const std::string& text, float cx, float cy, float h, RGBA tint);

    void drawListText(const std::string& s, float x, float y, RGBA tint);
    float listTextHeight(const std::string& s);
    float listTextWidth(const std::string& s);
    void drawListButton(const Rect& r, const std::string& label, bool hovered);
    void drawHoldButton(const Rect& r, const std::string& label, float fill);

    // A small picture of the world as last drawn, without the UI, kept beside a save to show in the save list
    // (renderer_preview.cpp). The next drawFrame takes it.
    void requestSceneGrab() { grabWanted_ = true; }
    // The latest picture, or null if none has been taken yet
    const std::vector<uint8_t>* sceneGrab(int& w, int& h) const;
    // What drawSavePreview shows: a saved picture file, or the latest grab. False if there is nothing to show.
    bool previewFromFile(const std::string& path);
    bool previewFromGrab();
    void drawSavePreview(const Rect& r);
    void forgetPreview() { previewKey_.clear(); }   // a picture file was written: read it again next time
    void releasePreviewGl();

private:
    void grabScene();
    bool grabWanted_ = false;
    std::vector<uint8_t> grab_;
    int grabW_ = 0, grabH_ = 0;
    unsigned grabCount_ = 0;
    GLuint grabFbo_[2] = { 0, 0 };
    GLuint grabTex_[2] = { 0, 0 };
    GLuint previewTex_ = 0;
    std::string previewKey_;
    bool previewShown_ = false;

    bool initGl();
    void loadLoadingArt();
    void loadPictures(LoadStages& stages);
    void loadUnitModels(LoadStages& stages);
    void loadMobArt(LoadStages& stages);
    void initSky();
    void makeTexels();
    void loadPropModels(LoadStages& stages);

    std::string fitText(const std::string& text, float maxW, bool ui);
    std::string fitTextTip(const std::string& text, float maxW, const Rect& tipArea);
    void drawBehaviorSection(const SidebarLayout& L, const std::vector<BehaviorRule>& rules, const std::string& task,
                             float mx, float my, bool editable, const std::array<bool, 4>& modes = {});
public:
    void drawPercentPrompt(const PercentPromptLayout& P, const std::string& title, const std::string& typed,
                           const std::string& range, const std::string& error, float mx, float my);

private:
    void drawSidebarScrollBar(const SidebarLayout& L);
    void drawWrappedText(const std::string& text, const Rect& box, RGBA tint);
    enum class CellLight { UnderMouse, WhenFilled };
    int drawStackGrid(const Rect& area, const std::vector<Rect>& cells, const ItemStack* stacks, size_t stackCount,
                      int skip, float mx, float my, CellLight light = CellLight::UnderMouse);
    void drawInteractionHeader(const Rect& bar, const Rect& nameBox, const std::string& name);
    void drawSmallButton(const Rect& r, const std::string& label, bool enabled, bool hovered);
    void drawRectGradient(const Rect& r, RGBA top, RGBA bottom);
    float digitCellWidth();
    float clockTextWidth(const std::string& s);
    void drawClockText(const std::string& s, float x, float y, RGBA tint);
    void drawItemTooltip(int item, int count, float mx, float my, const Equipment* compareTo = nullptr);
    float tipWidth(const std::vector<TipRow>& rows);
    float tipHeight(size_t rowCount);
    void drawTip(const std::vector<TipRow>& rows, float x, float y, float width);

    void drawQuadVerts(GLuint tex, const float* v, RGBA tint);
    void drawColorTris(const render_detail::Vertex* v, size_t count, const Mat4& mvp);
    void drawColorTris(const render_detail::Batch& b, const Mat4& mvp);
    void drawTexture(GLuint tex, float x, float y, float w, float h, RGBA tint);
    void drawPortrait(const TextTex& g, const Rect& square, float saturation);
    void drawUiQuad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                    RGBA tint);
    void drawDot(float cx, float cy, float d);
    void pollOverview();
    OverviewBuilder overview_;
    const TextTex& uiIconTexture(int icon) { return uiIcons_.get(icon); }
    PictureCache uiIcons_{ "data/icon/", false, iconFile };
    GLuint overviewTex_ = 0;
    GLuint worldPreviewTex_ = 0;
    float overviewSize_ = 0.0f;
    float overviewExtent_ = 1.0f;
    GLuint dotTex_ = 0;
    GLuint dotWhiteTex_ = 0;
    float hudTop_ = 12.0f;

    const TextTex* itemTexture(int item);
    bool itemPixelSolid(int item, float u, float v);
    void drawItemPicture(const Rect& cell, int item, float alpha);
    void drawItemPictureAt(float cx, float cy, int item, float alpha);
    void drawFitted(const TextTex& g, const Rect& r, RGBA tint);

    void drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint);
    void drawLyingGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint, float toward);
    void setSaturation(float sat);
    float unitTop(World& w, ObjectId u);
    float unitHover(World& w, ObjectId u) const;
    float bedLift(World& w, ObjectId u) const;
    struct UnitPose {
        bool lying = false;
        bool carried = false;
        float lift = 0.0f;
    };
    UnitPose unitPose(World& w, ObjectId e) const;

    void drawModel(const Model& m, float x, float y, float z, float targetSize, float yaw = 0.0f);

    void drawHud(const std::vector<HudEntry>& lines, int winW, int winH);

    const TextTex& unitGlyph(World& w, ObjectId e) const;

    void drawGroundItems(World& w, const Terrain& t, const Camera& cam);

    struct GroundStack {
        Rect box;
        std::vector<ObjectId> items;
        std::vector<std::string> names;
        std::vector<Rect> rows;
        float dist2 = 0.0f;
    };
    std::vector<GroundStack> groundStacks(World& w, const Terrain& t, const Camera& cam, int winW, int winH);
    static bool topLabelRow(const std::vector<GroundStack>& stacks, float mx, float my, size_t& stack, size_t& row);

    const TextLine& uiLine(const std::string& s) { return text_.ui.line(s); }
    const TextLine& listLine(const std::string& s) { return text_.list.line(s); }
    void drawFontText(TextFont& f, const std::string& s, float x, float y, RGBA tint);

    GLuint colorProgram_ = 0, glyphProgram_ = 0;
    GLint uMVPColor_ = -1, uMVPGlyph_ = -1, uTint_ = -1, uSat_ = -1, uShine_ = -1, uFaction_ = -1, uHue_ = -1;

    GLuint dynVao_ = 0, dynVbo_ = 0;
    BuildPick buildSelected_;
    bool buildInClaim_ = false;
    bool planPopupOpen_ = false;   // the Build tab's Building Blueprint popout is open
    bool developerOpen_ = false;   // the Build tab's Developer spawn panel is open
    BuildPreview buildPreview_;
    Cutaway cutaway_;
    GLuint glyphVao_ = 0, glyphVbo_ = 0;
    struct TerrainChunk {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei indexCount = 0;
        int step = 0;
        int nbStep[4] = { 0, 0, 0, 0 };
        uint32_t version = 0;
        ChunkBounds bounds{};
    };
    std::vector<TerrainChunk> chunks_;
    int terrainSize_ = 0;
    int chunksPerSide_ = 0;
    ChunkMesh chunkScratch_;
    void releaseTerrain();
    void releaseChunk(TerrainChunk& c);
    void buildChunk(const Terrain& t, const ChunkData& c, int id, int step, const int* nbStep);
    void drawTerrain(const Terrain& t, const Camera& cam);

    // The uniforms of kSurfaceCommon, which the ground, water and cloud shaders all have
    struct SurfaceUniforms {
        GLint origin = -1, noise = -1, varied = -1, salt = -1, macro = -1, cloudShadow = -1, shadowSun = -1;
        GLint cloudShift = -1, cloudScale = -1, cloudHeight = -1, cloudOpacity = -1, cloudCover = -1, bands = -1;
        void find(GLuint prog);
    };
    struct TerrainShader {
        GLuint prog = 0;
        GLint viewProj = -1, eye = -1, seaY = -1, depthScale = -1, rockRgb = -1, rockSlope = -1, sunDir = -1;
        GLint voidCut = -1, groundTint = -1, tintAmount = -1;
        GLint sunLight = -1, ambient = -1, underwater = -1, fogColor = -1, fog = -1, tile = -1;
        GLint tileShift = -1, tileSize = -1;
        SurfaceUniforms common;
    };
    struct SeaShader {
        GLuint prog = 0;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, sunDir = -1, sunLight = -1, ambient = -1, color = -1;
        GLint glitter = -1, glow = -1, mapSize = -1;
        GLint fogColor = -1, fog = -1, time = -1, opacity = -1, pattern = -1, speed = -1, fadeStart = -1, fadeEnd = -1, tile = -1;
        GLint tileShift = -1, tileSizes = -1, evenOut = -1;
        SurfaceUniforms common;
    };
    struct CloudShader {
        GLuint prog = 0;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, layer = -1, lit = -1, fogColor = -1, fog = -1;
        GLint fadeStart = -1, fadeEnd = -1, edge = -1;
        SurfaceUniforms common;
    };
    // Worked out once a frame, in doubles, for every surface shader: the point near the camera that positions are measured
    // from, where each grid of tiles falls from it, and where the drifting clouds are
    struct SurfaceFrame {
        double originX = 0.0, originZ = 0.0;
        float groundShift[2] = {}, waterShift[4] = {};
        float macro[3] = {};
        float cloudShift[4] = {}, cloudScale[2] = {}, cloudHeight[2] = {}, cloudOpacity[2] = {};
        float cloudCover = 0.5f, cloudShadow = 0.0f;
        float sun[3] = { 0.0f, 1.0f, 0.0f };
        float cloudLit[3] = { 1.0f, 1.0f, 1.0f };
        float fogColor[3] = {}, fog = 0.0f;
        bool clouds = false;
    };
    void setSurfaceUniforms(const SurfaceUniforms& u) const;
    void updateSurfaceFrame(const Camera& cam, const SkyLight& sky);
    void drawClouds(const Camera& cam);
    struct FloorShader {
        GLuint prog = 0;
        GLint mapSize = -1;
        GLint viewProj = -1, center = -1, half = -1, eye = -1, ambient = -1, sunLight = -1, underwater = -1, fogColor = -1, fog = -1;
    };
    TerrainShader groundShader_;
    SeaShader seaShader_;
    FloorShader floorShader_;
    CloudShader cloudShader_;
    SurfaceFrame surfaceFrame_;
    GLuint noiseTex_ = 0;
    uint32_t surfaceSeed_ = 0;
    bool cloudsOn_ = true, cloudShadowsOn_ = true, variedSurfaces_ = true;
    bool surfaceBands_ = true;
    int renderScale_ = 100;
    int detailRange_ = 256;
    int viewDistance_ = 1024;
    // The walls' and doors' tiles, worked out again only when a prop or a storey is added or removed (see drawWorld)
    struct BuildingTiles {
        bool valid = false;
        uint64_t propRevision = 0, storeyRevision = 0;
        std::unordered_set<long long> walls;   // by levelKey: walls and doors
        std::unordered_set<long long> doors;   // by levelKey: doors only
    };
    BuildingTiles buildingTiles_;
    GLuint sceneFbo_ = 0, sceneTex_ = 0, sceneDepth_ = 0;
    int sceneW_ = 0, sceneH_ = 0, sceneWinW_ = 0, sceneWinH_ = 0;
    bool sceneActive_ = false;
    bool heldDragging_ = false;
    float heldBox_[4] = {};
    std::vector<HudEntry> heldHud_;
    void beginScene(int winW, int winH);
    void presentScene();
    void releaseScene();
    void drawFrameTop(bool dragging, const float* corners, const std::vector<HudEntry>& hud, int winW, int winH);
    struct PassTimer {
        GLuint query = 0;
        bool running = false, waiting = false;
        float ms = 0.0f;
    };
    PassTimer groundTimer_, waterTimer_, cloudTimer_;
    bool passTimersOn_ = false;
    void passTimerBegin(PassTimer& t);
    void passTimerEnd(PassTimer& t);
    GLuint groundTex_ = 0, seaTex_ = 0;
    uint32_t surfaceVersion_ = 0;
    GLuint emptyVao_ = 0;
    float skySun_[3] = { 0.0f, 1.0f, 0.0f };
    bool initTerrainGl();
    void releaseTerrainGl();
    void uploadSurfaceTiles(const Terrain& t);

    TTF_Font* titleFont_ = nullptr;
    std::map<std::string, TextTex> titleWords_;
    const TextTex& titleWord(const std::string& text);
    TextTex titleLogo_;
    TextTex loadingArt_;
    bool hoverAny_ = false;
    Rect hoverNow_, hoverLast_;
    bool hoverHad_ = false;
    float titleMouse_ = 0.0f;
    unsigned long long titleMouseAt_ = 0;
    std::vector<TextTex> mobSprites_;
    std::vector<TextTex> mobProfiles_;   // data/profile/<its model, or its picture>.png, by kind
    std::array<TextTex, UNIT_SPRITE_COUNT> profilePics_{};
    const TextTex& unitPortrait(World& w, ObjectId e) const;
    const TextTex& unitPicture(int n) const;
    std::array<Model, UNIT_SPRITE_COUNT> unitModels_{};
    std::array<const Model*, UNIT_SPRITE_COUNT> unitModelFor_{};
    std::vector<Model> mobModels_;
    float unitHeight(World& w, ObjectId e) const;
    const Model* unitModelNumber(int n) const;
    const Model* unitModel(World& w, ObjectId e) const;
    float unitAspect(World& w, ObjectId e) const;
    Mat4 unitWorldMatrix(World& w, ObjectId e, const Model& m, float yaw) const;
    void drawUnitModel(const Model& m, const Mat4& placement);
    RGBA unitFactionTint(World& w, ObjectId e) const;
    // A boss's Coloration, in turns of hue
    float unitHue(World& w, ObjectId e) const;
    struct UnitFacing {
        float x = 0.0f, z = 0.0f;
        float yaw = 0.0f;
        unsigned long long idleSince = 0;
        bool settled = false;
        float settleX = 0.0f, settleZ = 0.0f;
        unsigned long long turnedAt = 0;
    };
    std::map<ObjectId, UnitFacing> unitFacing_;
    float unitYaw(ObjectId e, float x, float z, bool idle);
    PictureCache itemPics_{ "data/item/", true, itemPictureFile };
    PictureCache emotePics_{ "data/emote/", false, emoteFile };
    const TextTex* emoteTexture(EmoteId emote);
    TextTex pink_;
    TextTex white_;
    struct PropHop {
        unsigned made = 0;
        float start = -1.0f;
    };
    std::map<ObjectId, PropHop> propHops_;
    void drawRing(float cx, float cy, float outer, float inner, float from, float to, RGBA color);
    std::vector<Model> propModels_;
    // A wall's other shapes (end, straight, corner, tee, cross), for each kind with the wall_rule flag
    std::vector<std::array<Model, 5>> wallShapes_;
    // The same in iron, crystal and stone, for the kinds made of a material: [kind][material - 1][0 plain, 1.. the wall shapes]
    static constexpr int MATERIAL_MODELS = 3;
    std::vector<std::array<std::array<Model, 6>, MATERIAL_MODELS>> materialModels_;
    std::array<Model, 4> stairSteps_;   // one step of stairs in each material (by BuildMaterial), stretched to each step's height
    const Model& propModel(PropKind kind) const;
    GLuint modelInstProgram_ = 0;
    GLint uVPInst_ = -1;
    GLint uGrainOnInst_ = -1;
    // Each building material's texture, by BuildMaterial (tools/make_building_textures.py), laid over its pieces
    std::array<TextTex, 4> surfaces_{};
    GLuint instVbo_ = 0;
    // One copy of a model in the world: where it is, and the faction color and saturation a unit is drawn with
    struct ModelInstance {
        Mat4 place;
        float faction[4];
        float sat;
        float hue;   // turns of hue shift
        float pad[2];
    };
    // Every model drawn this frame, each with its copies, so each model is one draw however many copies it has. The
    // lists keep their room from frame to frame.
    std::vector<std::pair<const Model*, std::vector<ModelInstance>>> instQueues_;
    std::unordered_map<const Model*, size_t> instSlot_;
    std::vector<ModelInstance> instUpload_;
    GLuint modelInstVao(const Model& m);
    struct PlanInstance {
        const Model* model;
        Mat4 place;
    };
    std::vector<PlanInstance> planInst_;   // the plan shown in the popout, as copies of models
    std::string planKey_;
    float planFocus_[3] = {};              // the middle of it, and how far across it is
    float planSpan_ = 1.0f;
    RGBA propTint_{ 0.0f, 0.0f, 0.0f, 0.0f };   // what queueModel tints a model with: red for a building being pulled down
    void queueModel(const Model& m, float x, float y, float z, float targetSize, float yaw);
    void queueModelMatrix(const Model& m, const Mat4& place, RGBA faction, float sat, float hue = 0.0f);
    void queuePropModel(PropKind kind, float x, float y, float z, float targetSize, float yaw);
    void drawModelInstances();
    struct ModelDrawCount { int models = 0, copies = 0, draws = 0; } modelDraws_;
public:
    // The last frame's instanced models: how many models, copies of them, and draw calls
    std::string modelDrawText() const;
private:
    float propTop(PropKind kind) const;
    float propBaseY(World& w, ObjectId e) const;

    TextCache text_;
    Mat4 pixels_{};
    int uiW_ = 0, uiH_ = 0;
    float uiScale() const { return uiH_ / 720.0f; }
    float mouseX_ = 0.0f, mouseY_ = 0.0f;
    std::string tip_;
    void drawTip();

    Mat4 viewProj_{};

    GLuint skyProgram_ = 0, skyVao_ = 0, skyVbo_ = 0;
    GLint uSkyInvVP_ = -1, uSunDir_ = -1, uSunAlpha_ = -1, uZenith_ = -1, uHorizon_ = -1, uSunCol_ = -1;
    GLint uMoonDir_ = -1, uMoonAlpha_ = -1, uMoonCol_ = -1, uStarAlpha_ = -1;
    float dayMinutes_ = 360.0f;
    SkyColors sky_;
    bool hasTheme_ = false;
    bool seaBounded_ = false;
    MapTheme theme_;
    Look themeLook_;
    SkyColors themeSky_;
    float worldTint_[3] = { 1.0f, 1.0f, 1.0f };   // the light on props and units
    GLint uSkyKind_ = -1, uSkyBaked_ = -1, uSkyUseBaked_ = -1, uTintInst_ = -1;
    GLuint skyCube_ = 0, skyBakeFbo_ = 0;
    bool skyBakeWanted_ = true;
    Uint64 skyBakeNotBefore_ = 0;   // a theme edited while dragging a slider is baked at most this often, not on every mouse move
    void bakeSky(int skyKind, const float* zenith, const float* horizon, float stars);
    float billX_ = 0.0f, billZ_ = 0.0f;
};
