#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "loading.h"
#include "language.h"
#include "build_info.h"
#include "camera.h"
#include "ui_layout.h"
#include "components.h"
#include "ecs.h"
#include "renderer.h"
#include "game_state.h"
#include "rng.h"
#include "audio.h"
#include "pak.h"
#include "fonts.h"
#include "savegame.h"
#include "settings.h"
#include "settings_menu.h"
#include "altar_ui.h"
#include "map_pick.h"
#include "squads.h"
#include "airship.h"
#include "wheelbarrow.h"
#include "display.h"
#include "inventory.h"
#include "party.h"
#include "party_ui.h"
#include "need_behaviors.h"
#include "build.h"
#include "claims.h"
#include "mobs.h"
#include "combat.h"
#include "equip.h"
#include "textcase.h"
#include "items.h"
#include "build_plan.h"
#include "building.h"
#include "systems.h"
#include "terrain.h"
#include "ui_types.h"
#include "useitem.h"
#include "world_format.h"
#include "play_state.h"
#include "storage.h"
#include "trade.h"
#include "selection.h"
#include "simulation.h"
#include "level.h"
#include "menus.h"
#include "app_support.h"
#include "editor_ui.h"
#include "level_file.h"
#include "world_gen.h"
#include "timefmt.h"

constexpr size_t HOVER_NAME_LIMIT = 10;
constexpr Uint64 DELETE_HOLD_MS = 3000;
constexpr size_t NAME_LIMIT = 32;
constexpr Uint64 UNSAVED_WARN_MS = 30000;

constexpr float TITLE_NAME_POINTS = 200.0f;

constexpr float WORK_SOUND_SECONDS = 1.6f;
constexpr float FORGE_SOUND_SECONDS = 3.0f;
constexpr float WORK_SOUND_RANGE = 30.0f;

// What lasts as long as one game: replaced whole whenever a level starts (enterLevel)
struct AppSession {
    Selection sel;
    int unitPanelRow = 0;
    float unitPanelWheel = 0.0f;
    bool runMode = false;
    int repView = FACTION_PLAYER;
    int repScroll = 0;
    bool behaviorAddOpen = false;
    ObjectId behaviorUiUnit = NO_OBJECT;
    struct PercentPrompt {
        bool open = false;
        ObjectId unit = NO_OBJECT;
        int row = -1;
        BehaviorKind kind = BehaviorKind::Eat;
        std::string text;
        std::string error;
    } percentPrompt;
    ObjectId lastLone = NO_OBJECT;
    float mapCx = 0.0f, mapCz = 0.0f, mapSpan = 0.0f;
    float moveMarkX = 0.0f, moveMarkZ = 0.0f, moveMarkTime = 0.0f;
    float mapDragX = 0.0f, mapDragY = 0.0f;
    bool mapDragging = false;
    float mapGrabX = 0.0f, mapGrabY = 0.0f;
    bool mapOpen = false;
    bool rightHold = false;
    bool formationDrag = false;   // Alt and the right mouse button: placing the selected units
    float dragFromX = 0.0f, dragFromZ = 0.0f, dragToX = 0.0f, dragToZ = 0.0f;
    bool optimizeAllOpen = false;
    bool optimizeSliderDrag = false;
    float holdX = 0.0f, holdZ = 0.0f;
    bool itemDrag = false;
    ObjectId dragUnit = NO_OBJECT;
    int dragSlot = -1;
    int dragGear = -1;
    struct ItemMenu {
        bool open = false;
        ObjectId unit = NO_OBJECT;
        int slot = -1;
        float x = 0.0f, y = 0.0f;
    };
    ItemMenu itemMenu;
    float sidebarScroll = 0.0f;
    ObjectId sidebarScrollUnit = NO_OBJECT;
    std::map<ObjectId, float> workSoundTimers;
    size_t lastSelectionKey = 0;
    std::optional<Uint64> lastSaveTicks;
    bool leftDown = false, middleDown = false;
    ObjectId followUnit = NO_OBJECT;
    GameLog gameLog;
    std::vector<FloatingNumber> floatingNumbers;
    std::vector<Puff> puffs;
    std::vector<TradeLabel> tradeLabels;
    int logOffset = 0;
    size_t logSeen = 0;
    std::vector<QueuedSound> soundsToPlay;
    float autoTimer = 0.0f;
    float pivotAlpha = 0.0f;
    int pivotTopFloor = 0;   // the highest finished floor under the camera's pivot (0: the ground)
    float actionAlpha = 0.0f;
    float actionX = 0.0f, actionZ = 0.0f;
    int actionStorey = 0;   // the floor the action crystal stands on
    float simLeft = 0.0f;
    std::vector<std::pair<ObjectId, Transform>> simBefore;
    std::vector<std::pair<ObjectId, Transform>> simTrue;
};

