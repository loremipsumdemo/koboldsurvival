#pragma once
// The running game: everything main() used to hold as locals, and what it did each frame, as one object.
// app.cpp starts it, runs the frame loop and shuts it down. The rest is split by job:
//   app_actions.cpp  saving and loading, the settings, and the menus' buttons (performAction)
//   app_panels.cpp   the Unit, Squad and Information panels: layout, grabbing, folding, locks, the world map
//   app_talk.cpp     talking and trading, the how-many prompt, and the right-click menu
//   app_input.cpp    one SDL event (handleEvent)
//   app_update.cpp   the game's step each frame, and the hover list and build preview
//   app_render.cpp   drawing the frame
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
#include "sky.h"
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

constexpr size_t HOVER_NAME_LIMIT = 10;      // the hover list shows up to this many names
constexpr Uint64 DELETE_HOLD_MS = 3000;      // holding Delete this long deletes the save
constexpr size_t NAME_LIMIT = 32;            // longest save name, in bytes
constexpr Uint64 UNSAVED_WARN_MS = 30000;    // loading over a game not saved this recently asks first
constexpr const char* SETTINGS_PATH = "settings.cfg";

// The size the title screen's name is drawn in before it is shrunk to fit: large enough that it is never enlarged,
// even on a 4K screen
constexpr float TITLE_NAME_POINTS = 200.0f;

// Working sounds: how often each plays while a prop is worked (real seconds), and how far from the camera's focus a
// worked prop is still heard, in tiles
constexpr float WORK_SOUND_SECONDS = 1.6f;
constexpr float FORGE_SOUND_SECONDS = 3.0f;
constexpr float WORK_SOUND_RANGE = 30.0f;

struct App {
    // ---- Start-up, the frame loop and shutdown (app.cpp) ----
    // Opens the window, the fonts and the renderer. 0 when the game can run, otherwise the exit code.
    int init();
    void run();
    void shutdown();
    void beginFrame();                       // the music, the window size and the menu fonts, at the top of each frame
    void handleEvent();                      // the event in e (app_input.cpp)
    void update();                           // timing and the game's step (app_update.cpp)
    void updateHover();                      // the window title, the hover list and the build preview (app_update.cpp)
    void render();                           // the frame (app_render.cpp)

    // ---- Methods made from main()'s lambdas ----
// app_actions.cpp
    void clearCombatText();
    void refreshSaves();
    void refreshMaps();
    void showNotice(const std::string& text, float seconds = 2.5f);
    void refuse(const std::string& text);
    void closePanel();
    void openPanel(bool saveMode);
    bool wouldLoseProgress();
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
    // app_panels.cpp
    std::vector<Entity> ownUnitList();
    bool sidebarUp();
    std::vector<BehaviorOption> behaviorOptionsFor(Entity u);
    SidebarLayout sidebarFor(int W, int H, float scroll);
    bool shownIsYours();
    void sortInventory(Entity unit);
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
    // app_talk.cpp
    void sayAs(Entity who, const std::string& text, bool player);
    void endTalk();
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
    void pickMenuAction(play::MenuAction a);
    void chooseOption(size_t i);

    // ---- State ----
    // Settings come first: the saved text size decides the fonts
    GameSettings settings;
    UiFonts fonts;
    UiFonts menuFonts;            // opened in the loop, once the window's height is known (menuFontPercent)
    int appliedMenuPercent = 0;   // the size they were opened at; 0 to open them again
    int appliedFontScale = 0;
    std::string appliedFontPath;
    std::string appliedLanguage;
    // The large title font, for the selected language, with the same fallbacks. It keeps the language it started with.
    std::vector<TTF_Font*> titleFontOwned;
    TTF_Font* font = nullptr;
    // One window, made once. Destroying an OpenGL window and making another left the new one black on Windows.
    SDL_Window* window = nullptr;
    SDL_GLContext ctx = nullptr;
    DisplayInfo displayInfo;

    Renderer renderer;
    // The game's name on the title screen: a very large font, drawn shrunk so it stays sharp
    std::vector<TTF_Font*> nameFontOwned;

