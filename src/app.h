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
#include "map_file.h"
#include "audio.h"
#include "pak.h"
#include "fonts.h"
#include "savegame.h"
#include "settings.h"
#include "settings_menu.h"
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
#include "systems.h"
#include "terrain.h"
#include "ui_types.h"
#include "useitem.h"
#include "world_format.h"
#include "play_state.h"
#include "storage.h"
#include "trade.h"
#include "selection.h"
#include "level.h"
#include "menus.h"
#include "app_support.h"
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
    Entity behaviorUiUnit = NULL_ENTITY;
    struct PercentPrompt {
        bool open = false;
        Entity unit = NULL_ENTITY;
        int row = -1;
        BehaviorKind kind = BehaviorKind::Eat;
        std::string text;
        std::string error;
    } percentPrompt;
    Entity lastLone = NULL_ENTITY;
    float mapCx = 0.0f, mapCz = 0.0f, mapSpan = 0.0f;
    float moveMarkX = 0.0f, moveMarkZ = 0.0f, moveMarkTime = 0.0f;
    float mapDragX = 0.0f, mapDragY = 0.0f;
    bool mapDragging = false;
    float mapGrabX = 0.0f, mapGrabY = 0.0f;
    bool mapOpen = false;
    bool rightHold = false;
    bool optimizeAllOpen = false;
    bool optimizeSliderDrag = false;
    float holdX = 0.0f, holdZ = 0.0f;
    bool itemDrag = false;
    Entity dragUnit = NULL_ENTITY;
    int dragSlot = -1;
    int dragGear = -1;
    struct ItemMenu {
        bool open = false;
        Entity unit = NULL_ENTITY;
        int slot = -1;
        float x = 0.0f, y = 0.0f;
    };
    ItemMenu itemMenu;
    float sidebarScroll = 0.0f;
    Entity sidebarScrollUnit = NULL_ENTITY;
    std::map<Entity, float> workSoundTimers;
    size_t lastSelectionKey = 0;
    std::optional<Uint64> lastSaveTicks;
    bool leftDown = false, middleDown = false;
    Entity followUnit = NULL_ENTITY;
    GameLog gameLog;
    std::vector<FloatingNumber> floatingNumbers;
    std::vector<TradeLabel> tradeLabels;
    int logOffset = 0;
    size_t logSeen = 0;
    float autoTimer = 0.0f;
    float pivotAlpha = 0.0f;
    float actionAlpha = 0.0f;
    float actionX = 0.0f, actionZ = 0.0f;
    float simLeft = 0.0f;
    std::vector<std::pair<Entity, Transform>> simBefore;
    std::vector<std::pair<Entity, Transform>> simTrue;
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
    void reportPropArrivals();
    void showWorkGains(double gameSecs);
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
    void openPanel(bool saveMode);
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
    std::vector<Entity> ownUnitList();
    bool sidebarUp();
    std::vector<BehaviorOption> behaviorOptionsFor(Entity u);
    SidebarLayout sidebarFor(int W, int H, float scroll);
    bool shownIsYours();
    void sortInventory(Entity unit);
    Entity corpseCarrier(Entity prop);
    void behaviorClick(Entity unit, const BehaviorHit& hit);
    void answerPercentPrompt(bool keep);
    bool behaviorExtraClick(Entity unit, const SidebarLayout& sl, float x, float y);
    bool unitPanelHovered(int W, int H);
    std::vector<std::string> menuLabels();
    void useSlot(Entity unit, int slot);
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
    void beginRightHold(float x, float z);
    void moveSelectedTo(float x, float z);
    bool overPanels(float x, float y, int W, int H);
    bool holdTarget(int W, int H, float x, float y, float& gx, float& gz);
    bool pointerOverPanels(int W, int H);
    void openTrade(bool free);
    void sayAs(Entity who, const std::string& text, bool player);
    void endTalk();
    void pauseForConversation();
    void endBrokenConversation();
    void requestTrade(bool npcSide, Entity owner, int slot);
    void confirmQuantity();
    void quantityClick(float qx, float qy);
    void requestStore(int slot);
    void showTraderInventory();
    void startTalk(Entity npc, Entity talker, bool shop);
    void tryStartTalk(Entity npc, Entity talker, std::vector<Entity> units, bool shop = false);
    void startFreeTrade(Entity other, Entity unit);
    bool noteLockedIn(const std::vector<Entity>& units);
    Entity menuTargetAt(float mx, float my);
    std::vector<Entity> menuActors(Entity target);
    bool talkable(Entity u);
    bool sells(Entity u);
    Entity putDownCarrier(Entity target);
    std::vector<play::MenuAction> menuActionsFor(Entity target);
    std::vector<std::string> contextLabels();
    ItemMenuLayout contextMenuLayout();
    bool openContextMenuOn(Entity target, float mx, float my, bool squadPanel = false);
    bool openContextMenu(float mx, float my);
    void talkOrWalkUp(Entity target, const std::vector<Entity>& actors, bool shop);
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
    ScenarioId scenarioPick = ScenarioId::Vagabond;
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
    SDL_Event e{};

    std::chrono::steady_clock::time_point frameStart;
    int winW = 0, winH = 0;
    float dt = 0.0f;
    std::vector<HudEntry> hud;
    float mouseX = 0.0f, mouseY = 0.0f;
};