struct App : AppSession {
    int init();
    void run();
    void shutdown();
    void beginFrame();
    void handleEvent();
    void onTextInput();
    void onKeyDown();
    void onMouseWheel();
    void onMouseButtonDown();
    bool mouseDownOnPopups();
    bool pressOnDisband();
    bool pressOnOptimizeAll();
    bool pressOnWorldMap();
    bool pressOnTalk();
    void pressOnTrade(const TradeLayout& tl, float x, float y);
    bool pressOnContextMenu();
    bool pressOnStoreQuantity();
    bool pressOnLoot();
    bool pressOnStorage();
    bool pressOnPanelBars();
    bool pressOnFactionTabs();
    bool pressOnSelectMenu();
    bool pressOnFactionGrid();
    bool pressOnAltar();
    bool pressOnSquadScroll();
    bool pressToGrabPanel();
    bool mouseDownOnPanels();
    void onMouseButtonUp();
    void onMouseMotion();
    void update();
    void updateDeleteHold();
    void updateUnitPanelState();
    void updateRightHold();
    void updateTalkOrders();
    void stepGame();
    void stepSimulation();
    void updateCamera();
    void placeUnitsBetweenSteps();
    void putUnitsBack();
    void reportPropArrivals(const std::vector<PropArrival>& arrivals);
    void showWorkGains(const std::vector<WorkGain>& gains);
    void showEvents();
    void playWorkSounds();
    void autosaveTick();
    void closeStalePanels();
    void playQueuedSounds();
    void updateHover();
    void render();
    void drawScreenButtons(const std::vector<Button>& buttons);
    void drawMenuItems(Screen which, const std::vector<Button>& buttons);
    void dimScreen(float alpha);
    void drawMenuBackdrop();
    void drawTitleScreen();
    void drawSettingsScreen();
    void drawScenarioScreen();
    void drawPartyScreen();
    void drawPauseScreen();
    void drawSavePanel();
    void drawFrameOverlays();
    void drawPlayingScreen();
    void updateWorldMapView();
    void drawWorldOverlays();
    void drawGamePanels();
    ItemStack draggedStack();
    void drawStoragePanel();
    void updateDisbandHold();
    float disbandHeldShare();
    void drawWorldMapPopup();
    void drawTalkOrTrade();