    Terrain terrain;
    World world;
    Rng rng;
    GameState gs;
    Selection sel;
    Camera cam;
    unsigned levelSeed = 0;
    std::vector<MapChoice> maps;          // premade and custom maps, for New Game
    MapChoice currentMap;                 // the map the game in progress is played on
    size_t mapOffset = 0;                 // first map shown in the New Game list
    bool mapSortByName = false;           // New Game list sorted by name, else by date saved
    bool mapSortAsc = false;              // ascending order; the list opens newest first
    SkyTuning sky;                        // atmosphere settings
    int unitPanelRow = 0;                 // first visible row of the bottom unit panel
    float unitPanelWheel = 0.0f;          // wheel movement over the squad panel not yet turned into a row of scroll
    // Walk or run for the squad. Run moves faster, and each unit that moves at run pace in an in-game hour pays
    // double hunger at the next hour. Not saved: a game starts walking.
    bool runMode = false;
    // The title bars' [-] buttons. Every panel starts open.
    bool combatCollapsed = false, squadCollapsed = false;
    // The Faction Panel: its tab (Squad or Reputation), the faction the Reputation tab shows, and how far its list is
    // scrolled. It shows the Player Faction's reputations until another faction's name is clicked.
    int factionTab = FACTION_TAB_SQUAD;
    int repView = FACTION_PLAYER;
    int repScroll = 0;
    bool unitOpen = true;           // the Unit Panel: open shows its tabs, closed shows its bar alone
    UnitTab unitTab = UnitTab::Inventory;   // the tab shown: Inventory, Equipment, Profile, Stats or Behavior
    // The Behavior tab: the Add Behavior list, which shuts when the panel shows another unit, and the # prompt asking
    // for one behavior's percent. The prompt is modal and asked once: it takes every click and key until it is answered.
    bool behaviorAddOpen = false;
    Entity behaviorUiUnit = NULL_ENTITY;
    struct PercentPrompt {
        bool open = false;
        Entity unit = NULL_ENTITY;
        int row = -1;                       // the behavior's row when it was asked, checked again when it is answered
        BehaviorKind kind = BehaviorKind::Eat;
        std::string text;                   // the digits typed
        std::string error;                  // why the last answer was refused
    } percentPrompt;
    Entity lastLone = NULL_ENTITY;  // the lone unit the Unit Panel showed last frame
    // Where the Squad Panel, the Unit Panel and the Information Panel are, as last locked (kept in the settings). 0 is
    // the default size and place.
    PanelPlace squadPlace, unitPlace, logPlace;
    // Each panel's lock: locked, it cannot be moved or resized. They start locked; locking one again keeps its place in
    // the settings.
    bool panelLocked[PANEL_COUNT] = { true, true, true };
    // The panel being dragged (a PanelId) and what the drag does: a mix of the GRAB_ edges, or GRAB_MOVE for its title
    // bar. The mouse, and the panel's open rectangle, where the drag began.
    std::optional<PanelId> grabWhich;
    int grabKind = 0;
    // The Squad Panel's scroll bar thumb while it is held, and how far down the thumb the mouse took hold
    bool squadThumbDrag = false;
    float squadThumbGrab = 0.0f;
    float grabMouseX = 0.0f, grabMouseY = 0.0f;
    float grabX = 0.0f, grabY = 0.0f, grabW = 0.0f, grabH = 0.0f;
    float mapCx = 0.0f, mapCz = 0.0f, mapSpan = 0.0f;   // the world map's view: its centre and height in tiles, 0 while closed
    float moveMarkX = 0.0f, moveMarkZ = 0.0f, moveMarkTime = 0.0f;   // the world map's green X: the last move order
    float mapDragX = 0.0f, mapDragY = 0.0f;   // how far the world map window has been dragged from the middle
    bool mapDragging = false;                 // its title bar is held
    float mapGrabX = 0.0f, mapGrabY = 0.0f;   // where the mouse took hold, less the drag at that moment
    bool mapOpen = false;          // the world map covers the ground: M or the View Map button
    bool rightHold = false;        // the right button is held over a point for the selected units
    // The Optimize All dialog, from the button beside Walk/Run: open, and whether its slider is being dragged
    bool optimizeAllOpen = false;
    bool optimizeSliderDrag = false;
    float holdX = 0.0f, holdZ = 0.0f;
    // A stack being dragged out of the inventory sidebar. Released outside the sidebar, it drops on the ground.
    bool itemDrag = false;
    Entity dragUnit = NULL_ENTITY;        // the unit whose inventory it came from
    int dragSlot = -1;                    // the inventory slot it came from, or -1 when dragging gear
    int dragGear = -1;                    // the gear slot it came from, or -1 when dragging from the inventory
    // The right-click menu for an inventory item: Use and Drop, opened at the mouse
    struct ItemMenu {
        bool open = false;
        Entity unit = NULL_ENTITY;
        int slot = -1;
        float x = 0.0f, y = 0.0f;
    };
    ItemMenu itemMenu;
    // How far the sidebar is scrolled, in pixels, and the unit that scroll was for. A different unit starts at the top.
    float sidebarScroll = 0.0f;
    Entity sidebarScrollUnit = NULL_ENTITY;

