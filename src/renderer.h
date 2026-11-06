#pragma once
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include "camera.h"
#include "game_tables.h"
#include "sky.h"
#include "ecs.h"
#include "factions.h"
#include "game_log.h"
#include "mobs.h"
#include "model.h"
#include "terrain_mesh.h"
#include "ui_types.h"
#include "props.h"
#include "overview.h"
#include "text_cache.h"

class LoadStages;
class Terrain;
struct FloatingNumber;
struct TradeLabel;
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

    void uploadTerrain(const Terrain& t);

    void drawLoadingScreen(float progress, int winW, int winH, const std::string& label = std::string(),
                           const std::string& category = std::string(), bool backdrop = true);

    std::vector<Entity> unitsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);
    std::vector<Entity> propsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH);
    std::vector<Entity> groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                           int winW, int winH);
    Entity nearestGroundItem(const std::vector<Entity>& items, World& w, const Camera& cam, float mx, float my,
                             int winW, int winH);

    void drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                   const std::vector<HudEntry>& hud, int winW, int winH);

    void drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH);

    void setBuildSelected(const BuildPick& pick) { buildSelected_ = pick; }
    void setBuildInClaim(bool in) { buildInClaim_ = in; }
    void setBuildPreview(const BuildPreview& p) { buildPreview_ = p; }
    void drawBuildPreview(const Terrain& t);
    void drawBuildSection(const SidebarLayout& L, float mx, float my);

    void drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha, int winW, int winH);

    void drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units, bool running,
                       int repView = FACTION_PLAYER);
    void drawWorldMap(const WorldMapLayout& L, const std::vector<MapDot>& dots, float cx, float cz, float span,
                      const WorldMapMarks& marks);
    void drawDialogue(const DialogueLayout& L, const std::vector<SpokenLine>& convo,
                      const std::vector<DialogueOption>& options, float mx, float my);
    std::vector<std::string> wrapListText(const std::string& text, float maxW);
    int drawStatsTab(const SidebarLayout& L, const Stats& stats, float mx, float my, bool mob = false);
    void drawStatTip(const Stats& stats, StatId stat, float mx, float my);
    void drawInventorySidebar(const SidebarLayout& L, World& w, Entity unit, float mx, float my,
                              int dimSlot, int dimGear, int dragItem, bool editable);
    void drawBehaviorTip(const SidebarLayout& L, World& w, Entity unit, float mx, float my);
    void beginClip(const Rect& r);
    void endClip();
    void drawDraggedStack(float mx, float my, int item, int count);
    void drawItemCell(const Rect& r, int item, int count, float alpha = 1.0f);
    void drawSlotLabel(const Rect& r, const std::string& text, float inset, size_t maxLines, RGBA tint);
    void drawPauseBar(bool hint);
    float pauseBarHeight(bool hint);
    void drawTradeLabels(const std::vector<TradeLabel>& labels, World& w, const Camera& cam, int winW, int winH);
    void drawUiTextLarge(const std::string& s, float cx, float cy, float scale, RGBA tint);
    void drawLoot(const LootLayout& L, const std::string& name, const Equipment* worn, const Inventory* inv, float mx, float my);
    void drawStorage(const StorageLayout& L, const std::string& propName, const std::string& hint,
                     const Storage* storage, const Output* output, const std::string& status, int dragFrom,
                     float mx, float my, const std::string& toggleLabel = std::string());
    void drawEmotes(World& w, const Camera& cam, int winW, int winH, bool playerNames, bool otherNames);
    void drawPropEmotes(World& w, const Camera& cam, int winW, int winH);
    void drawHoverCue(World& w, const Camera& cam, int winW, int winH, Entity u, EmoteId emote, bool namesShown);
    void drawPropProgress(World& w, const Camera& cam, int winW, int winH);
    void drawTrade(const TradeLayout& L, const std::string& npcName, const std::string& yourName, const Inventory* npcInv,
                   const std::vector<TradeGhost>& theirs, const std::vector<TradeGhost>& yours, int dragFrom,
                   const TradeStatus& status, bool showValue, float mx, float my);
    void drawOptimizeAll(const OptimizeAllLayout& L, int unitCount, int attackShare, const std::array<bool, 3>& toggles,
                         float mx, float my);
    void drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                            float mx, float my);
    void drawDisband(World& w, const DisbandLayout& L, Entity unit, float held, float mx, float my);
    void drawNotice(const std::string& text, int winW, int winH);
    ItemMenuLayout itemMenuLayout(float x, float y, int buttonCount, int winW, int winH);
    void drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my);

    void drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH, float mx, float my);
    Entity groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH,
                             bool labels = true);
    void drawGroundItemName(World& w, const Camera& cam, int winW, int winH, Entity item);

    void drawUnitNames(World& w, const Camera& cam, int winW, int winH, bool players, bool others);
    void drawPropTooltip(const std::vector<std::string>& lines, float mx, float my);

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
    void drawUiTris(const std::vector<UiVert>& v);
    void drawRectGradientH(const Rect& r, RGBA left, RGBA right);
    void noteHover(const Rect& r);
    void setTitleFont(TTF_Font* font);
    void hoverFrameStart() { hoverAny_ = false; }
    bool hoverFrameEnd();
    void drawRect(const Rect& r, RGBA c);
    void drawRoundedRect(const Rect& r, float radius, RGBA c);
    void drawUiIcon(int icon, const Rect& r, RGBA tint = RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    void drawIconText(float x, float y, float h, int icon, const std::string& text, RGBA tint);
    void drawIconLabel(const Rect& r, int icon, const std::string& text, RGBA tint, bool uiFont);
    void drawTitleBar(const Rect& bar, int icon, const std::string& title);
    void drawCollapseButton(const Rect& r, bool collapsed);
    void drawCloseButton(const Rect& r);
    void drawReputationList(World& w, const UnitPanelLayout& L, int repView);
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

private:
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
    float unitTop(World& w, Entity u);
    float unitHover(World& w, Entity u) const;
    float bedLift(World& w, Entity u) const;
    struct UnitPose {
        bool lying = false;
        bool carried = false;
        float lift = 0.0f;
    };
    UnitPose unitPose(World& w, Entity e) const;

    void drawModel(const Model& m, float x, float y, float z, float targetSize, float yaw = 0.0f);

    void drawHud(const std::vector<HudEntry>& lines, int winW, int winH);

    const TextTex& unitGlyph(World& w, Entity e) const;

    void drawGroundItems(World& w, const Terrain& t, const Camera& cam);

    struct GroundStack {
        Rect box;
        std::vector<Entity> items;
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
    GLint uMVPColor_ = -1, uMVPGlyph_ = -1, uTint_ = -1, uSat_ = -1, uFaction_ = -1;

    GLuint dynVao_ = 0, dynVbo_ = 0;
    BuildPick buildSelected_;
    bool buildInClaim_ = false;
    BuildPreview buildPreview_;
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
    std::array<TextTex, UNIT_SPRITE_COUNT> profilePics_{};
    const TextTex& unitPortrait(World& w, Entity e) const;
    const TextTex& unitPicture(int n) const;
    std::array<Model, UNIT_SPRITE_COUNT> unitModels_{};
    std::array<const Model*, UNIT_SPRITE_COUNT> unitModelFor_{};
    std::array<Model, MOB_KIND_COUNT> mobModels_{};
    float unitHeight(World& w, Entity e) const;
    const Model* unitModelNumber(int n) const;
    const Model* unitModel(World& w, Entity e) const;
    float unitAspect(World& w, Entity e) const;
    Mat4 unitWorldMatrix(World& w, Entity e, const Model& m, float yaw) const;
    void drawUnitModel(const Model& m, const Mat4& placement);
    RGBA unitFactionTint(World& w, Entity e) const;
    struct UnitFacing {
        float x = 0.0f, z = 0.0f;
        float yaw = 0.0f;
        unsigned long long idleSince = 0;
        bool settled = false;
        float settleX = 0.0f, settleZ = 0.0f;
        unsigned long long turnedAt = 0;
    };
    std::map<Entity, UnitFacing> unitFacing_;
    float unitYaw(Entity e, float x, float z, bool idle);
    PictureCache itemPics_{ "data/item/item_", true };
    PictureCache emotePics_{ "data/emote/", false, emoteFile };
    const TextTex* emoteTexture(EmoteId emote);
    TextTex pink_;
    TextTex white_;
    struct PropHop {
        unsigned made = 0;
        float start = -1.0f;
    };
    std::map<Entity, PropHop> propHops_;
    void drawRing(float cx, float cy, float outer, float inner, float from, float to, RGBA color);
    std::array<Model, PROP_KIND_COUNT> propModels_{};
    const Model& propModel(PropKind kind) const;
    GLuint modelInstProgram_ = 0;
    GLint uVPInst_ = -1;
    GLuint instVbo_ = 0;
    std::array<GLuint, PROP_KIND_COUNT> propInstVao_{};
    std::array<std::vector<Mat4>, PROP_KIND_COUNT> propInstances_{};
    GLuint propInstVao(PropKind kind);
    void queuePropModel(PropKind kind, float x, float y, float z, float targetSize, float yaw);
    void drawPropInstances();
    float propTop(PropKind kind) const;
    float propBaseY(World& w, Entity e) const;

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
    float billX_ = 0.0f, billZ_ = 0.0f;
};