    // World Generation and the level editor (app_worldgen.cpp)
    static constexpr int WORLD_GEN_PREVIEW_PX = 320;
    struct EditorEntry {
        std::string label;
        EditorTool tool = EditorTool::None;   // None: a heading, which can't be picked
        int value = 0;
    };
    void openWorldGen();
    void defaultWorldGenName();
    bool worldHasSaves(const std::string& name) const;
    int worldSaveCount(const std::string& name) const;
    std::vector<std::pair<std::string, RGBA>> mapDeleteLines();
    void cancelMapDelete();
    void deleteWorldMap();
    void refreshPlayedMaps();
    void stopWorldGenName();
    void worldGenChanged();
    void drawWorldGenScreen();
    void worldGenClick(float x, float y);
    void worldGenBarAt(int row, float x);
    MapTheme& themeBeingEdited();
    int& themePresetBeingEdited();
    void themeEdited();
    bool devThemeShown() const;
    void openDevTheme();
    void devThemeRevert();
    void devThemeMakePermanent();
    void drawThemePanel(const Rect& area);
    bool themeClick(const Rect& area, float x, float y);
    void themeDragAt(float x);
    bool generateWorld();
    void openLevelEditor(const std::string& mapName);
    void editorFocusCamera();
    void editorEdited();
    void editorRemember();
    void editorUndo();
    void stopEditorName();
    void setEditorTool(EditorTool tool, int picked);
    // The spawn list, shared by the level editor and the Build tab's Developer panel (app_spawn.cpp)
    std::vector<EditorEntry> spawnEntries(EditorTab tab);
    void drawSpawnPicker(SpawnPicker& p, const SpawnPickerLayout& L);
    bool spawnPickerClick(SpawnPicker& p, const SpawnPickerLayout& L, float x, float y);   // true when a row was picked
    // A key while a count is being typed into the spawn list: true if it took the key
    bool spawnPickerKey(SpawnPicker& p);
    void spawnPickerCommit(SpawnPicker& p, bool keep);
    void spawnPickerWheel(SpawnPicker& p, const SpawnPickerLayout& L, float x, float y, float notches);
    int gatherUnits();   // the Developer panel: every unit of yours to the ground floor around the camera's pivot; how many
    int spawnAt(SpawnPicker& p, float x, float z, bool levelIds, int storey = 0);   // places the pick (a cluster of them); how many
    void openDeveloper();
    void closeDeveloper();
    bool pressOnDeveloper();
    void drawDeveloper();
    void drawEditorPanel();
    bool editorMouseDown();
    void editorPlaceAt(float mx, float my);
    void editorEraseAt(float mx, float my);
    bool editorSave();
    bool editorLoad();
    bool editorDirty();
    void editorExit();
    bool editorKeyDown();

    WorldGenUi worldGenUi;
    ThemeUi themeUi;
    bool editing = false;
    LevelEditorUi editor;
    std::string pendingLevel;