    Screen screen = Screen::Title;
    // The title and pause menus: the item lit by the arrow keys or the mouse, the screen it is for, and when that screen
    // opened (its items slide in)
    int menuSel = -1;
    // Sounds: each worked prop's time to its next working sound, the selection last frame, and how many units were down
    std::map<Entity, float> workSoundTimers;
    size_t lastSelectionKey = 0;
    Screen menuScreen = Screen::Playing;
    unsigned long long menuShownAt = 0;
    float menuMouseX = -1.0f, menuMouseY = -1.0f;
    Screen backScreen = Screen::Title;    // where Back goes from Settings
    SettingsMenuState settingsUi;         // the Settings screen's tab, open dropdown and dragged slider

    // The starting party: the units a new game begins with, edited in the party menu. It is rolled the
    // first time that menu opens, then kept for the session, so edits survive a trip back to the map list.
    Party party{};
    PartyUi partyUi;
    // The scenario page: the scenario picked (the top one, Vagabond, each time the page opens) and how far its list is
    // scrolled
    ScenarioId scenarioPick = ScenarioId::Vagabond;
    int scenarioScroll = 0;
    MapChoice pendingMap;                 // the map picked, which starts once the party is confirmed
    Rng partyRng;

    // Whether a game is loaded. Loading over one may need a confirmation first.
    bool gameActive = false;
    // When the current game was last saved. Empty until it's saved, so a new or loaded game counts as unsaved.
    std::optional<Uint64> lastSaveTicks;

    // Save/load popout. It sits over the current screen.
    bool panelOpen = false;
    bool panelSaveMode = false;           // true: Save Game, false: Load Game
    Screen panelFrom = Screen::Title;     // the screen the popout opened over
    Ask ask = Ask::None;                  // the question the popout is asking, if any
    std::optional<SaveSummary> pendingOverwrite;   // the save an overwrite question is about
    std::optional<SaveSummary> pendingLoad;        // the save an unsaved-progress question is about
    std::optional<SaveSummary> pendingDelete;      // the save the delete question is about
    bool deleteHolding = false;           // the Delete button is being held down
    Uint64 deleteHoldStart = 0;           // when that hold began, in SDL ticks
    Rect deleteHoldRect{ 0.0f, 0.0f, 0.0f, 0.0f };   // the Delete button being held
    std::string saveName;                 // text typed in the popout for a manual save
    size_t listOffset = 0;                // first save shown in the popout
    int viewH = 720;                      // window height, for how many save rows fit

    // Sort for the save list. The popout always opens sorted by time, newest first.
    SortKey sortKey = SortKey::Time;
    bool sortAsc = false;

    std::vector<SaveSummary> saves;

    bool leftDown = false, middleDown = false;
    bool running = true;
    bool screenshotPending = false;   // set by F12, and the next frame is saved before it is shown
    float pressX = 0.0f, pressY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;
    std::string lastTitle;

    // The unit the camera follows, set by a double-click on its panel card. A manual pan clears it.
    Entity followUnit = NULL_ENTITY;

    // The game log and the damage numbers over the units. Neither is saved, so each new or loaded level starts empty.
    GameLog gameLog;
    std::vector<FloatingNumber> floatingNumbers;
    std::vector<TradeLabel> tradeLabels;   // short labels over units for a second: trade values, and "+item" from work
    int infoTab = 1;        // the Information Panel's tab: 0 Message, 1 Game (shown first), 2 Time
    LogFilter logFilter;    // which game message kinds the Game tab shows
    bool logFilterOpen = false;
    int logOffset = 0;      // lines the game log is scrolled up from its newest line; 0 follows new lines
    size_t logSeen = 0;     // lines the log held last frame, so a view scrolled up stays on the same lines

    // Autosave countdown, in seconds of play
    float autoTimer = 0.0f;

    // Pivot ring under the camera's orbit point: fades in while the middle button is held
    float pivotAlpha = 0.0f;

    // Action crystal: appears where the last right-click order went, then fades out
    float actionAlpha = 0.0f;
    float actionX = 0.0f, actionZ = 0.0f;

    // Short message (lang("MISC_GAME_SAVED"), and so on)
    std::string notice;
    float noticeSec = 0.0f;

    Uint64 lastTicks = 0;
    // The frame rate shown in the corner (Show FPS): frames counted over half a second at a time
    Uint64 fpsSince = 0;
    int fpsFrames = 0;
    std::string fpsText = "-- FPS";
    SDL_Event e{};

    // ---- This frame ----
    std::chrono::steady_clock::time_point frameStart;   // the top of the frame, for the frame rate limit at its end
    int winW = 0, winH = 0;                // the window's size in pixels
    float dt = 0.0f;                       // seconds since the last frame, at most a quarter of a second
    std::vector<HudEntry> hud;             // the hover list
    float mouseX = 0.0f, mouseY = 0.0f;    // the mouse, as the hover list and the drawing see it
};