    bool partyAction(Action action, int param);
    void partyRandomUnit();
    void partyDropPick(int param);
    bool savePanelAction(Action action, int param);
    void finishSave(bool ok);
    void clearQuestion();
    void enterLevel();
    void refreshSaves();
    void showNotice(const std::string& text, float seconds = 2.5f);
    void refuse(const std::string& text);
    void closePanel();
    // levels: the level editor's levels for this map, in place of saved games
    void openPanel(bool saveMode, bool levels = false);
    int pickedIndex() const;
    bool panelCanConfirm() const;
    void pickSave(int index);
    void loadPicked();
    void writePreview(const std::string& savePath);
    void drawPanelSide(const PanelGeometry& g);
    bool wouldLoseProgress();
    enum class LeaveTo { Title, Desktop };
    void requestLeave(LeaveTo to);
    void leave(LeaveTo to);
    void startMapLevel(const MapChoice& choice);
    void openParty(const MapChoice& choice);
    void openPartyMenu();
    void stopNameEdit();
    void closeDropdown();
    bool writeSave(const std::string& path, const std::string& name, bool autosave);
    void loadSave(std::string path);
    bool autosaveNow();
    void performDelete();
    void panWithKeys(float dt, bool edgeScroll);
    void applyAudio();
    void applyVideo();
    void saveSettingsFile();
    GearPrefs gearPrefs();
    void applyFontScale();
    void applyLanguage();
    void settingsCommit(const SettingsChange& c, bool save);
    void leaveSettings();
    void resetPanels();
    void settingsClick(float x, float y, int W, int H);
    void performAction(Action action, int param);
    UnitPanelLayout squadPanelLayout(int W, int H);
    std::vector<ObjectId> ownUnitList();   // the units of the squad the Squad tab shows
    void pickSquad(int squad, bool additive);
    void optimizeSquadGear();
    void optimizeSelectedGear();
    // The Optimize, Floor and Form buttons' dropdowns: 0 none open, 1 Optimize, 2 Floor, 3 Form
    static constexpr int SQUAD_MENU_FORM = 3;
    int squadMenu = 0;
    int formMenuScroll = 0;
    ItemMenuLayout squadMenuLayout();
    std::vector<std::string> squadMenuLabels();
    void runSquadMenu(int choice);
    // Floors: the one looked at follows the camera (Auto) or Q and E (Manual), Z back to Auto; the cutaway inside a
    // building; clicks that land on the highest floor seen under the pointer
    void updateFloorView();
    // What the cutaway was last worked out for: it is made again only when one of these changes
    struct CutawayKey {
        BuildingStamp stamp;
        int x = 0, z = 0, level = 0;
        bool inside = false;
        bool operator==(const CutawayKey& o) const { return stamp == o.stamp && x == o.x && z == o.z && level == o.level && inside == o.inside; }
    };
    CutawayKey cutawayKey;
    bool cutawayKeyValid = false;
    // Kept between frames so the per-frame build preview and hover work isn't made again when nothing it depends on changed
    struct SpotCache {
        bool filled = false, ok = false;
        PropKind kind = PropKind::Wagon;
        float x = 0.0f, z = 0.0f;
        int storey = 0;
        std::uint64_t props = 0, owners = 0, foundations = 0, storeys = 0, blueprints = 0;
        Uint64 tick = 0;
    };
    SpotCache spotCache;
    bool buildSpotOk(PropKind kind, float x, float z, int storey, const std::vector<Claim>& claims);
    std::vector<std::array<float, 3>> campRings;   // the campfires whose rings show while a Bedroll is placed
    bool campRingsValid = false;
    std::uint64_t campRingsProps = 0, campRingsOwners = 0, campRingsBlueprints = 0;
    Uint64 campRingsTick = 0;
    std::vector<BuildPreview::Mark> bpBoxMarks;    // the Blueprint editor's box edge
    int bpBoxW = 0, bpBoxD = 0;
    struct HoverCache {
        bool filled = false, hasProps = false, hasUnits = false, hasItems = false;
        float x = 0.0f, y = 0.0f;
        Screen screen = Screen::Playing;
        int winW = 0, winH = 0;
        float focusX = 0.0f, focusY = 0.0f, focusZ = 0.0f, yaw = 0.0f, pitch = 0.0f, distance = 0.0f, lift = 0.0f;
        std::uint64_t revision = 0;
        Uint64 tick = 0;
        std::vector<ObjectId> props, units, items;
    };
    HoverCache hover;
    void stepFloor(int by);
    void floorAuto();
    void drawFloorBanner();
    std::optional<std::pair<float, float>> pickFloor(float mx, float my, int& storey);
    std::optional<std::pair<float, float>> pickAbove(float mx, float my, std::optional<std::pair<float, float>> ground, float above);
    int clickStorey = 0;   // the floor the last order's click landed on
    long long holdPlanned = -1;   // a held right click: the tile and floor the units going by ladders were last sent to
    void pickClosestUnit();
    void runSelectMenu(int choice);
    ItemMenuLayout selectMenuLayout();
    void startSquadRename();
    void stopSquadRename(bool keep);
    bool squadKeyDown();
    void noteSquadOfSelection();
    void openAltar();
    std::vector<ObjectId> flyAirshipsTo(const std::vector<ObjectId>& units, float x, float z);
    // The Build tab's foundations and pieces (app_building.cpp)
    bool buildPressAt(float gx, float gz);
    bool buildRelease();
    void updateBuildPaint();
    bool previewBuilding(BuildPreview& bp, float gx, float gz);
    // Picking what a foundation, wall or door is made of
    bool pressOnMaterialAsk();
    void drawMaterialAsk();
    bool pressOnCancelAsk();
    void drawCancelAsk();
    float cancelAskTextWidth();
    int cancelBlueprints(const std::vector<ObjectId>& list);
    void finishAreaTool();
    void drawFoundationSize();
    void startRename(ObjectId u);
    void openStoreFilter(ObjectId prop);
    bool ownDoor(ObjectId target);
    // A Stall's Stock window: your stores down the left, the one picked's items on the right to order hauled to the Stall
    void openStall(ObjectId stall);
    bool pressOnStall();
    void drawStall();
    void scrollStall(float mx, float my, float wheel);
    std::optional<std::pair<float, float>> buildPickAt(float mx, float my);
    void markFloorTiles(BuildPreview& bp, int tx, int tz);
    bool pressOnStoreFilter();
    void drawStoreFilter();
    void openBehaviorGroups(ObjectId altarProp);
    std::vector<BehaviorOption> groupTaskOptions();
    bool pressOnBehaviorGroups();
    void scrollBehaviorGroups(float mx, float my, float wheel);
    void drawBehaviorGroups();
    // The Schedule tab: a click or a drag over its hours paints them; a press on an Override switch flips it. True if it took the press.
    bool scheduleClick(float x, float y, bool press);
    void openGhosts(ObjectId altarProp);
    bool pressOnGhosts();
    void drawGhosts();
    std::vector<ItemStack> ghostBones();
    std::vector<size_t> ghostOrder();
    void finishRename(bool keep);
    bool pressOnRename();
    void drawRename();
    void openMapPick();
    void drawDefaultWorldAsk();
    void makeDefaultWorld();
    void pickMap(int i);
    void pickLevel(int i);
    void updateMapPick();
    bool mapPickMouseDown();
    void mapPickWheel(float x, float y, float notches);
    void drawMapPickScreen();
    MapPickLayout mapPickLayout();
    std::array<std::vector<ObjectId>, 2> altarColumns();
    void applyAltarMoves();
    void drawAltar();
    // Manage Squads (app_altar.cpp)
    void openManageSquads();
    int squadsTarget(ObjectId u);
    std::array<std::vector<ObjectId>, 2> squadsColumns();
    int squadsCount(int squad);
    bool pressOnManageSquads();
    void scrollManageSquads(float x, float y, float notches);
    void applySquadMoves();
    void drawManageSquads();
    // The Build tab's Adjust tool (app_prop_adjust.cpp)
    struct AdjustView;
    AdjustView adjustView();
    int adjustHandleAt(const AdjustView& v, float mx, float my);
    bool adjustPress(float mx, float my);
    // A press on the widget's buttons or handles, which are drawn over the panels and so come before them
    bool pressOnAdjustWidget();
    void adjustMotion(float mx, float my);
    bool adjustRelease();
    void endPropAdjust(bool keep);
    void drawPropAdjust();
    // Research and Crafting Order (app_crafting.cpp)
    std::vector<int> craftRows();
    void openCraftWindow(ObjectId prop, bool research);
    bool pressOnCraftWindow();
    void scrollCraftWindow(float notches);
    void drawCraftWindow();
    // Faction Rules: your faction's rules for outsiders (app_faction_rules.cpp)
    void openFactionRules();
    bool pressOnFactionRules();
    void drawFactionRules();
    // When the selection changes and the Squad tab shows an emptied squad: the squad of the selected units instead
    void followSelectionSquad();
    bool sidebarUp();
    std::vector<BehaviorOption> behaviorOptionsFor(ObjectId u);
    SidebarLayout sidebarFor(int W, int H, float scroll);
    bool shownIsYours();
    void sortInventory(ObjectId unit);
    ObjectId corpseCarrier(ObjectId prop);
    void behaviorClick(ObjectId unit, const BehaviorHit& hit);
    void answerPercentPrompt(bool keep);
    bool behaviorExtraClick(ObjectId unit, const SidebarLayout& sl, float x, float y);
    bool unitPanelHovered(int W, int H);
    std::vector<std::string> menuLabels();
    void useSlot(ObjectId unit, int slot);
    GameLogLayout gameLogPanel(int W, int H);
    DialogueLayout dialogueBox(int W, int H);
    bool onTalkOrTrade(int W, int H, float x, float y);
    bool infoPanelClick(int W, int H, float tx, float ty);
    PanelPlace& placeOf(PanelId which);
    bool panelFolded(PanelId which);
    Rect shownPanel(PanelId which, int W, int H);
    Rect panelBarOf(PanelId which, int W, int H);
    std::pair<std::optional<PanelId>, int> grabAt(int W, int H, float x, float y);
    void setGrabCursor(int kind);
    void applyGrab(int W, int H, float x, float y);
    void endGrab(int W, int H);
    void setFolded(PanelId which, bool fold, int W, int H);
    void toggleLock(PanelId which);
    Rect lockRectOf(PanelId which, int W, int H);
    void drawPanelLock(PanelId which);
    bool gameLogHovered(int W, int H);
    WorldMapLayout worldMapFor(int W, int H);
    bool mapGroundAt(int W, int H, float x, float y, float& gx, float& gz);
    void markMove(float x, float z);
    FormationChoice formationChoice() const;
    bool formationKeyDown();
    void applyFormationKey(int index);   // 0..3 a shape, 4 tight or loose: what Alt and 1..5 do
    std::vector<Renderer::FormationHelpEntry> formationEntries();
    Renderer::FormationMenuLayout formMenuLayout();
    bool pressOnFormMenu();
    bool wheelOnFormMenu();
    void drawFormMenu();
    void finishFormationDrag();
    void beginRightHold(float x, float z);
    void moveSelectedTo(float x, float z);
    bool overPanels(float x, float y, int W, int H);
    bool mouseOverPanels();
    bool holdTarget(int W, int H, float x, float y, float& gx, float& gz);
    bool pointerOverPanels(int W, int H);
    void openTrade(bool free);
    void sayAs(ObjectId who, const std::string& text, bool player);
    void endTalk();
    void pauseForConversation();
    void endBrokenConversation();
    void requestTrade(bool npcSide, ObjectId owner, int slot);
    void confirmQuantity();
    void quantityClick(float qx, float qy);
    void requestStore(int slot);
    void showTraderInventory();
    void startTalk(ObjectId npc, ObjectId talker, bool shop);
    void tryStartTalk(ObjectId npc, ObjectId talker, std::vector<ObjectId> units, bool shop = false);
    void startFreeTrade(ObjectId other, ObjectId unit);
    bool noteLockedIn(const std::vector<ObjectId>& units);
    ObjectId menuTargetAt(float mx, float my);
    std::vector<ObjectId> menuActors(ObjectId target);
    bool talkable(ObjectId u);
    bool sells(ObjectId u);
    ObjectId putDownCarrier(ObjectId target);
    std::vector<play::MenuAction> menuActionsFor(ObjectId target);
    std::vector<std::string> contextLabels();
    ItemMenuLayout contextMenuLayout();
    bool openContextMenuOn(ObjectId target, float mx, float my, bool squadPanel = false);
    bool openContextMenu(float mx, float my);
    void talkOrWalkUp(ObjectId target, const std::vector<ObjectId>& actors, bool shop);
    void pickMenuAction(play::MenuAction a);
    void chooseOption(size_t i);

    GameSettings settings;
    UiFonts fonts;
    UiFonts menuFonts;
    int appliedMenuPercent = 0;
    int appliedFontScale = 0;
    std::string appliedFontPath;
    std::string appliedLanguage;
    // One window, made once. Destroying an OpenGL window and making another left the new one black on Windows.
    SDL_Window* window = nullptr;
    SDL_GLContext ctx = nullptr;
    DisplayInfo displayInfo;

    Renderer renderer;
    std::vector<TTF_Font*> nameFontOwned;

    Terrain terrain;
    World world;
    Rng rng;
    GameState gs;
    Camera cam;
    MapChoice currentMap;
    bool combatCollapsed = false, squadCollapsed = false;
    int factionTab = FACTION_TAB_SQUAD;
    bool selectMenuOpen = false;
    bool squadRenaming = false;
    std::string squadRenameText;
    int lastSquadTap = -1;
    unsigned long long lastSquadTapAt = 0;
    AltarUi altar;
    SquadsUi squadsUi;
    bool factionRulesOpen = false;
    // The Research or Crafting Order window, and the prop it is for
    struct CraftUi {
        bool open = false;
        ObjectId prop = NO_OBJECT;
        bool research = false;
        int scroll = 0;
    };
    CraftUi craftUi;
    // The prop the Adjust tool has hold of, how it was before, and the handle being dragged
    struct PropAdjustUi {
        enum class Mode { Move = 0, Rotate, Scale };
        ObjectId prop = NO_OBJECT;
        PropVisual before;
        Mode mode = Mode::Move;
        int drag = -1;   // 0 to 2: the x, y or z arm or ring. 3: the middle. -1: none.
        float lastX = 0.0f, lastY = 0.0f;
    };
    PropAdjustUi propAdjust;
    // The Building Blueprints editor (app_blueprint.cpp)
    BlueprintUi bpEdit;
    void openBlueprintEditor();
    void blueprintExit();
    bool blueprintSave(bool overwriteConfirmed = false);
    void blueprintDelete(const std::string name);
    void refreshPlanList();
    void blueprintToggleNpc(size_t index);
    // The Build tab's Building Blueprint popout and the plan taken up from it to be laid in your claim (app_plan_popup.cpp)
    struct PlanPopup {
        bool open = false;
        int selected = 0, scroll = 0;
        int material = 0;   // a BuildMaterial
    };
    PlanPopup planPopup;
    struct PlanPick {
        bool active = false;
        BuildPlan plan;
        int turns = 0;
        int material = 0;
        BuildPlan turned;                // the plan turned that many times, with its footprint and foundation rectangles
        std::vector<bool> footprint;
        std::vector<PlanRect> rects;
        int turnedFor = -1;
    };
    PlanPick planPick;
    struct PlanSpotCache {
        bool filled = false, ok = false;
        int x = 0, z = 0, turns = 0;
        std::uint64_t props = 0, foundations = 0;
        Uint64 tick = 0;
    };
    PlanSpotCache planSpot;
    void togglePlanPopup();
    void closePlanPopup();
    bool pressOnPlanPopup();
    void scrollPlanPopup(float wheel);
    bool planPopupKey();
    void drawPlanPopup();
    void startPlanPick();
    void cancelPlanPick();
    bool planPickOrigin(float mx, float my, int& x, int& z);
    bool planSpotFree(const BuildPlan& turned, int x, int z, const std::vector<Claim>& claims, bool cached);
    void previewPlanPick(BuildPreview& bp, const std::vector<Claim>& claims);
    bool placePlanPick(float mx, float my);
    bool blueprintLoad(const std::string& name);
    void blueprintClear();
    bool blueprintKeyDown();
    bool pressOnBlueprint();
    void drawBlueprintPanel();
    void updateBlueprintEditor();
    std::string blueprintText();
    BuildPlan blueprintPlan();
    KillUi devKill;     // the Developer panel's Kill Entity tab
    SweepUi devClean;   // and its Clean tab
    DestroyUi devDestroy;   // and its Destroy tab
    SweepUi& sweepUi();
    void sweepClick(const DeveloperLayout& L, float x, float y);
    bool killEntity(ObjectId u);
    bool cleanItem(ObjectId g);
    int killAt(float mx, float my);
    int cleanAt(float mx, float my);
    int killAll();
    int cleanAll();
    bool destroyProp(ObjectId e);
    int destroyAt(float mx, float my);
    int destroyAll();
    void updateSweepHold();
    void drawSweepTab(const DeveloperLayout& L);
    SpawnPicker devSpawn;   // the Build tab's Developer panel (its Theme tab edits the world's theme, temporarily)
    // The Developer panel's theme: what the world has saved, and what is shown now. Changes are temporary until made permanent.
    bool devThemeLoaded = false;
    bool devThemeConfirm = false;   // Make permanent was pressed once: a second press changes the world's theme
    MapTheme devSavedTheme;
    int devSavedPreset = 0;
    MapTheme devTheme;
    int devThemePreset = 0;
    std::vector<ObjectId> lastSelection;   // the selection as followSelectionSquad last saw it
    BehaviorGroupsUi groupsUi;
    StoreFilterUi storeFilterUi;
    StallUi stallUi;
    RenamePrompt rename;
    GhostsUi ghostsUi;
    MapPickUi mapPick;
    std::unique_ptr<OverviewBuilder> mapPreview;
    std::unique_ptr<LayoutPlanner> layoutPlanner;
    bool unitOpen = true;
    UnitTab unitTab = UnitTab::Inventory;
    PanelPlace squadPlace, unitPlace, logPlace;
    bool panelLocked[PANEL_COUNT] = { true, true, true };
    std::optional<PanelId> grabWhich;
    int grabKind = 0;
    bool squadThumbDrag = false;
    float squadThumbGrab = 0.0f;
    float grabMouseX = 0.0f, grabMouseY = 0.0f;
    float grabX = 0.0f, grabY = 0.0f, grabW = 0.0f, grabH = 0.0f;

    Screen screen = Screen::Title;
    int menuSel = -1;
    Screen menuScreen = Screen::Playing;
    unsigned long long menuShownAt = 0;
    float menuMouseX = -1.0f, menuMouseY = -1.0f;
    Screen backScreen = Screen::Title;
    SettingsMenuState settingsUi;

    Party party{};
    PartyUi partyUi;
    ScenarioId scenarioPick = ScenarioId::Outpost;
    int scenarioScroll = 0;
    MapChoice pendingMap;
    Rng partyRng;

    bool gameActive = false;

    bool panelOpen = false;
    bool panelSaveMode = false;
    Screen panelFrom = Screen::Title;
    LeaveTo leaveTo = LeaveTo::Title;
    Ask ask = Ask::None;
    std::optional<SaveSummary> pendingOverwrite;
    std::optional<SaveSummary> pendingLoad;
    std::optional<SaveSummary> pendingDelete;
    bool panelLevels = false;
    std::string pickedPath;   // the save or level picked in the list
    std::optional<LevelData> pickedLevel;   // the picked level, read for its details
    bool deleteHolding = false;
    Uint64 deleteHoldStart = 0;
    Rect deleteHoldRect;
    std::string saveName;
    size_t listOffset = 0;
    int viewH = 720;

    SortKey sortKey = SortKey::Time;
    bool sortAsc = false;

    std::vector<SaveSummary> saves;

    bool running = true;
    bool screenshotPending = false;
    float pressX = 0.0f, pressY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;
    std::string lastTitle;

    int infoTab = 1;
    LogFilter logFilter;
    bool logFilterOpen = false;

    std::string notice;
    float noticeSec = 0.0f;

    Uint64 lastTicks = 0;
    static constexpr float SIM_STEP_SECONDS = 1.0f / 60.0f;
    static constexpr int SIM_MOST_STEPS_PER_FRAME = 5;
    Uint64 fpsSince = 0;
    int fpsFrames = 0;
    std::string fpsText = "-- FPS";
    // A measuring run (see app_bench.cpp)
    struct Bench {
        bool on = false, measuring = false, quit = false;
        int step = 0, frames = 0;
        Uint64 since = 0, measureSince = 0;
        bool clouds = true, shadows = true, varied = true;
        int renderScale = 100;
        Camera saved;
        double ground = 0.0, water = 0.0, cloudMs = 0.0;
    };
    Bench bench;
    void benchStart();
    void benchFrame();
    // The debug log (see app_debug_log.cpp): what has been written so far, and what the next overview line sums up
    struct DebugLogState {
        bool on = false;
        std::string level;
        Uint64 since = 0, lastFrame = 0;
        int frames = 0;
        double worstMs = 0.0;
    };
    DebugLogState debugLogState;
    void debugLogFrame();
    void debugLogSession();
    void debugLogBuildTest();
    SDL_Event e{};

    std::chrono::steady_clock::time_point frameStart;
    int winW = 0, winH = 0;
    float dt = 0.0f;
    std::vector<HudEntry> hud;
    float mouseX = 0.0f, mouseY = 0.0f;
    std::vector<ObjectId> unitsUnderMouse, itemsUnderMouse;
    static constexpr unsigned long long PROP_TOOLTIP_DELAY_MS = 1000;
    ObjectId hoverProp = NO_OBJECT;   // the nearest prop under the mouse, while nothing covers it
    unsigned long long hoverPropSince = 0;
    std::optional<bool> mouseOnPanels;
};
