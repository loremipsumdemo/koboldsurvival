#include "loading.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // the WinMain entry point for a Windows GUI program (no console window)
#include "language.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
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
#include <optional>
#include <string>
#include <utility>
#include <vector>
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

using namespace play;

namespace {

constexpr size_t HOVER_NAME_LIMIT = 10;      // the hover list shows up to this many names
constexpr Uint64 DELETE_HOLD_MS = 3000;      // holding Delete this long deletes the save
constexpr size_t NAME_LIMIT = 32;            // longest save name, in bytes
constexpr Uint64 UNSAVED_WARN_MS = 30000;    // loading over a game not saved this recently asks first
constexpr const char* SETTINGS_PATH = "settings.cfg";

// Draws one loading frame and keeps the window responsive while things load
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label = std::string(),
                  const std::string& category = std::string(), bool backdrop = true) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH, label, category, backdrop);
    SDL_GL_SwapWindow(window);
}

// Current time in milliseconds since 1970
long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Plays a sound effect now, on the slider it follows (sounds.h lists them all, with their files)
// A sound effect is not played again within this long of its last start: a click that lands on several things at once,
// or an order given to many units, plays it once
// volume and pan are for a world sound, heard from the camera (sounds.h); one too far off to hear is not played at all.
constexpr Uint64 SFX_REPEAT_GAP_MS = 50;
void playSfx(Sfx s, float volume = 1.0f, float pan = 0.0f) {
    static Uint64 lastStart[(int)Sfx::Count] = {};
    static bool started[(int)Sfx::Count] = {};
    if (volume <= 0.0f) return;
    const int i = (int)s;
    if (i >= 0 && i < (int)Sfx::Count) {
        const Uint64 now = SDL_GetTicks();
        if (started[i] && now - lastStart[i] < SFX_REPEAT_GAP_MS) return;
        started[i] = true;
        lastStart[i] = now;
    }
    const SfxInfo& info = sfxInfo(s);
    audio::playEffect(info.file, info.ui ? audio::Bus::Interface : audio::Bus::Effects, volume, pan);
}

// The size the title screen's name is drawn in before it is shrunk to fit: large enough that it is never enlarged,
// even on a 4K screen
constexpr float TITLE_NAME_POINTS = 200.0f;
// The menus' text (the title, New Game, the scenarios, the party menu, Settings and the Esc menu) is this much larger
// than the game's. The stat rows of the party menu's Stats tab stay at the game's size. The menus are laid out for a
// 720 pixel high window and grow with it, so in a smaller window the text grows less, to keep fitting its boxes:
// the full 1.5 times from 1080 pixels high, and the game's own size at 720.
constexpr float MENU_FONT_SCALE = 1.5f;
int menuFontPercent(int uiScale, int windowH) {
    const float grow = std::clamp(windowH / 720.0f, 1.0f, MENU_FONT_SCALE);
    return (int)std::lround(uiScale * grow);
}

// Working sounds: how often each plays while a prop is worked (real seconds), and how far from the camera's focus a
// worked prop is still heard, in tiles
constexpr float WORK_SOUND_SECONDS = 1.6f;
constexpr float FORGE_SOUND_SECONDS = 3.0f;
constexpr float WORK_SOUND_RANGE = 30.0f;

} // namespace

// The text fonts, opened at the Font size setting and for the selected language. Each main font has the other fonts
// as fallbacks (see fonts.h). owned holds every font opened, so they can all be closed.
struct UiFonts {
    TTF_Font* hud = nullptr;
    TTF_Font* ui = nullptr;
    TTF_Font* list = nullptr;
    TTF_Font* statsTitle = nullptr;
    TTF_Font* statsRow = nullptr;
    std::vector<TTF_Font*> owned;
    bool valid() const { return hud && ui && list && statsTitle && statsRow; }
};

void closeUiFonts(UiFonts& f);

UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId) {
    // 100% is the default size: 20 points at 100%
    const float size = 20.0f * pct / 100.0f;
    UiFonts f;
    // The HUD, the UI, the lists and the title are bold. The Stats popout is regular.
    f.hud = openFontChain(size, true, fontPath, languageId, f.owned);
    f.ui = openFontChain(size, true, fontPath, languageId, f.owned);
    f.list = openFontChain(size, true, fontPath, languageId, f.owned);
    f.statsTitle = openFontChain(size, false, fontPath, languageId, f.owned);
    f.statsRow = openFontChain(size, false, fontPath, languageId, f.owned);
    return f;
}

void closeUiFonts(UiFonts& f) {
    closeFonts(f.owned);
    f.hud = f.ui = f.list = f.statsTitle = f.statsRow = nullptr;
}

int main(int, char**) {
    startLogFile();
    std::set_terminate(onTerminate);
    SDL_Log("Kobold Game starting. Working folder: %s", std::filesystem::current_path().string().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO)) return fatalError(nullptr, std::string("SDL_Init failed: ") + SDL_GetError());
    if (!TTF_Init()) return fatalError(nullptr, std::string("TTF_Init failed: ") + SDL_GetError());
    // Every asset comes from data.pak next to the executable, except the music and sound effects: those are loose
    // files in the BGM and SFX folders beside it, read by the audio module.
    if (!pakOpen("data.pak")) return fatalError(nullptr, "Could not open data.pak. It must sit next to the game executable.");
    SDL_Log("data.pak open");
    // The factions and their reputations, from data/factioninfo.txt. Missing or bad, the built-in list is used.
    {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        std::string err;
        if (!pakFind("data/factioninfo.txt", bytes, size)) {
            SDL_Log("data/factioninfo.txt is missing; using the built-in factions");
        } else if (!loadFactions(std::string((const char*)bytes, size), err)) {
            SDL_Log("data/factioninfo.txt: %s; using the built-in factions", err.c_str());
        }
    }
    // Settings come first: the saved text size decides the fonts
    GameSettings settings = loadSettings(SETTINGS_PATH);
    UiFonts fonts = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    UiFonts menuFonts;            // opened in the loop, once the window's height is known (menuFontPercent)
    int appliedMenuPercent = 0;   // the size they were opened at; 0 to open them again
    int appliedFontScale = settings.fontScale;
    std::string appliedFontPath = settings.fontPath;
    std::string appliedLanguage = settings.language;
    {
        // A saved language whose file can't be found goes back to the default one, and the setting is saved
        std::string langErr;
        if (!loadLanguage(settings.language, langErr)) {
            SDL_Log("Language: %s. Using the default language.", langErr.c_str());
            settings.language = LANGUAGE_DEFAULT_ID;
            loadLanguage(settings.language, langErr);
            saveSettings(settings, SETTINGS_PATH);
        }
    }
    // The large title font, for the selected language, with the same fallbacks. It keeps the language it started with.
    std::vector<TTF_Font*> titleFontOwned;
    TTF_Font* font = openFontChain(64.0f, true, settings.fontPath, settings.language, titleFontOwned);
    if (!font || !fonts.valid()) return fatalError(nullptr, std::string("Could not open a font from data.pak: ") + SDL_GetError());
    SDL_Log("Settings, language and fonts loaded (language %s, UI scale %d%%)", settings.language.c_str(), settings.fontScale);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // 24 bits: the depth every graphics card offers for a window. Asking for 32 made Windows fall back to its
    // software renderer ("GDI Generic", OpenGL 1.1) on cards without it, and the game could not start.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // One window, made once. Destroying an OpenGL window and making another left the new one black on Windows.
    SDL_Window* window = SDL_CreateWindow("Kobold Survival", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) return fatalError(nullptr, std::string("Could not create the game window: ") + SDL_GetError());
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx || !SDL_GL_MakeCurrent(window, ctx))
        return fatalError(window, std::string("Could not start OpenGL 3.3: ") + SDL_GetError() +
                                  "\nUpdating the graphics driver may help.");
    {
        // The game needs OpenGL 3.3 on the graphics card. Windows' own software renderer only has 1.1.
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* card = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        int major = 0, minor = 0;
        if (version) std::sscanf(version, "%d.%d", &major, &minor);
        const bool software = card && std::string(card).find("GDI Generic") != std::string::npos;
        if (major < 3 || (major == 3 && minor < 3) || software)
            return fatalError(window, std::string("The game needs OpenGL 3.3 on the graphics card, and got OpenGL ") +
                                      (version ? version : "?") + " on " + (card ? card : "?") +
                                      ".\nUpdating the graphics driver may help.");
    }
    {
        int gotDepth = 0;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &gotDepth);
        SDL_Log("Depth buffer: %d bits", gotDepth);
    }

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
        return fatalError(window, std::string("GLEW init failed: ") + reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
    glGetError();
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    moveOldMapsFolder();
    DisplayInfo displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);

    // ---- Loading phase: textures and models load before the title screen appears ----
    Renderer renderer;
    // The first load has no backdrop behind the bar, and names what is loading under it
    if (!renderer.init(font, fonts.hud, fonts.ui,
                       [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what, false); }))
        return fatalError(window, "Loading the game's textures, models or shaders failed. log.txt beside the game has the details.");
    SDL_Log("Renderer loaded");
    renderer.setListFont(fonts.list);
    // The game's name on the title screen: a very large font, drawn shrunk so it stays sharp
    std::vector<TTF_Font*> nameFontOwned;
    renderer.setTitleFont(openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nameFontOwned));
    renderer.setStatsFonts(fonts.statsTitle, fonts.statsRow);

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
    auto placeFromSettings = [&](PanelId which) {
        const std::array<float, 4>& v = settings.panels[(size_t)idx(which)];
        PanelPlace p = defaultPanelPlace(which);   // its bar's side, until the first frame settles it from its place
        p.dx = v[0];
        p.dy = v[1];
        p.w = v[2];
        p.h = v[3];
        return p;
    };
    PanelPlace squadPlace = placeFromSettings(PanelId::Squad), unitPlace = placeFromSettings(PanelId::Unit),
               logPlace = placeFromSettings(PanelId::Info);
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
    // The stats popout for the lone selected unit, opened from the sidebar's Stats button
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
    partyRng.gen.seed((unsigned)std::time(nullptr));

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

    std::vector<SaveSummary> saves = listSaves();
    sortSaves(saves, sortKey, sortAsc);

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
    auto clearCombatText = [&]() {
        gameLog = GameLog{};
        floatingNumbers.clear();
        tradeLabels.clear();
        discardQueuedLabels();
        logOffset = 0;
        logSeen = 0;
    };

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

    Uint64 lastTicks = SDL_GetTicks();
    // The frame rate shown in the corner (Show FPS): frames counted over half a second at a time
    Uint64 fpsSince = lastTicks;
    int fpsFrames = 0;
    std::string fpsText = "-- FPS";
    SDL_Event e;

    auto refreshSaves = [&]() {
        saves = listSaves();
        sortSaves(saves, sortKey, sortAsc);
    };

    auto refreshMaps = [&]() {
        maps = listMaps();
        sortMaps(maps, mapSortByName, mapSortAsc);
    };

    auto showNotice = [&](const std::string& text, float seconds = 2.5f) {
        notice = text;
        noticeSec = seconds;
    };
    // A notice that something was refused (locked, taken, no room, out of reach...), with the refusal sound
    auto refuse = [&](const std::string& text) {
        showNotice(text);
        playSfx(Sfx::UiError);
    };

    // Closes the popout and drops any pending question
    auto closePanel = [&]() {
        panelOpen = false;
        ask = Ask::None;
        pendingOverwrite.reset();
        pendingLoad.reset();
        pendingDelete.reset();
        deleteHolding = false;
        SDL_StopTextInput(window);
    };

    // Opens the popout in save mode (with an empty name) or load mode. The sort goes back to the default.
    auto openPanel = [&](bool saveMode) {
        panelOpen = true;
        panelSaveMode = saveMode;
        panelFrom = screen;
        ask = Ask::None;
        pendingOverwrite.reset();
        pendingLoad.reset();
        pendingDelete.reset();
        deleteHolding = false;
        saveName.clear();
        listOffset = 0;
        sortKey = SortKey::Time;
        sortAsc = false;
        refreshSaves();
        if (saveMode) SDL_StartTextInput(window);
        else SDL_StopTextInput(window);
    };

    // True if loading now would throw away progress: a game is running and it hasn't been saved
    // in the last UNSAVED_WARN_MS
    auto wouldLoseProgress = [&]() -> bool {
        if (!gameActive) return false;
        if (!lastSaveTicks) return true;
        return SDL_GetTicks() - *lastSaveTicks > UNSAVED_WARN_MS;
    };

    // Loads a map and starts a new game on it, showing the level-start image
    auto startMapLevel = [&](const MapChoice& choice) {
        std::string err;
        levelSeed = (unsigned)std::time(nullptr);
        levelPlayerCamp = scenarioHasCamp(scenarioPick);   // only Full Random Camp starts with your camp
        // The title music ends as a new game starts, before its level loads
        audio::stopTracks();
        if (!prepareLevel(choice, world, terrain, renderer, rng, levelSeed, true, err,
                          [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what); })) {
            refuse(langf("MISC_LOAD_FAILED", { choice.name, err }));
            return;
        }
        // The party from the menu replaces the three units that the seed made: its people, then its mobs after them
        {
            std::vector<MobKind> mobKinds;
            for (const PartyMember& m : party) {
                if (m.mob != MobKind::None) mobKinds.push_back(m.mob);
            }
            const int people = partyPeople(party);
            ensurePlayerUnits(world, terrain, rng, people);
            ensurePlayerMobs(world, terrain, rng, mobKinds, people);
        }
        applyParty(world, party);
        // In Pack of Two the wolf starts out following its Vagabond
        if (scenarioPick == ScenarioId::PackOfTwo) {
            const Entity lead = firstSquadUnit(world);
            std::vector<Entity> beasts;
            for (Entity u : world.view<Unit, Commandable, Mob>()) beasts.push_back(u);
            if (lead != NULL_ENTITY && !world.has<Mob>(lead)) issueFollowOrder(world, beasts, lead);
        }
        clearCombatText();
        closePanel();
        currentMap = choice;
        // A new game starts with the default camera, not the zoom and angle the last game was left at. The focus is
        // clamped to the map, so the map's size has to be known before it is set.
        cam = Camera{};
        cam.mapSize = (float)terrain.size();
        focusCameraOnFirstUnit(world, cam);
        primeStreaming(world, terrain, cam);
        gs = GameState{};
        sel = Selection{};
        // The game starts with the unit the camera is on selected, and the camera following it as a double-click on its
        // panel card would, until the first manual pan
        clearSelection(world);
        const Entity firstUnit = firstSquadUnit(world);
        if (firstUnit != NULL_ENTITY) world.add(firstUnit, Selected{});
        leftDown = middleDown = false;
        itemDrag = false;
        followUnit = firstUnit;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
    };

    // Opens the scenario page for a picked map, with the top scenario (Vagabond) picked. The party menu comes after it.
    auto openParty = [&](const MapChoice& choice) {
        pendingMap = choice;
        scenarioPick = ScenarioId::Vagabond;
        scenarioScroll = 0;
        screen = Screen::ScenarioSelect;
    };
    // From the scenario page to the party menu, with a party rolled for the scenario. A new party is rolled every time,
    // so its names come from the language in use now. Back in the party menu returns to the scenario page.
    auto openPartyMenu = [&]() {
        party = scenarioParty(scenarioPick, partyRng);
        partyUi = PartyUi{};
        partyUi.resizable = scenarioResizable(scenarioPick);
        partyUi.editable = scenarioEditable(scenarioPick);
        partyUi.mobKindEditable = scenarioMobKindEditable(scenarioPick);
        backScreen = Screen::ScenarioSelect;
        screen = Screen::PartySetup;
    };

    // Stops typing into the name box, if it is taking text
    auto stopNameEdit = [&]() {
        if (partyUi.editingName) SDL_StopTextInput(window);
        partyUi.editingName = false;
        partyUi.composition.clear();
    };

    // Closes the open dropdown, if there is one
    auto closeDropdown = [&]() {
        partyUi.dropKind = DropKind::None;
        partyUi.dropScroll = 0;
    };

    // Writes the current game to a file. Used for manual saves and autosaves alike.
    auto writeSave = [&](const std::string& path, const std::string& name, bool autosave) -> bool {
        SaveData d = captureWorld(world, gs, cam, levelSeed, nowMs(), name, currentMap);
        d.autosave = autosave;
        bool ok = writeSaveFile(path, d);
        if (ok) lastSaveTicks = SDL_GetTicks();
        refreshSaves();
        return ok;
    };

    // Loads a save and starts play. The popout closes.
    auto loadSave = [&](std::string path) {
        SaveData d;
        if (!readSaveFile(path, d)) {
            refuse(lang("MISC_READ_FAILED"));
            refreshSaves();
            return;
        }
        if (d.mapStem.empty()) {
            showNotice(lang("MISC_SAVE_OLD"));
            refreshSaves();
            return;
        }
        std::optional<MapChoice> found = findMap(listMaps(), d.mapSource, d.mapStem);
        if (!found) {
            refuse(langf("MISC_MAP_MISSING", { d.name }));
            refreshSaves();
            return;
        }
        const MapChoice choice = *found;
        std::string err;
        // The title music ends as a saved game starts, before its level loads
        audio::stopTracks();
        if (!restoreLevel(world, terrain, renderer, rng, d, choice, err)) {
            refuse(langf("MISC_MAP_LOAD_FAILED", { choice.name, err }));
            refreshSaves();
            return;
        }
        currentMap = choice;
        levelSeed = d.seed;
        clearCombatText();
        gs = d.gs;
        setPlayerReputation(d.reputation);   // older saves have none, so every standing is its default
        // Older saves have no camera, so they start from the default one, not the last game's view
        cam = Camera{};
        cam.mapSize = (float)terrain.size();
        focusCameraOnFirstUnit(world, cam);
        applySaveCamera(d.cam, cam);
        primeStreaming(world, terrain, cam);
        sel = Selection{};
        leftDown = middleDown = false;
        itemDrag = false;
        followUnit = NULL_ENTITY;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
        closePanel();
        showNotice(langf("MISC_LOADED", { d.name }));
        playSfx(Sfx::UiLoaded);
    };

    // Writes an autosave into the next slot. Slots rotate: an empty one first, otherwise the oldest.
    auto autosaveNow = [&]() -> bool {
        std::string path = nextAutosavePath();
        std::string name = std::filesystem::path(path).stem().string();   // autosave0, and so on
        return writeSave(path, name, true);
    };

    // Deletes the save the question is about, then closes the question
    auto performDelete = [&]() {
        if (!pendingDelete) {
            ask = Ask::None;
            return;
        }
        const std::string name = pendingDelete->name;
        std::error_code ec;
        std::filesystem::remove(pendingDelete->path, ec);
        ask = Ask::None;
        pendingDelete.reset();
        deleteHolding = false;
        refreshSaves();
        showNotice(ec ? langf("MISC_DELETE_FAILED", { name }) : langf("MISC_DELETED", { name }));
    };

    // Pans the camera with WASD or the arrow keys. Edge scrolling pans when the mouse is at a window edge.
    auto panWithKeys = [&](float dt, bool edgeScroll) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float right = 0.0f, forward = 0.0f;   // edge scrolling adds to these below
        if (percentPrompt.open) keys = nullptr;   // the keys go into the Behavior tab's # prompt
        if (keys && (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])) right += 1.0f;
        if (keys && (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])) right -= 1.0f;
        if (keys && (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])) forward += 1.0f;
        if (keys && (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])) forward -= 1.0f;

        if (edgeScroll) {
            int winW = 0, winH = 0;
            SDL_GetWindowSizeInPixels(window, &winW, &winH);
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            if (mx < 6.0f) right -= 1.0f;
            if (mx > winW - 6.0f) right += 1.0f;
            if (my < 6.0f) forward += 1.0f;
            if (my > winH - 6.0f) forward -= 1.0f;
        }

        // Holding shift pans twice as fast
        const bool fast = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        float panSpeed = cam.distance * 0.9f * dt * (fast ? 2.0f : 1.0f);
        // Any manual move takes the camera off the unit it was following
        if (right != 0.0f || forward != 0.0f) followUnit = NULL_ENTITY;
        cam.pan(right * panSpeed, forward * panSpeed);
    };

    // ---- Settings: the audio buses and the window follow the settings as they change ----
    auto applyAudio = [&]() {
        audio::setGlobal(settings.globalVolume, settings.globalOn);
        audio::setBus(audio::Bus::Music, settings.musicVolume, settings.musicOn);
        audio::setBus(audio::Bus::Ambient, settings.ambientVolume, settings.ambientOn);
        audio::setBus(audio::Bus::Effects, settings.effectsVolume, settings.effectsOn);
        audio::setBus(audio::Bus::Interface, settings.interfaceVolume, settings.interfaceOn);
    };
    // Reads the monitors again, then makes the window show the settings, which are normalized to what it can show
    auto applyVideo = [&]() {
        displayInfo = queryDisplays(settings.monitor);
        applyDisplay(window, settings, displayInfo);
        SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
    };
    auto saveSettingsFile = [&]() { saveSettings(settings, SETTINGS_PATH); };
    // How Optimize and Optimize All choose, from the settings the Optimize All dialog sets
    auto gearPrefs = [&]() {
        GearPrefs p;
        p.attackShare = settings.optimizeAttackShare;
        p.accessories = settings.optimizeAccessories;
        p.twoHanded = settings.optimizeTwoHanded;
        p.valueTies = settings.optimizeValueTies;
        return p;
    };
    // Applies what a change touched. It is saved when the change is final: a slider saves when released.
    // The Font size setting: every text font is reopened at the new size, and the text drawn with the old ones is dropped
    auto applyFontScale = [&]() {
        if (settings.fontScale == appliedFontScale && settings.fontPath == appliedFontPath
            && settings.language == appliedLanguage) return;
        UiFonts next = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
        if (!next.valid()) {
            closeUiFonts(next);
            return;
        }
        renderer.setFonts(next.hud, next.ui, next.list, next.statsTitle, next.statsRow);
        closeUiFonts(fonts);
        fonts = next;
        appliedMenuPercent = 0;   // the menu fonts follow at the top of the next frame
        // The name's font follows the language and the font picked, too
        std::vector<TTF_Font*> nextName;
        if (TTF_Font* f = openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nextName)) {
            renderer.setTitleFont(f);
            closeFonts(nameFontOwned);
            nameFontOwned = nextName;
        }
        appliedFontScale = settings.fontScale;
        appliedFontPath = settings.fontPath;
        appliedLanguage = settings.language;
    };
    // The Language setting: its text file is reloaded. A file that can't be found goes back to the default language,
    // and that choice is saved.
    auto applyLanguage = [&]() {
        std::string err;
        if (loadLanguage(settings.language, err)) return;
        showNotice(err + ". Using the default language.");
        settings.language = LANGUAGE_DEFAULT_ID;
        loadLanguage(settings.language, err);
        saveSettingsFile();
    };
    auto settingsCommit = [&](const SettingsChange& c, bool save) {
        if (!c.any) return;
        if (c.display) applyVideo();
        else if (c.vsync) SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
        if (c.audio) applyAudio();
        if (c.language) {
            applyLanguage();
            // The party's names came from the old language, so the party is rolled again in the new one
            party = scenarioParty(scenarioPick, partyRng, partyPeople(party));
        }
        if (c.fonts || c.language) applyFontScale();
        if (save) saveSettingsFile();
    };
    auto leaveSettings = [&]() {
        settingsUi.openRow = -1;
        settingsUi.dragRow = -1;
        screen = backScreen;
    };
    // A left click on the Settings screen: a tab, a dropdown or one of its options, a toggle, a slider or a button
    // Puts the Squad Panel, the Information Panel and the Unit Panel back in their default places and sizes, locked,
    // and keeps that in the settings
    auto resetPanels = [&]() {
        squadPlace = defaultPanelPlace(PanelId::Squad);
        logPlace = defaultPanelPlace(PanelId::Info);
        unitPlace = defaultPanelPlace(PanelId::Unit);
        for (int i = 0; i < PANEL_COUNT; ++i) {
            panelLocked[i] = true;
            settings.panels[(size_t)i] = {};
        }
        saveSettingsFile();
    };
    auto settingsClick = [&](float x, float y, int W, int H) {
        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
        const SettingsClick c = hitSettings(layoutSettings(W, H, rows, settingsUi), rows, x, y);
        // Every control that does something clicks. Closing a dropdown, and a click on nothing, do not.
        // A tab, a switch, a dropdown and Back each have their own sound. Closing a dropdown, a slider drag (it sounds as
        // it is let go) and a click on nothing are silent.
        switch (c.kind) {
        case ClickKind::None: case ClickKind::CloseDropdown: case ClickKind::LevelDrag: break;
        case ClickKind::Tab: playSfx(Sfx::UiTab); break;
        case ClickKind::ToggleRow: case ClickKind::LevelToggle: playSfx(Sfx::UiToggle); break;
        case ClickKind::OpenDropdown: playSfx(Sfx::UiMenuOpen); break;
        case ClickKind::Back: playSfx(Sfx::UiBack); break;
        default: playSfx(Sfx::UiClick); break;
        }
        switch (c.kind) {
        case ClickKind::Tab:
            settingsSelectTab(settingsUi, static_cast<SettingsTab>(c.index));
            break;
        case ClickKind::OpenDropdown:
            settingsOpenDropdown(settingsUi, rows, c.row);
            break;
        case ClickKind::CloseDropdown:
            settingsCloseDropdown(settingsUi);
            break;
        case ClickKind::PickOption:
            settingsCloseDropdown(settingsUi);
            settingsCommit(pickOption(rows[c.row].id, rows[c.row].options[c.index], settings), true);
            break;
        case ClickKind::ToggleRow:
        case ClickKind::LevelToggle:
            settingsCommit(setRowOn(rows[c.row].id, !rows[c.row].on, settings), true);
            break;
        case ClickKind::LevelDrag:
            settingsUi.dragRow = c.row;
            settingsCommit(setRowLevel(rows[c.row].id, c.index, settings), false);
            break;
        case ClickKind::Defaults:
            settingsCloseDropdown(settingsUi);
            settingsCommit(resetAllSettings(settings), true);
            break;
        case ClickKind::Action:
            if (rows[c.row].id == RowId::ResetPanels) resetPanels();
            break;
        case ClickKind::Back:
            leaveSettings();
            break;
        case ClickKind::None:
            break;
        }
    };

    // Runs a button's action
    auto performAction = [&](Action action, int param) {
        // Going back or saying no has its own sound, and so does resuming the game
        const bool back = action == Action::Back || action == Action::ReturnToTitle || action == Action::CancelName ||
                          action == Action::OverwriteNo || action == Action::LoadNo || action == Action::DeleteNo;
        playSfx(back ? Sfx::UiBack : action == Action::Resume ? Sfx::UiResume : Sfx::UiClick);
        switch (action) {
        case Action::Continue:
            if (!saves.empty()) loadSave(saves.front().path);
            return;
        case Action::NewGame: {
            // New Game always starts on the main map, so there is no map to pick
            const std::optional<MapChoice> mainMap = findMap(listMaps(), "packed", MAIN_MAP_STEM);
            if (!mainMap) {
                showNotice("The main map is missing from data.pak");
                return;
            }
            openParty(*mainMap);
            return;
        }
        case Action::PickMap:
            if (param >= 0 && param < (int)maps.size()) openParty(maps[param]);
            return;
        case Action::MapSortName:
        case Action::MapSortTime: {
            // Clicking the active column flips the order. A new column starts ascending.
            const bool byName = action == Action::MapSortName;
            if (mapSortByName == byName) {
                mapSortAsc = !mapSortAsc;
            } else {
                mapSortByName = byName;
                mapSortAsc = true;
            }
            sortMaps(maps, mapSortByName, mapSortAsc);
            mapOffset = 0;
            return;
        }
        case Action::MapScrollUp: {
            const size_t page = visibleSaveRows(viewH);
            mapOffset = (mapOffset > page) ? mapOffset - page : 0;
            return;
        }
        case Action::MapScrollDown: {
            const size_t page = visibleSaveRows(viewH);
            const size_t maxOffset = maps.size() > page ? maps.size() - page : 0;
            mapOffset = std::min(mapOffset + page, maxOffset);
            return;
        }
        case Action::PartyPick:
            partyUi.selected = std::clamp(param, 0, (int)party.size() - 1);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyTabPick:
            partyUi.tab = (PartyTab)std::clamp(param, 0, PARTY_TAB_COUNT - 1);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyNameField:
            closeDropdown();
            if (!partyUi.editingName) SDL_StartTextInput(window);
            partyUi.editingName = true;
            return;
        case Action::PartyBodyBox:
            stopNameEdit();
            partyUi.dropKind = DropKind::Body;
            partyUi.dropScroll = 0;
            return;
        case Action::PartySpriteBox:
            // The list opens scrolled so the unit's current sprite is near the middle
            stopNameEdit();
            partyUi.dropKind = DropKind::Sprite;
            partyUi.dropScroll = clampDropScroll(pickedMember(party, partyUi).sprite - 1 - DROP_VISIBLE_ROWS / 2,
                                                 UNIT_SPRITE_COUNT);
            return;
        case Action::PartyMobBox:
            // A mob member's kind: a list of the regular kinds, in Custom and Beastmaster only
            if (!partyUi.mobKindEditable) return;
            stopNameEdit();
            partyUi.dropKind = DropKind::MobKind;
            partyUi.dropSlot = 0;
            partyUi.dropScroll = 0;
            return;
        case Action::PartyAddMob:
            // The list of regular kinds; picking one adds a mob of that kind
            if (!partyUi.resizable || (int)party.size() >= PARTY_MAX_UNITS) return;
            stopNameEdit();
            partyUi.dropKind = DropKind::MobKind;
            partyUi.dropSlot = -1;
            partyUi.dropScroll = 0;
            return;
        case Action::PartyRandomUnit: {
            // A new member for the picked unit, with a name that no other unit in the party has
            const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
            if (party[picked].mob != MobKind::None) {
                // A mob: in Custom a new one of the same kind, rolled afresh; elsewhere it stays as the scenario made it
                if (partyUi.editable) party[picked] = mobMember(party[picked].mob, partyRng);
                stopNameEdit();
                closeDropdown();
                return;
            }
            std::vector<std::string> others;
            for (int i = 0; i < (int)party.size(); ++i) {
                if (i != picked) others.push_back(party[i].name);
            }
            PartyMember fresh = randomMember(partyRng, others);
            if (!partyUi.editable) {
                // Outside Custom only who the unit is changes: its stats, gear, items and hunger stay as the scenario made them
                const PartyMember& old = party[picked];
                fresh.stats = old.stats;
                fresh.gear = old.gear;
                fresh.items = old.items;
                fresh.hunger = old.hunger;
            }
            party[picked] = fresh;
            stopNameEdit();
            closeDropdown();
            return;
        }
        case Action::PartyGearSlot:
            if (!partyUi.editable) return;
            stopNameEdit();
            partyUi.dropKind = DropKind::Gear;
            partyUi.dropSlot = std::clamp(param, 0, GEAR_SLOTS - 1);
            partyUi.dropScroll = 0;
            return;
        case Action::PartyStep: {
            // Minus is the even parameter and plus the odd one. Holding Shift moves a level by ten.
            if (!partyUi.editable) return;
            const StatId stat = (StatId)(param / 2);
            const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
            Stats& st = pickedMember(party, partyUi).stats;
            setStatLevel(st, stat, levelOf(st, stat) + ((param % 2) != 0 ? step : -step));
            return;
        }
        case Action::PartyInvSlot:
            stopNameEdit();
            partyUi.stackSlot = std::clamp(param, 0, INVENTORY_SLOTS - 1);
            if (!partyUi.editable) return;   // the slot is picked to read about, with no list to change it
            partyUi.dropKind = DropKind::Item;
            partyUi.dropSlot = partyUi.stackSlot;
            partyUi.dropScroll = 0;
            return;
        case Action::PartyCount: {
            if (!partyUi.editable) return;
            ItemStack& st = pickedMember(party, partyUi).items[partyUi.stackSlot];
            if (st.item > 0) st.count = std::clamp(st.count + param, 1, stackLimit(st.item));
            return;
        }
        case Action::PartyDropPick: {
            const std::vector<DropEntry> entries = dropdownEntries(party, partyUi);
            if (param < 0 || param >= (int)entries.size()) return;
            PartyMember& m = pickedMember(party, partyUi);
            const int value = entries[param].value;
            switch (partyUi.dropKind) {
            case DropKind::Sprite:
                m.sprite = value;
                break;
            case DropKind::Body:
                // A new body type brings its own model with it; the model can still be changed after
                m.body = (BodyType)value;
                m.sprite = modelForBody(m.body);
                break;
            case DropKind::Gear:
                if (partyUi.editable) m.gear[partyUi.dropSlot] = value;
                break;
            case DropKind::Item: {
                if (!partyUi.editable) break;
                ItemStack& st = m.items[partyUi.dropSlot];
                if (value == 0) {
                    st = ItemStack{};
                } else {
                    // Choosing the same item again keeps its count. Anything else starts at one.
                    const int count = st.item == value ? st.count : 1;
                    st = ItemStack{ value, std::clamp(count, 1, stackLimit(value)) };
                }
                break;
            }
            case DropKind::MobKind:
                if (partyUi.dropSlot < 0) {
                    // Add mob: a new member at the end of the party, picked and scrolled to
                    if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
                        party.push_back(mobMember((MobKind)value, partyRng));
                        partyUi.selected = (int)party.size() - 1;
                        partyUi.listScroll = (int)party.size();
                    }
                } else if (partyUi.mobKindEditable && m.mob != MobKind::None && m.mob != (MobKind)value) {
                    // A mob of the new kind, with that kind's stats. A name typed for it is kept; the old kind's own
                    // name gives way to the new kind's.
                    const bool renamed = cleanName(m.name) != lang(mobInfo(m.mob).nameKey);
                    const std::string name = m.name;
                    m = mobMember((MobKind)value, partyRng);
                    if (renamed) m.name = name;
                }
                break;
            case DropKind::None:
                break;
            }
            closeDropdown();
            return;
        }
        case Action::PartyDropScroll: {
            const int count = (int)dropdownEntries(party, partyUi).size();
            partyUi.dropScroll = clampDropScroll(partyUi.dropScroll + param, count);
            return;
        }
        case Action::PartyDropBack:
        case Action::PartyHeading:
        case Action::PartyNote:
        case Action::PartyStatLabel:
        case Action::PartyMessage:
            return;
        case Action::PartyAddUnit:
            // A random unit at the end of the party. The list is scrolled to the end, and the layout clamps it.
            if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
                addRandomMember(party, partyRng);
                partyUi.selected = (int)party.size() - 1;
                partyUi.listScroll = (int)party.size();
                stopNameEdit();
                closeDropdown();
            }
            return;
        case Action::PartyRemoveUnit:
            // The picked unit leaves the party. The party can be emptied, but a game needs at least one unit to start.
            if (partyUi.resizable && !party.empty()) {
                const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
                party.erase(party.begin() + picked);
                partyUi.selected = std::clamp(picked, 0, (int)party.size() - 1);
                stopNameEdit();
                closeDropdown();
            }
            return;
        case Action::PartyRandomAll:
            party = scenarioParty(scenarioPick, partyRng, partyPeople(party));
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyConfirm:
            if (party.empty() || !partyNamesValid(party)) return;
            stopNameEdit();
            closeDropdown();
            startMapLevel(pendingMap);
            return;
        case Action::LoadGame:
        case Action::Load:
            openPanel(false);
            return;
        case Action::Save:
            openPanel(true);
            return;
        case Action::LoadSlot:
            if (param < 0 || param >= (int)saves.size()) return;
            if (panelSaveMode) {
                // Save mode: a click copies that save's name into the name field
                saveName = saves[param].name;
            } else if (wouldLoseProgress()) {
                // Load mode, and the current game would be lost: ask first
                pendingLoad = saves[param];
                ask = Ask::LoadUnsaved;
            } else {
                loadSave(saves[param].path);
            }
            return;
        case Action::LoadYes: {
            if (!pendingLoad) {
                ask = Ask::None;
                return;
            }
            SaveSummary target = *pendingLoad;
            loadSave(target.path);
            return;
        }
        case Action::LoadNo:
            ask = Ask::None;
            pendingLoad.reset();
            return;
        case Action::DeleteSave:
            if (param < 0 || param >= (int)saves.size()) return;
            pendingDelete = saves[param];
            ask = Ask::Delete;
            return;
        case Action::DeleteHold:
            // Handled by the hold timer, not by clicks
            return;
        case Action::DeleteNo:
            ask = Ask::None;
            pendingDelete.reset();
            deleteHolding = false;
            return;
        case Action::ScrollUp: {
            // Jump up one page of saves
            const size_t page = visibleSaveRows(viewH);
            listOffset = (listOffset > page) ? listOffset - page : 0;
            return;
        }
        case Action::ScrollDown: {
            const size_t page = visibleSaveRows(viewH);
            size_t maxOffset = saves.size() > page ? saves.size() - page : 0;
            listOffset = std::min(listOffset + page, maxOffset);
            return;
        }
        case Action::SortName:
        case Action::SortTime: {
            // Clicking the active column flips the direction. A new column starts ascending.
            SortKey k = (action == Action::SortName) ? SortKey::Name : SortKey::Time;
            if (sortKey == k) {
                sortAsc = !sortAsc;
            } else {
                sortKey = k;
                sortAsc = true;
            }
            sortSaves(saves, sortKey, sortAsc);
            listOffset = 0;
            return;
        }
        case Action::ConfirmName: {
            // Save button. A name that matches an existing save asks before overwriting it.
            std::string name = trimmedName(saveName);
            if (name.empty()) return;
            if (std::optional<SaveSummary> existing = findSaveByName(saves, name)) {
                pendingOverwrite = existing;
                ask = Ask::Overwrite;
                return;
            }
            bool ok = writeSave(manualSavePath(nowMs()), name, false);
            closePanel();
            showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
            playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
            return;
        }
        case Action::OverwriteYes: {
            if (!pendingOverwrite) {
                ask = Ask::None;
                return;
            }
            SaveSummary target = *pendingOverwrite;
            // Keep the save's file and its AUTO or MANUAL type; only the contents change
            bool ok = writeSave(target.path, target.name, target.autosave);
            closePanel();
            showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
            playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
            return;
        }
        case Action::OverwriteNo:
            ask = Ask::None;
            pendingOverwrite.reset();
            return;
        case Action::CancelName:
            // Close button
            closePanel();
            return;
        case Action::Settings:
            backScreen = screen;
            screen = Screen::Settings;
            settingsUi.openRow = -1;
            settingsUi.dragRow = -1;
            rescanLanguages();
            return;
        case Action::Language:
            // The start menu's Language button: the same Settings screen, opened on its Language tab
            backScreen = screen;
            screen = Screen::Settings;
            settingsUi.tab = SettingsTab::Language;
            settingsUi.openRow = -1;
            settingsUi.dragRow = -1;
            rescanLanguages();
            return;
        case Action::Back:
            stopNameEdit();
            closeDropdown();
            screen = backScreen;
            return;
        case Action::Resume:
            screen = Screen::Playing;
            return;
        case Action::ReturnToTitle:
            screen = Screen::Title;
            gameActive = false;
            refreshSaves();
            return;
        case Action::Quit:
            running = false;
            return;
        }
    };

    // Without an audio output the game runs silent, so this result is not checked
    audio::init();
    applyAudio();

    SDL_Log("Start-up done, showing the title screen");
    while (running) {
        // Taken at the top of the frame, for the frame rate limit at its end
        const auto frameStart = std::chrono::steady_clock::now();

        // Music and ambient sound follow the screen. A screen that names a track starts it, nullptr keeps what is
        // playing, and "" is silence. Each request is tried once, so a missing track is not reloaded every frame.
        const Soundtrack sound = soundtrackFor(screen);
        if (sound.music) audio::playTrack(audio::Bus::Music, sound.music);
        if (sound.ambient) audio::playTrack(audio::Bus::Ambient, sound.ambient);

        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        viewH = winH;

        // The menu fonts, opened again when the window's height or the text settings call for another size
        if (const int pct = menuFontPercent(settings.fontScale, winH); pct != appliedMenuPercent) {
            UiFonts next = openUiFonts(pct, settings.fontPath, settings.language);
            if (next.valid()) {
                renderer.setMenuFonts(next.hud, next.ui, next.list, next.statsTitle, next.statsRow);
                closeUiFonts(menuFonts);
                menuFonts = next;
            } else {
                closeUiFonts(next);
            }
            appliedMenuPercent = pct;
        }

        // Own units by entity number, lowest first. The bottom panel shows them in this order, and the
        // camera starts on the first of them, which is also the first party member.
        auto ownUnitList = [&]() {
            std::vector<Entity> units = world.view<Unit, Transform, Commandable>();
            std::sort(units.begin(), units.end());
            return units;
        };
        // The sidebar is up for the lone selected unit, yours or a red one
        auto sidebarUp = [&]() { return sidebarUnitOf(world) != NULL_ENTITY; };
        // The Add Behavior list for a unit: every behavior in the game it can be given. The needs behaviors (not for a
        // mob) and the walks first, then Auto-Harvest of each resource kind standing in your claims, then Auto-Haul to
        // each storage prop in your claims, nearest first. Each says whether the unit holds it already.
        auto behaviorOptionsFor = [&](Entity u) {
            std::vector<BehaviorOption> out;
            const Behaviors* b = world.get<Behaviors>(u);
            if (!b) return out;
            auto offer = [&](const BehaviorRule& r, std::string label) {
                BehaviorOption o;
                o.rule = r;
                o.label = std::move(label);
                o.have = holdsBehavior(b->rules, r);
                out.push_back(std::move(o));
            };
            for (BehaviorKind k : addableKinds(world.has<Mob>(u))) {
                BehaviorRule r;
                r.kind = k;
                offer(r, behaviorName(r));
            }
            const std::vector<Claim>& claims = claimsOf(world);
            std::array<bool, PROP_KIND_COUNT> harvestable{};
            std::vector<std::pair<float, Entity>> stores;
            const Transform* ut = world.get<Transform>(u);
            for (Entity p : world.view<Prop, Transform>()) {
                const Transform* pt = world.get<Transform>(p);
                if (!claimAt(claims, pt->x, pt->z, FACTION_PLAYER)) continue;
                const PropKind kind = world.get<Prop>(p)->kind;
                if (idx(kind) >= 0 && idx(kind) < PROP_KIND_COUNT && yieldsItems(kind)) harvestable[(size_t)idx(kind)] = true;
                if (world.has<Storage>(p)) {
                    const float d = ut ? std::hypot(pt->x - ut->x, pt->z - ut->z) : 0.0f;
                    stores.push_back({ d, p });
                }
            }
            // Ones the unit holds count too, so a list can always show them, wherever their props are now
            for (const BehaviorRule& r : b->rules) {
                if (r.kind == BehaviorKind::AutoHarvest && r.propKind >= 0 && r.propKind < PROP_KIND_COUNT) harvestable[(size_t)r.propKind] = true;
            }
            for (int k = 0; k < PROP_KIND_COUNT; ++k) {
                if (!harvestable[(size_t)k]) continue;
                BehaviorRule r;
                r.kind = BehaviorKind::AutoHarvest;
                r.propKind = k;
                offer(r, behaviorName(r));
            }
            std::sort(stores.begin(), stores.end());
            constexpr size_t STORES_SHOWN = 8;   // the nearest ones, so the list stays short in a big camp
            for (size_t i = 0; i < stores.size() && i < STORES_SHOWN; ++i) {
                BehaviorRule r;
                r.kind = BehaviorKind::AutoHaul;
                r.prop = stores[i].second;
                r.propKind = idx(world.get<Prop>(r.prop)->kind);
                offer(r, behaviorName(r) + "  (" + std::to_string((int)std::lround(stores[i].first)) + "m)");
            }
            return out;
        };
        // The sidebar's layout for a window of W by H, for the unit it shows, scrolled by the given pixels
        auto sidebarFor = [&](int W, int H, float scroll) {
            const Entity shown = sidebarUnitOf(world);
            const Behaviors* b = shown != NULL_ENTITY ? world.get<Behaviors>(shown) : nullptr;
            // The Behavior tab's list is for one unit: another one shown shuts it
            if (shown != behaviorUiUnit) {
                behaviorUiUnit = shown;
                behaviorAddOpen = false;
            }
            const bool editable = shown != NULL_ENTITY && world.has<Commandable>(shown);
            BehaviorUi bui;
            std::vector<BehaviorOption> options;
            if (b && editable && unitTab == UnitTab::Behavior) {
                if (behaviorAddOpen) {
                    options = behaviorOptionsFor(shown);
                    bui.options = (int)options.size();
                }
            }
            SidebarLayout L = layoutInventorySidebar(W, H, b ? (int)b->rules.size() : 0, scroll, unitOpen, unitTab, unitPlace,
                                                     renderer.profileCardHeight(H), bui);
            if (!editable || unitTab != UnitTab::Behavior) L.behaviorAdd = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };   // nothing to add to
            L.behaviorOptionInfo = std::move(options);
            // A mob has no gear slots, nor gear buttons, so nothing there can be clicked or dragged. Its inventory is
            // Not available until it is a corpse: no slots, and no sort button.
            if (shown != NULL_ENTITY && world.has<Mob>(shown)) {
                L.gear.clear();
                L.optimizeButton = L.removeAllButton = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
                if (!world.has<Corpse>(shown)) {
                    L.slots.clear();
                    L.sortButton = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
                }
            }
            return L;
        };
        // Only your own units can be changed from the sidebar. A red unit's sidebar is for looking at.
        auto shownIsYours = [&]() {
            const Entity shown = sidebarUnitOf(world);
            return shown != NULL_ENTITY && world.has<Commandable>(shown);
        };
        // Sorts a unit's inventory by item type
        auto sortInventory = [&](Entity unit) {
            Inventory* inv = world.get<Inventory>(unit);
            if (!inv) return;
            sortInventoryByType(*inv);
            playSfx(Sfx::UiClick);
        };
        // A click on one part of a behavior row: the on box switches it, # asks for its percent, the priority buttons
        // move it, and the delete button removes it
        auto behaviorClick = [&](Entity unit, const BehaviorHit& hit) {
            Behaviors* b = world.get<Behaviors>(unit);
            if (!b || hit.row < 0 || hit.row >= (int)b->rules.size()) return;
            const size_t row = (size_t)hit.row;
            BehaviorRule& r = b->rules[row];
            switch (hit.part) {
            case BehaviorPart::Toggle:
                r.on = !r.on;
                r.cooldown = 0.0f;
                if (r.on) b->wait = 0.0f;   // an automated behavior switched on looks for its step at once
                break;
            case BehaviorPart::Number:
                // Eat, Heal and Rest: the prompt asks for the percent, once
                if (!hasPercent(r.kind)) return;
                percentPrompt = PercentPrompt{};
                percentPrompt.open = true;
                percentPrompt.unit = unit;
                percentPrompt.row = (int)row;
                percentPrompt.kind = r.kind;
                percentPrompt.text = std::to_string(r.percent);   // what it is now, to change or type over
                SDL_StartTextInput(window);
                playSfx(Sfx::UiClick);
                return;
            case BehaviorPart::Up:
            case BehaviorPart::Down:
                if (!moveBehavior(b->rules, row, hit.part == BehaviorPart::Up ? -1 : 1)) return;
                break;
            case BehaviorPart::Remove:
                if (!removeBehavior(b->rules, row)) return;
                break;
            case BehaviorPart::None:
                return;
            }
            b->pending = true;   // Eat and Heal are checked again, so the change takes effect at once
            playSfx(Sfx::UiClick);
        };
        // Shuts the # prompt. keep: the typed percent is set, if it is in the behavior's range; otherwise the prompt
        // stays up and says why. Cancel (keep false) leaves the percent as it was.
        auto answerPercentPrompt = [&](bool keep) {
            if (!percentPrompt.open) return;
            if (keep) {
                Behaviors* b = world.get<Behaviors>(percentPrompt.unit);
                const int row = percentPrompt.row;
                // The list may have changed under it (the unit fell, was traded away): then there is nothing to set
                if (!b || row < 0 || row >= (int)b->rules.size() || b->rules[(size_t)row].kind != percentPrompt.kind) {
                    keep = false;
                } else {
                    const PercentRange range = percentRange(percentPrompt.kind);
                    const int value = percentPrompt.text.empty() ? -1 : std::atoi(percentPrompt.text.c_str());
                    if (!percentInRange(percentPrompt.kind, value)) {
                        percentPrompt.error = langf("UILABEL_BEHAVIOR_PROMPT_BAD", { std::to_string(range.lo), std::to_string(range.hi) });
                        playSfx(Sfx::UiBack);
                        return;
                    }
                    BehaviorRule& r = b->rules[(size_t)row];
                    if (setPercent(r, value)) {
                        r.cooldown = 0.0f;
                        b->pending = true;   // checked again at once
                    }
                }
            }
            percentPrompt = PercentPrompt{};
            SDL_StopTextInput(window);
            playSfx(keep ? Sfx::UiClick : Sfx::UiBack);
        };
        // A click on the Add Behavior button or its list. False when the click was on neither.
        auto behaviorExtraClick = [&](Entity unit, const SidebarLayout& sl, float x, float y) {
            const BehaviorExtraHit hit = behaviorExtraHitAt(sl, x, y);
            if (hit.add) {
                behaviorAddOpen = !behaviorAddOpen;
                playSfx(Sfx::UiClick);
                return true;
            }
            if (hit.option >= 0) {
                const std::vector<BehaviorOption> options = behaviorOptionsFor(unit);
                Behaviors* b = world.get<Behaviors>(unit);
                if (!b || hit.option >= (int)options.size()) return true;
                const BehaviorOption& o = options[(size_t)hit.option];
                if (!addBehavior(b->rules, o.rule)) {
                    refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
                    return true;
                }
                // Patrol Spawn walks about where it was given, for a unit that was not placed by a spawn
                if (o.rule.kind == BehaviorKind::PatrolSpawn && !world.has<SpawnHome>(unit)) {
                    if (const Transform* t = world.get<Transform>(unit)) world.add(unit, SpawnHome{ t->x, t->z, false });
                }
                b->pending = true;
                b->wait = 0.0f;   // an automated behavior looks for its step at once
                showNotice(langf(o.have ? "MISC_BEHAVIOR_SWITCHED_ON" : "MISC_BEHAVIOR_ADDED", { behaviorName(o.rule) }));
                playSfx(Sfx::UiClick);
                return true;
            }
            return false;
        };
        // Whether the mouse is over the bottom unit panel
        auto unitPanelHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).panel.contains(mx, my);
        };
        // Whether a point is over the bottom unit panel or the inventory sidebar. Those belong to the
        // interface: clicks and wheel turns there never reach the world behind them.
        // The buttons of the right-click menu for the item it was opened on: Use for a consumable with an
        // effect, then Drop. Empty when the menu is closed or its item is gone.
        auto menuLabels = [&]() {
            std::vector<std::string> labels;
            const Inventory* inv = world.get<Inventory>(itemMenu.unit);
            if (!itemMenu.open || !inv || itemMenu.slot < 0 || itemMenu.slot >= INVENTORY_SLOTS) return labels;
            const ItemDef* def = findItem(inv->slots[itemMenu.slot].item);
            if (!def) return labels;
            if (def->typeId == "consumable" && !def->onUse.empty()) labels.push_back(lang("UILABEL_USE"));
            labels.push_back(lang("UILABEL_DROP"));
            return labels;
        };
        // Uses the stack in a slot of a unit, and says what happened
        auto useSlot = [&](Entity unit, int slot) {
            const Inventory* inv = world.get<Inventory>(unit);
            if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return;
            const ItemDef* def = findItem(inv->slots[slot].item);
            const std::string itemName = def ? itemDisplayName(*def) : std::string(lang("MISC_THAT_ITEM"));
            switch (useStackInSlot(world, unit, slot)) {
            case UseResult::Used:
                showNotice(langf("MISC_USED", { itemName }));
                break;
            case UseResult::NotUsable:
                refuse(itemName + " can't be used");
                break;
            case UseResult::Empty:
                break;
            }
        };
        // The game log (the Information Panel), stopping short of the unit panel. Its offset is kept in range.
        auto gameLogPanel = [&](int W, int H) {
            return renderer.gameLogLayout(W, H, infoTab, logFilterOpen, logOffset,
                                            (int)infoLines(gameLog, infoTab, logFilter).size(), combatCollapsed, logPlace);
        };
        // The talk: each line said goes into the talk's lines and the Message tab. The player's side speaks as the
        // first player unit that is selected, or else the first one there is.
        auto sayAs = [&](Entity who, const std::string& text, bool player) {
            SpokenLine ln;
            ln.text = text;
            ln.player = player;
            const bool known = who != NULL_ENTITY && world.has<Unit>(who);
            ln.speaker = known ? nameOf(world, who) : std::string(player ? lang("MISC_YOU") : lang("UILABEL_UNNAMED_UNIT"));
            const UnitSprite* us = known ? world.get<UnitSprite>(who) : nullptr;
            ln.sprite = us ? us->id : 1;
            dialogueConvo.push_back(ln);
            gameLog.say(ln.speaker + ": " + text);
        };
        // Ends the talk and the trade with it
        auto endTalk = [&]() {
            talkCooldown = TALK_COOLDOWN;
            tradeOpen = false;
            freeTrade = false;
            tradeNpc.clear();
            tradePlayer.clear();
            tradeNpcDrag = -1;
            quantity.open = false;
            dialogueUnit = NULL_ENTITY;
            dialogueConvo.clear();
        };
        // An item put in the trade from a stack: a single item goes straight in, a stack asks how many first
        auto requestTrade = [&](bool npcSide, Entity owner, int slot) {
            const Inventory* inv = world.get<Inventory>(owner);
            if (!inv || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
            std::vector<TradeGhost>& side = npcSide ? tradeNpc : tradePlayer;
            const int item = inv->slots[slot].item;
            if (inv->slots[slot].count <= 1) {
                if (tradeAddItems(side, *inv, slot, 1) == 0) refuse(lang("MISC_TRADE_ALREADY"));
                return;
            }
            const int avail = tradeAvailable(*inv, side, item);
            if (avail <= 0) {
                refuse(lang("MISC_TRADE_NO_MORE"));
                return;
            }
            const ItemDef* def = findItem(item);
            quantity = QuantityPrompt{ true, npcSide, owner, slot, item, def ? itemDisplayName(*def) : std::string(lang("MISC_ITEM_FALLBACK")), 1, avail };
        };
        auto confirmQuantity = [&]() {
            if (quantity.toStorage) {
                std::string why;
                if (storageProp != NULL_ENTITY && quantity.owner == storageUnit &&
                    storeCountInStorage(world, quantity.slot, quantity.chosen, why)) {
                    queueSound(Sfx::ItemMove);
                } else if (!why.empty()) {
                    refuse(why);
                }
                quantity.open = false;
                return;
            }
            const Inventory* inv = world.get<Inventory>(quantity.owner);
            std::vector<TradeGhost>& side = quantity.npcSide ? tradeNpc : tradePlayer;
            const int added = inv ? tradeAddItems(side, *inv, quantity.slot, quantity.chosen) : 0;
            if (added == 0) refuse(lang("MISC_TRADE_FULL"));
            quantity.open = false;
        };
        // A click on the how-many prompt, for a trade or for a storage: the -100 to +100 steps, Single and Max set the
        // number, Confirm moves that many, Cancel closes it
        auto quantityClick = [&](float qx, float qy) {
            const QuantityLayout ql = layoutQuantity(winW, winH);
            const int cap = std::max(1, quantity.available);
            auto nudge = [&](int by) { quantity.chosen = std::clamp(quantity.chosen + by, 1, cap); };
            if (ql.minus100.contains(qx, qy)) nudge(-100);
            else if (ql.minus10.contains(qx, qy)) nudge(-10);
            else if (ql.minus1.contains(qx, qy)) nudge(-1);
            else if (ql.plus1.contains(qx, qy)) nudge(1);
            else if (ql.plus10.contains(qx, qy)) nudge(10);
            else if (ql.plus100.contains(qx, qy)) nudge(100);
            else if (ql.single.contains(qx, qy)) quantity.chosen = 1;
            else if (ql.max.contains(qx, qy)) quantity.chosen = cap;
            else if (ql.confirm.contains(qx, qy)) confirmQuantity();
            else if (ql.cancel.contains(qx, qy)) quantity.open = false;
        };
        // An item from the storage unit's inventory going into the storage: a single item goes straight in, a stack
        // asks how many first, as a trade does. Max is what the unit carries of the item, or what the storage has room
        // for if that is less.
        auto requestStore = [&](int slot) {
            const Inventory* inv = world.get<Inventory>(storageUnit);
            const Storage* st = world.get<Storage>(storageProp);
            if (!inv || !st || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
            const int item = inv->slots[slot].item;
            std::string why;
            if (inv->slots[slot].count <= 1) {
                if (storeCountInStorage(world, slot, 1, why)) queueSound(Sfx::ItemMove);
                else if (!why.empty()) refuse(why);
                return;
            }
            if (const std::string no = refusalFor(world, item); !no.empty()) {
                refuse(no);
                return;
            }
            const int cap = std::min(carriedCount(*inv, item), slotsRoomFor(st->slots.data(), STORAGE_SLOTS, item));
            if (cap <= 0) {
                refuse(lang("MISC_STORAGE_FULL"));
                return;
            }
            const ItemDef* def = findItem(item);
            quantity = QuantityPrompt{ true, false, storageUnit, slot, item,
                                       def ? itemDisplayName(*def) : std::string(lang("MISC_ITEM_FALLBACK")), 1, cap, true };
        };
        // A trade begins: the Unit Panel opens (if folded) on the Inventory tab, gear and inventory, of your unit doing the
        // trading, at the top of its list, so it is ready before the game pauses for the trade
        auto showTraderInventory = [&]() {
            if (!unitOpen) {
                unitPlace = settlePanel(PanelId::Unit, unitPlace, winW, winH, true);
                unitOpen = true;
            }
            unitTab = UnitTab::Inventory;
            sidebarScroll = 0.0f;
        };
        auto startTalk = [&](Entity npc, Entity talker, bool shop) {
            if (world.has<Mob>(npc) || world.has<Mob>(talker)) return;   // mobs have no dialogue (mobs.h)
            // Only the talker starts the dialogue. The other selected units still walk to the NPC's spot.
            std::vector<Entity> others;
            for (Entity u : selectedUnits(world)) {
                if (u != talker) others.push_back(u);
            }
            clearTalkOrders(world);
            if (const Transform* nt = world.get<Transform>(npc); nt && !others.empty()) {
                issueMoveOrder(world, terrain, others, Vec2{ nt->x, nt->z });
            }
            dialogueUnit = npc;
            dialogueConvo.clear();
            dialoguePlayer = talker;
            playSfx(Sfx::TalkOpen);
            const Dialogue* dg = world.get<Dialogue>(npc);
            sayAs(npc, dg ? dg->text : std::string(lang("NPC_GREETING")), false);
            // Shop: the trade option is picked at once, as if "Show me your wares" were clicked
            if (shop && dg) {
                for (const DialogueOption& o : dg->options) {
                    if (!o.opensTrade) continue;
                    sayAs(talker, o.prompt, true);
                    sayAs(npc, o.reply, false);
                    tradeOpen = true;
                    showTraderInventory();
                    freeTrade = false;
                    tradeNpc.clear();
                    tradePlayer.clear();
                    tradeNpcDrag = -1;
                    break;
                }
            }
        };
        // Picking an option: the player says its prompt, and the unit answers with its reply
        // Starts a talk, unless one ended too recently. Then no dialogue starts, but the units still walk to the NPC.
        auto tryStartTalk = [&](Entity npc, Entity talker, std::vector<Entity> units, bool shop = false) {
            if (talkCooldown <= 0.0f) {
                startTalk(npc, talker, shop);
                return;
            }
            for (Entity u : world.view<TalkOrder>()) units.push_back(u);
            std::sort(units.begin(), units.end());
            units.erase(std::unique(units.begin(), units.end()), units.end());
            const Transform* nt = world.get<Transform>(npc);
            if (nt && !units.empty()) issueMoveOrder(world, terrain, units, Vec2{ nt->x, nt->z });
        };
        // A free trade between two of your own units: the trade screen, with the other unit's items on the left
        auto startFreeTrade = [&](Entity other, Entity unit) {
            if (world.has<Mob>(other) || world.has<Mob>(unit)) return;   // mobs don't trade (mobs.h)
            clearTalkOrders(world);
            closeStorage();
            contextMenu.open = false;
            itemMenu.open = false;
            quantity.open = false;
            dialogueUnit = other;
            dialoguePlayer = unit;
            dialogueConvo.clear();
            tradeOpen = true;
            showTraderInventory();
            freeTrade = true;
            tradeNpc.clear();
            tradePlayer.clear();
            tradeNpcDrag = -1;
        };
        // Tells the player when a unit given an order can't follow it because it is locked in a cage. True if one is.
        auto noteLockedIn = [&](const std::vector<Entity>& units) {
            // Unconscious ones can't do anything at all; say so when they are all there is
            bool anyAwake = false;
            for (Entity u : units) anyAwake = anyAwake || !world.has<Unconscious>(u);
            if (!units.empty() && !anyAwake) {
                refuse(langf("MISC_IS_UNCONSCIOUS", { nameOf(world, units[0]) }));
                return true;
            }
            for (Entity u : units) {
                if (!lockedIn(world, u)) continue;
                refuse(langf("MISC_LOCKED_IN", { nameOf(world, u), propNameOf(world, occupiedProp(world, u)) }));
                return true;
            }
            return false;
        };
        // ---- The right-click menu ----
        // What is under the pointer: a unit (one that is not selected, if there is one), or else the nearest prop
        // that can be used
        auto menuTargetAt = [&](float mx, float my) {
            // What the pointer is on is in front of the ground it is over: a unit or prop farther from the eye than
            // that ground (behind a hill, past a ridge) is hidden by it, so it is passed over
            float ex = 0.0f, ey = 0.0f, ez = 0.0f;
            cam.eye(ex, ey, ez);
            float groundD2 = 1e30f;
            if (auto g = pickGround(cam, terrain, mx, my, winW, winH)) {
                const float gy = std::max(terrain.heightAt(g->first, g->second), 0.0f);
                groundD2 = (g->first - ex) * (g->first - ex) + (gy - ey) * (gy - ey) + (g->second - ez) * (g->second - ez);
            }
            auto hidden = [&](Entity e) {
                const Transform* tr = world.get<Transform>(e);
                if (!tr) return false;
                const float d = std::sqrt((tr->x - ex) * (tr->x - ex) + (tr->y - ey) * (tr->y - ey) + (tr->z - ez) * (tr->z - ez));
                return d > std::sqrt(groundD2) + HIDDEN_BEHIND_GROUND;
            };
            std::vector<Entity> under = renderer.unitsAtPixel(world, cam, mx, my, winW, winH);
            under.erase(std::remove_if(under.begin(), under.end(), hidden), under.end());
            // A unit locked in a cage stands for the cage: the menu offers to unlock it
            for (Entity u : under) {
                if (lockedIn(world, u)) return occupiedProp(world, u);
            }
            for (Entity u : under) {
                if (!world.has<Selected>(u)) return u;
            }
            if (!under.empty()) return under[0];
            Entity prop = NULL_ENTITY;
            float best = 0.0f;
            for (Entity p : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
                if (propInfo(world.get<Prop>(p)->kind).use == PropUse::None || hidden(p)) continue;
                const Transform* pt = world.get<Transform>(p);
                const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
                if (prop == NULL_ENTITY || d2 < best) {
                    prop = p;
                    best = d2;
                }
            }
            return prop;
        };
        // Your selected units that act on a target, nearest first. The target itself never acts on itself.
        // A unit locked in a cage acts only on that cage.
        auto menuActors = [&](Entity target) {
            std::vector<Entity> units;
            for (Entity u : selectedUnits(world)) {
                if (world.has<Unconscious>(u)) continue;   // out cold: it does nothing
                if (world.has<Carried>(u) && !world.has<PlayingDead>(u)) continue;   // carried: it does nothing
                // A mob works no props: of a prop, it can only lie or sit on one (a bed to rest in)
                if (world.has<Mob>(u) && world.has<Prop>(target) && propInfo(world.get<Prop>(target)->kind).use != PropUse::Occupy) continue;
                if (u != target && (!lockedIn(world, u) || occupiedProp(world, u) == target)) units.push_back(u);
            }
            std::sort(units.begin(), units.end(), [&](Entity a, Entity b) {
                return gapBetween(world, a, target) < gapBetween(world, b, target);
            });
            return units;
        };
        // The options for a target, in the order the menu lists them
        // Whether an NPC can be talked to, and whether its talk has a trade (Show me your wares)
        auto talkable = [&](Entity u) {
            return world.has<Unit>(u) && world.has<Dialogue>(u) && !world.has<Mob>(u) && !world.has<Commandable>(u) && !world.has<Hostile>(u) &&
                   !world.has<Unconscious>(u) && !world.has<Carried>(u);
        };
        auto sells = [&](Entity u) {
            const Dialogue* dg = world.get<Dialogue>(u);
            if (!dg || !talkable(u)) return false;
            for (const DialogueOption& o : dg->options) {
                if (o.opensTrade) return true;
            }
            return false;
        };
        // The selected unit that could put a unit down: the one carrying it, or the unit itself if it is the carrier.
        // NULL_ENTITY when neither is one of your selected units.
        auto putDownCarrier = [&](Entity target) {
            if (world.has<Carrying>(target) && world.has<Selected>(target) && !cannotAct(world, target)) return target;
            if (const Carried* c = world.get<Carried>(target)) {
                if (world.has<Selected>(c->by) && !cannotAct(world, c->by)) return c->by;
            }
            return NULL_ENTITY;
        };
        auto menuActionsFor = [&](Entity target) {
            std::vector<MenuAction> out;
            // A selected unit carrying someone, or the unit it carries: Put down comes first
            if (world.has<Unit>(target) && putDownCarrier(target) != NULL_ENTITY) out.push_back(MenuAction::PutDown);
            if (world.has<Unconscious>(target)) {
                // Out cold or dead: carried off (unless someone has it already) and looted
                if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
                out.push_back(MenuAction::Inventory);
                return out;
            }
            if (world.has<Unit>(target)) {
                if (world.has<Commandable>(target)) {
                    // One of your squad: trade, follow, defend, and pick it up (unless it carries or is carried). A
                    // mob is not traded with: its inventory is shut while it is awake.
                    if (!world.has<Mob>(target)) out.push_back(MenuAction::Trade);
                    out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
                    if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
                } else if (world.has<Hostile>(target)) {
                    out.push_back(MenuAction::Attack);   // a hostile unit is only fought, never followed
                } else if (world.has<Mob>(target) && world.has<Faction>(target) && isMobFaction(world.get<Faction>(target)->id)) {
                    out.push_back(MenuAction::AttackUnprovoked);   // a wild mob is only hunted, never followed or defended
                } else {
                    if (talkable(target)) out.push_back(MenuAction::Talk);
                    if (sells(target)) out.push_back(MenuAction::Shop);
                    out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend, MenuAction::AttackUnprovoked });
                }
            } else if (const Prop* p = world.get<Prop>(target)) {
                const PropUse use = propInfo(p->kind).use;
                // A prop locked to all the selected units offers only Unlock, until it is open. One of their own
                // faction's, or inside their faction's claim, is open to them.
                if (const Lock* lock = world.get<Lock>(target); lock && lock->locked) {
                    const std::vector<Entity> sel = selectedUnits(world);
                    bool shut = sel.empty() ? lockShutFor(world, target, NULL_ENTITY) : true;
                    for (Entity u : sel) shut = shut && lockShutFor(world, target, u);
                    if (shut) return std::vector<MenuAction>{ MenuAction::Unlock };
                }
                // An occupiable prop (a bed, a chair, a cage): Occupy it, or, while a selected unit carries someone,
                // Place them on it in its stead, so a carrier is never sent to occupy one
                bool carrying = false;
                for (Entity u : selectedUnits(world)) carrying = carrying || world.has<Carrying>(u);
                if (hasStorage(propInfo(p->kind))) out.push_back(MenuAction::Inventory);
                if (use == PropUse::Occupy) out.push_back(carrying ? MenuAction::Place : MenuAction::Occupy);
                if (isWorkProp(use)) out.push_back(MenuAction::Use);
                // The automated behaviors: Auto-Harvest of a resource prop's kind, Auto-Haul to a prop that keeps items
                if (yieldsItems(p->kind)) out.push_back(MenuAction::AutoHarvest);   // not the Debris, which gives nothing
                if (world.has<Storage>(target)) out.push_back(MenuAction::AutoHaul);
                // A lockable prop: Lock it when it is open (it stays open until then), or, when the lock is the
                // selected units' own, Unlock it with the key
                if (const Lock* lock = world.get<Lock>(target)) out.push_back(lock->locked ? MenuAction::Unlock : MenuAction::Lock);
            }
            return out;
        };
        auto contextLabels = [&]() {
            std::vector<std::string> labels;
            for (MenuAction a : contextMenu.actions) {
                switch (a) {
                case MenuAction::Talk: labels.push_back(lang("UILABEL_TALK")); break;
                case MenuAction::Shop: labels.push_back(lang("UILABEL_SHOP")); break;
                case MenuAction::Trade: labels.push_back(lang("UILABEL_TRADE")); break;
                case MenuAction::Follow: labels.push_back(lang("UILABEL_FOLLOW")); break;
                case MenuAction::Defend: labels.push_back(lang("UILABEL_DEFEND")); break;
                case MenuAction::Attack: labels.push_back(lang("UILABEL_ATTACK")); break;
                case MenuAction::AttackUnprovoked: labels.push_back(lang("UILABEL_ATTACK")); break;   // it turns them hostile
                case MenuAction::Inventory: labels.push_back(lang("UILABEL_INVENTORY")); break;
                case MenuAction::Use: {
                    // Debris is cleared, not used
                    const Prop* tp = world.get<Prop>(contextMenu.target);
                    labels.push_back(lang(tp && tp->kind == PropKind::Debris ? "UILABEL_CLEAR" : "UILABEL_USE"));
                    break;
                }
                case MenuAction::Occupy: labels.push_back(lang("UILABEL_OCCUPY")); break;
                case MenuAction::Place: labels.push_back(lang("UILABEL_PLACE")); break;
                case MenuAction::Unlock: labels.push_back(lang("UILABEL_UNLOCK")); break;
                case MenuAction::Lock: labels.push_back(lang("UILABEL_LOCK")); break;
                case MenuAction::PickUp: labels.push_back(lang("UILABEL_PICK_UP")); break;
                case MenuAction::PutDown: labels.push_back(lang("UILABEL_PUT_DOWN")); break;
                case MenuAction::AutoHarvest: labels.push_back(lang("UILABEL_AUTO_HARVEST")); break;
                case MenuAction::AutoHaul: labels.push_back(lang("UILABEL_AUTO_HAUL")); break;
                case MenuAction::Disband: labels.push_back(lang("UILABEL_DISBAND")); break;
                }
            }
            return labels;
        };
        auto contextMenuLayout = [&]() {
            return renderer.itemMenuLayout(contextMenu.x, contextMenu.y, (int)contextMenu.actions.size(), winW, winH);
        };
        // Opens the menu for what is under the pointer. False if nothing there can be acted on, so the right-click is
        // a move order instead.
        // squadPanel: opened on the unit's cell in the Squad Panel, which adds Disband for a unit of yours
        auto openContextMenuOn = [&](Entity target, float mx, float my, bool squadPanel = false) {
            if (target == NULL_ENTITY) return false;
            std::vector<MenuAction> actions = menuActionsFor(target);
            const bool disband = squadPanel && world.has<Commandable>(target);
            if (disband) actions.push_back(MenuAction::Disband);
            if (actions.empty()) return false;
            if (menuActors(target).empty()) {
                // Put down and Disband need no other unit
                if (disband) {
                    std::vector<MenuAction> own;
                    if (actions[0] == MenuAction::PutDown) own.push_back(MenuAction::PutDown);
                    own.push_back(MenuAction::Disband);
                    contextMenu = ContextMenu{ true, mx, my, target, own };
                    playSfx(Sfx::UiMenuOpen);
                    itemMenu.open = false;
                    return true;
                }
                // Only Put down needs no other unit: the carrier does it
                if (actions[0] == MenuAction::PutDown) {
                    contextMenu = ContextMenu{ true, mx, my, target, { MenuAction::PutDown } };
                    playSfx(Sfx::UiMenuOpen);
                    itemMenu.open = false;
                    return true;
                }
                // Your lone selected unit clicked on itself: a move order, as on the ground under it
                if (world.has<Selected>(target) && world.has<Commandable>(target)) return false;
                if (!noteLockedIn(selectedUnits(world))) refuse(lang("MISC_SELECT_UNIT_FIRST"));
                return true;
            }
            // A hostile unit, or a wild mob, offers only Attack, so the right click is the attack order itself, with no
            // menu. A wild mob that was not hostile turns on the attackers, as any unit attacked unprovoked does.
            if (actions.size() == 1 && (actions[0] == MenuAction::Attack || actions[0] == MenuAction::AttackUnprovoked)) {
                if (actions[0] == MenuAction::AttackUnprovoked) makeHostile(world, target);
                issueAttackOrder(world, menuActors(target), target);
                if (const Transform* tt = world.get<Transform>(target)) {
                    actionX = tt->x;
                    actionZ = tt->z;
                    actionAlpha = 1.0f;
                }
                contextMenu.open = false;
                itemMenu.open = false;
                playSfx(Sfx::UiOrder);
                return true;
            }
            contextMenu = ContextMenu{ true, mx, my, target, actions };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        };
        auto openContextMenu = [&](float mx, float my) { return openContextMenuOn(menuTargetAt(mx, my), mx, my); };
        // Does the picked option with the selected units
        auto pickMenuAction = [&](MenuAction a) {
            const Entity target = contextMenu.target;
            contextMenu.open = false;
            if (a == MenuAction::Disband) {
                // The popout asks first: Disband has to be held down to confirm
                if (world.has<Commandable>(target)) {
                    disbandTarget = target;
                    disbandHeldAt = 0;
                    itemMenu.open = false;
                    playSfx(Sfx::UiMenuOpen);
                }
                return;
            }
            if (a == MenuAction::PutDown) {
                // Put down at once, where the carrier stands
                const Entity carrier = putDownCarrier(target);
                if (carrier != NULL_ENTITY) putDown(world, carrier);
                return;
            }
            const std::vector<Entity> actors = menuActors(target);
            if (actors.empty()) {
                refuse(lang("MISC_SELECT_UNIT_FIRST"));
                return;
            }
            if (!world.has<Unit>(target) && !world.has<Prop>(target)) return;
            switch (a) {
            case MenuAction::Talk: {
                // A selected unit already in talk range talks now; otherwise they walk up, and the first there talks.
                // A mob never talks.
                Entity talker = NULL_ENTITY;
                for (Entity u : actors) {
                    if (world.has<Mob>(u)) continue;
                    if (gapBetween(world, u, target) <= TALK_RANGE) {
                        talker = u;
                        break;
                    }
                }
                if (talker != NULL_ENTITY) tryStartTalk(target, talker, actors);
                else issueTalkOrder(world, actors, target);
                break;
            }
            case MenuAction::Trade: {
                // The nearest selected unit that is not a mob trades: now if it is close enough, or once it has walked up
                const auto trader = std::find_if(actors.begin(), actors.end(), [&](Entity u) { return !world.has<Mob>(u); });
                if (trader == actors.end()) {
                    refuse(lang("MISC_MOB_INVENTORY_LOCKED"));
                    break;
                }
                if (gapBetween(world, *trader, target) <= TALK_RANGE) startFreeTrade(target, *trader);
                else issueTalkOrder(world, { *trader }, target, true);
                break;
            }
            case MenuAction::Follow:
                issueFollowOrder(world, actors, target);
                break;
            case MenuAction::Defend:
                issueDefendOrder(world, actors, target);
                break;
            case MenuAction::AttackUnprovoked:
                // The unit attacked fights back at once; others of its faction that see the attack join in
                makeHostile(world, target);
                issueAttackOrder(world, actors, target);
                break;
            case MenuAction::Attack:
                issueAttackOrder(world, actors, target);
                break;
            case MenuAction::Inventory:
                if (world.has<Unconscious>(target)) {
                    // Looting: the nearest unit that can goes (never a mob, which takes nothing), and the panel opens
                    // when it gets there (or now, if it is)
                    std::vector<Entity> looters;
                    for (Entity u : actors) {
                        if (!world.has<Mob>(u)) looters.push_back(u);
                    }
                    if (looters.empty()) {
                        refuse(lang("MISC_MOB_CANT_HOLD"));
                        break;
                    }
                    if (gapBetween(world, looters[0], target) <= CARRY_REACH && !cannotAct(world, looters[0])) {
                        closeStorage();
                        lootUnit = target;
                        lootBy = looters[0];
                        playSfx(Sfx::UiMenuOpen);
                    } else if (!issueLootOrder(world, terrain, looters, target)) {
                        refuse(lang("MISC_SELECT_UNIT_FIRST"));
                    }
                    break;
                }
                issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Inventory);
                break;
            case MenuAction::PickUp:
                if (!issueCarryOrder(world, terrain, actors, target)) refuse(lang("MISC_ALREADY_CARRYING"));
                break;
            case MenuAction::Place: {
                // The nearest selected unit that carries someone places them on the prop
                Entity carrier = NULL_ENTITY;
                for (Entity u : actors) {
                    if (world.has<Carrying>(u)) {
                        carrier = u;
                        break;
                    }
                }
                if (carrier != NULL_ENTITY) issuePropOrder(world, terrain, { carrier }, target, PropAction::Place);
                break;
            }
            case MenuAction::Occupy: {
                // The nearest selected unit that carries nobody occupies it
                Entity occupier = NULL_ENTITY;
                for (Entity u : actors) {
                    if (!world.has<Carrying>(u)) {
                        occupier = u;
                        break;
                    }
                }
                if (occupier != NULL_ENTITY) issuePropOrder(world, terrain, { occupier }, target, PropAction::Occupy);
                break;
            }
            case MenuAction::PutDown:
            case MenuAction::Disband:
                break;   // done above
            case MenuAction::AutoHarvest:
            case MenuAction::AutoHaul: {
                // Every selected unit gets the behavior, at the bottom of its list (or switched on, if it has it), and
                // looks for its first step at once. Both work only on props in your faction's claim, so a prop outside
                // one is refused.
                const PropKind kind = world.get<Prop>(target)->kind;
                const Transform* pt = world.get<Transform>(target);
                if (!pt || !claimAt(claimsOf(world), pt->x, pt->z, FACTION_PLAYER)) {
                    refuse(lang("MISC_AUTO_OUTSIDE_CLAIM"));
                    return;
                }
                BehaviorRule add;
                add.kind = a == MenuAction::AutoHarvest ? BehaviorKind::AutoHarvest : BehaviorKind::AutoHaul;
                add.propKind = idx(kind);
                if (a == MenuAction::AutoHaul) add.prop = target;
                int given = 0;
                for (Entity u : actors) {
                    Behaviors* b = world.get<Behaviors>(u);
                    if (!b || !addBehavior(b->rules, add)) continue;
                    b->wait = 0.0f;
                    ++given;
                }
                if (given == 0) {
                    refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
                    return;
                }
                showNotice(langf(a == MenuAction::AutoHarvest ? "MISC_AUTO_HARVEST_SET" : "MISC_AUTO_HAUL_SET",
                                 { std::to_string(given), lang(propInfo(kind).nameKey) }));
                playSfx(Sfx::UiOrder);
                return;
            }
            case MenuAction::Shop: {
                // As Talk, then Show me your wares at once
                Entity talker = NULL_ENTITY;
                for (Entity u : actors) {
                    if (world.has<Mob>(u)) continue;
                    if (gapBetween(world, u, target) <= TALK_RANGE) {
                        talker = u;
                        break;
                    }
                }
                if (talker != NULL_ENTITY) tryStartTalk(target, talker, actors, true);
                else issueTalkOrder(world, actors, target, false, true);
                break;
            }
            case MenuAction::Unlock: {
                // The selected unit best at Lockpicking goes (the nearest, if several are as good), if it is good enough
                const Lock* lock = world.get<Lock>(target);
                if (!lock || !lock->locked) return;
                // A lock that is the selected units' own: the nearest that is not a mob opens it with the key
                Entity keyholder = NULL_ENTITY;
                for (Entity u : actors) {
                    if (!world.has<Mob>(u) && !lockShutFor(world, target, u)) {
                        keyholder = u;
                        break;
                    }
                }
                if (keyholder != NULL_ENTITY) {
                    issuePropOrder(world, terrain, { keyholder }, target, PropAction::Unlock);
                    break;
                }
                Entity best = NULL_ENTITY;
                int bestLevel = 0;
                for (Entity u : actors) {   // nearest first
                    const int lv = levelOf(statsOf(world, u), StatId::Lockpicking);
                    if (lv > bestLevel) {
                        best = u;
                        bestLevel = lv;
                    }
                }
                if (best == NULL_ENTITY || bestLevel < lock->level) {
                    refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock->level) }));
                    return;
                }
                issuePropOrder(world, terrain, { best }, target, PropAction::Unlock);
                break;
            }
            case MenuAction::Lock: {
                // The nearest selected unit that is not a mob goes and locks it
                const auto locker = std::find_if(actors.begin(), actors.end(), [&](Entity u) { return !world.has<Mob>(u); });
                if (locker == actors.end()) {
                    refuse(lang("MISC_SELECT_UNIT_FIRST"));
                    return;
                }
                issuePropOrder(world, terrain, { *locker }, target, PropAction::Lock);
                break;
            }
            case MenuAction::Use: {
                const PropInfo& info = propInfo(world.get<Prop>(target)->kind);
                if (info.use != PropUse::Resource) {
                    issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Use);
                    break;
                }
                // A resource prop takes the nearest selected units, as many as it has room for. Units already on it
                // keep their places; the rest share what is left.
                std::vector<Entity> units = actors;
                int others = 0;
                for (Entity u : world.view<Working>()) {
                    if (world.get<Working>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
                }
                for (Entity u : world.view<PropOrder>()) {
                    if (world.get<PropOrder>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
                }
                const int room = std::max(0, info.workers - others);
                if (room == 0) {
                    refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
                    return;
                }
                if ((int)units.size() > room) {
                    units.resize((size_t)room);
                    refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
                }
                issuePropOrder(world, terrain, units, target, PropAction::Use);
                break;
            }
            }
            if (const Transform* tt = world.get<Transform>(target)) {
                actionX = tt->x;
                actionZ = tt->z;
                actionAlpha = 1.0f;
            }
            playSfx(Sfx::UiClick);
        };
        auto chooseOption = [&](size_t i) {
            const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
            if (!dg || i >= dg->options.size()) return;
            const std::string prompt = dg->options[i].prompt, reply = dg->options[i].reply;
            const bool trade = dg->options[i].opensTrade;
            sayAs(dialoguePlayer, prompt, true);
            sayAs(dialogueUnit, reply, false);
            if (trade) {
                tradeOpen = true;
                showTraderInventory();
                freeTrade = false;
                tradeNpc.clear();
                tradePlayer.clear();
                tradeNpcDrag = -1;
            }
        };
        // The talk box for the lines so far and the options of the unit it is with
        auto dialogueBox = [&](int W, int H) {
            const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
            const int lines = (int)std::min<size_t>(2, dialogueConvo.size());
            return layoutDialogue(W, H, lines, dg ? (int)dg->options.size() : 0);
        };
        // Whether a point is on the talk box, or on the trade's two panels while it is open
        auto onTalkOrTrade = [&](int W, int H, float x, float y) {
            if (dialogueUnit == NULL_ENTITY) return false;
            if (tradeOpen) {
                const TradeLayout tl = layoutTrade(W, H, renderer.pauseBarHeight(false));
                return tl.interaction.contains(x, y) || tl.tradePanel.contains(x, y);
            }
            return dialogueBox(W, H).box.contains(x, y);
        };
        // A click on the Information Panel's tabs, its Filter button or its menu. True if the click was used.
        auto infoPanelClick = [&](int W, int H, float tx, float ty) {
            const GameLogLayout cl = gameLogPanel(W, H);
            if (cl.collapsed) return false;
            if (cl.tabMessage.contains(tx, ty)) {
                if (infoTab != 0) { infoTab = 0; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (cl.tabGame.contains(tx, ty)) {
                if (infoTab != 1) { infoTab = 1; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (cl.tabTime.contains(tx, ty)) {
                if (infoTab != 2) { infoTab = 2; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (infoTab == 1 && cl.filterBtn.contains(tx, ty)) {
                logFilterOpen = !logFilterOpen;
                return true;
            }
            if (cl.filterOpen) {
                for (const GameLogLayout::FilterRow& fr : cl.filterRows) {
                    if (fr.heading || !fr.r.contains(tx, ty)) continue;
                    logFilter.on[(int)fr.kind] = !logFilter.on[(int)fr.kind];
                    return true;
                }
                if (cl.filterMenu.contains(tx, ty)) return true;
                logFilterOpen = false;   // a click elsewhere closes the menu, and still does its own job
            }
            return false;
        };
        // A panel's place, by its PanelId
        auto placeOf = [&](PanelId which) -> PanelPlace& {
            return which == PanelId::Squad ? squadPlace : which == PanelId::Unit ? unitPlace : logPlace;
        };
        // Whether a panel is folded to its title bar
        auto panelFolded = [&](PanelId which) {
            return which == PanelId::Squad ? squadCollapsed : which == PanelId::Unit ? !unitOpen : combatCollapsed;
        };
        // What a panel shows on the screen: its open rectangle, or its title bar where it is folded
        auto shownPanel = [&](PanelId which, int W, int H) -> Rect {
            if (which == PanelId::Squad) return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).panel;
            if (which == PanelId::Unit) return sidebarFor(W, H, sidebarScroll).panel;
            return gameLogPanel(W, H).panel;
        };
        // A panel's title bar, wherever it runs: along the panel's top, or along its bottom
        auto panelBarOf = [&](PanelId which, int W, int H) -> Rect {
            if (which == PanelId::Squad) return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).title;
            if (which == PanelId::Unit) return sidebarFor(W, H, sidebarScroll).title;
            return gameLogPanel(W, H).bar;
        };
        // What a press or the mouse over a point would grab on an unlocked panel: the panel, and a mix of the GRAB_ edges
        // under the mouse (an edge or a corner resizes it), or GRAB_MOVE on its title bar. A folded panel can only be
        // moved. No panel (nullopt) means none. A locked panel grabs nothing.
        auto grabAt = [&](int W, int H, float x, float y) -> std::pair<std::optional<PanelId>, int> {
            const float out = 5.0f, in = 4.0f;   // how far outside and inside an edge still grabs it
            for (PanelId which : { PanelId::Info, PanelId::Unit, PanelId::Squad }) {
                if (panelLocked[idx(which)]) continue;
                const Rect r = shownPanel(which, W, H);
                const bool nearX = x >= r.x - out && x < r.x + r.w + out;
                const bool nearY = y >= r.y - out && y < r.y + r.h + out;
                if (!nearX || !nearY) continue;
                int kind = 0;
                if (!panelFolded(which)) {
                    if (y < r.y + in) kind |= GRAB_TOP;
                    if (y >= r.y + r.h - in) kind |= GRAB_BOTTOM;
                    if (x < r.x + in) kind |= GRAB_LEFT;
                    if (x >= r.x + r.w - in) kind |= GRAB_RIGHT;
                }
                if (kind != 0) return { which, kind };
                if (panelBarOf(which, W, H).contains(x, y)) return { which, GRAB_MOVE };
            }
            return { std::nullopt, 0 };
        };
        // The arrows that show over an edge or a corner, and the hand-move arrows over an unlocked title bar
        auto setGrabCursor = [&](int kind) {
            static SDL_Cursor* cursors[SDL_SYSTEM_CURSOR_COUNT] = {};
            static int shown = -1;
            SDL_SystemCursor want = SDL_SYSTEM_CURSOR_DEFAULT;
            const bool tb = (kind & (GRAB_TOP | GRAB_BOTTOM)) != 0, lr = (kind & (GRAB_LEFT | GRAB_RIGHT)) != 0;
            if (kind == GRAB_MOVE) want = SDL_SYSTEM_CURSOR_MOVE;
            else if (tb && lr) {
                const bool nwse = ((kind & GRAB_TOP) && (kind & GRAB_LEFT)) || ((kind & GRAB_BOTTOM) && (kind & GRAB_RIGHT));
                want = nwse ? SDL_SYSTEM_CURSOR_NWSE_RESIZE : SDL_SYSTEM_CURSOR_NESW_RESIZE;
            } else if (tb) want = SDL_SYSTEM_CURSOR_NS_RESIZE;
            else if (lr) want = SDL_SYSTEM_CURSOR_EW_RESIZE;
            if ((int)want == shown) return;
            shown = (int)want;
            if (want == SDL_SYSTEM_CURSOR_DEFAULT) {
                SDL_SetCursor(SDL_GetDefaultCursor());
                return;
            }
            if (!cursors[want]) cursors[want] = SDL_CreateSystemCursor(want);
            SDL_SetCursor(cursors[want] ? cursors[want] : SDL_GetDefaultCursor());
        };
        // Moves or resizes the grabbed panel to follow the mouse, from where the press began. Each edge stops at the
        // panel's smallest size and at the window's edge.
        auto applyGrab = [&](int W, int H, float x, float y) {
            const float dx = x - grabMouseX, dy = y - grabMouseY;
            float mw = 0.0f, mh = 0.0f;
            panelMinSize(*grabWhich, H, mw, mh);
            mw = std::min(mw, (float)W);
            mh = std::min(mh, (float)H);
            float left = grabX, top = grabY, right = grabX + grabW, bottom = grabY + grabH;
            if (grabKind == GRAB_MOVE) {
                // The whole panel moves; only the bar it shows is kept on the screen (below)
                left = grabX + dx;
                top = grabY + dy;
                right = left + grabW;
                bottom = top + grabH;
            } else {
                if (grabKind & GRAB_LEFT) left = std::clamp(grabX + dx, 0.0f, right - mw);
                if (grabKind & GRAB_RIGHT) right = std::clamp(grabX + grabW + dx, left + mw, (float)W);
                if (grabKind & GRAB_TOP) top = std::clamp(grabY + dy, 0.0f, bottom - mh);
                if (grabKind & GRAB_BOTTOM) bottom = std::clamp(grabY + grabH + dy, top + mh, std::max((float)H, grabY + grabH));
            }
            PanelPlace& p = placeOf(*grabWhich);
            p = placeForRect(*grabWhich, Rect{ left, top, right - left, bottom - top }, W, H, p.barBottom);
            p = placeForRect(*grabWhich, panelRect(*grabWhich, p, W, H), W, H, p.barBottom);   // its bar on the screen
        };
        // Lets go of the grab. The panel stays as it was left, and its bar goes to the side its new place calls for.
        auto endGrab = [&](int W, int H) {
            if (grabWhich) placeOf(*grabWhich) = settlePanel(*grabWhich, placeOf(*grabWhich), W, H, panelFolded(*grabWhich));
            grabWhich.reset();
            grabKind = 0;
        };
        // Folds or opens a panel. Its bar stays where it is: folding takes the rest of the panel away, and opening grows
        // it back away from the bar, toward the middle of the window.
        auto setFolded = [&](PanelId which, bool fold, int W, int H) {
            PanelPlace& p = placeOf(which);
            p = settlePanel(which, p, W, H, !fold);   // the bar on its side before it changes
            if (which == PanelId::Squad) squadCollapsed = fold;
            else if (which == PanelId::Unit) unitOpen = !fold;
            else combatCollapsed = fold;
        };
        // The lock on a panel's bar was pressed: unlocking lets it be moved and resized; locking it again keeps its
        // place and size in the settings
        auto toggleLock = [&](PanelId which) {
            panelLocked[idx(which)] = !panelLocked[idx(which)];
            if (panelLocked[idx(which)]) {
                const PanelPlace& p = placeOf(which);
                settings.panels[(size_t)idx(which)] = { p.dx, p.dy, p.w, p.h };
                saveSettingsFile();
            }
            playSfx(Sfx::UiClick);
        };
        // The lock buttons, by PanelId
        auto lockRectOf = [&](PanelId which, int W, int H) -> Rect {
            if (which == PanelId::Squad) return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).lock;
            if (which == PanelId::Unit) return sidebarFor(W, H, sidebarScroll).lock;
            return gameLogPanel(W, H).lock;
        };
        // A panel's lock on its title bar, drawn just after the panel. An unlocked panel also gets a green edge, to show
        // it can be moved and resized.
        auto drawPanelLock = [&](PanelId which) {
            const int W = winW, H = winH;
            if (!panelLocked[idx(which)]) {
                const Rect r = shownPanel(which, W, H);
                const float e = std::max(1.0f, 2.0f * H / 720.0f);
                const RGBA g = theme::alpha(theme::GREEN, 0.9f);
                renderer.drawRect(Rect{ r.x - e, r.y - e, r.w + 2.0f * e, e }, g);
                renderer.drawRect(Rect{ r.x - e, r.y + r.h, r.w + 2.0f * e, e }, g);
                renderer.drawRect(Rect{ r.x - e, r.y, e, r.h }, g);
                renderer.drawRect(Rect{ r.x + r.w, r.y, e, r.h }, g);
            }
            renderer.drawLockButton(lockRectOf(which, W, H), panelLocked[idx(which)]);
        };
        // Whether the mouse is over the game log
        auto gameLogHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return gameLogPanel(W, H).panel.contains(mx, my);
        };
        // The world map window, 800 by 600 in the middle of the screen
        auto worldMapFor = [&](int W, int H) {
            return layoutWorldMap(W, H, mapDragX, mapDragY);
        };
        // The ground point under the mouse on the world map, while it is open
        auto mapGroundAt = [&](int W, int H, float x, float y, float& gx, float& gz) {
            if (!mapOpen) return false;
            return mapGround(worldMapFor(W, H).map, mapCx, mapCz, mapSpan, (float)terrain.size(), x, y, gx, gz);
        };
        // The green X on the world map: where the last move order went, and how long it has left to show
        auto markMove = [&](float x, float z) {
            moveMarkX = x;
            moveMarkZ = z;
            moveMarkTime = MOVE_MARK_SECONDS;
        };
        // A move order to a ground point for the selected units, as a right click on the ground gives
        // The right button pressed on a point: the selected units walk straight at it while it is held, with the
        // crystal showing it. The route is planned when the button is let go.
        auto beginRightHold = [&](float x, float z) {
            const std::vector<Entity> units = selectedUnits(world);
            if (units.empty()) return;
            noteLockedIn(units);   // a unit locked in a cage stays put
            rightHold = true;
            holdX = x;
            holdZ = z;
            actionX = x;
            actionZ = z;
            actionAlpha = 1.0f;
            markMove(x, z);
            steerUnits(world, units, Vec2{ x, z });
            playSfx(Sfx::UiOrder);
        };
        auto moveSelectedTo = [&](float x, float z) {
            const std::vector<Entity> units = selectedUnits(world);
            if (units.empty()) return;
            issueMoveOrder(world, terrain, units, Vec2{ x, z });
            actionX = x;
            actionZ = z;
            actionAlpha = 1.0f;
            markMove(x, z);
        };
        auto overPanels = [&](float x, float y, int W, int H) {
            const std::vector<std::string> labels = menuLabels();
            return overGamePanels(layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll),
                                  sidebarFor(W, H, sidebarScroll), true, x, y)
                || gameLogPanel(W, H).panel.contains(x, y)
                || (lootUnit != NULL_ENTITY && layoutLoot(W, H).panel.contains(x, y))
                || (mapOpen && worldMapFor(W, H).window.contains(x, y))
                || (!labels.empty() && renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), W, H).box.contains(x, y));
        };
        // The point a held right button is over: the world map's ground where the mouse is on it, or else the ground
        // under the mouse. Over the panels, or over the world map window away from its ground, there is none.
        auto holdTarget = [&](int W, int H, float x, float y, float& gx, float& gz) {
            if (mapGroundAt(W, H, x, y, gx, gz)) return true;
            if (overPanels(x, y, W, H)) return false;
            auto hit = pickGround(cam, terrain, x, y, W, H);
            if (!hit) return false;
            gx = hit->first;
            gz = hit->second;
            return true;
        };
        auto pointerOverPanels = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return overPanels(mx, my, W, H);
        };

        // The menus measure and draw their text in the larger menu fonts, the game in its own: set before the input,
        // so a click is matched against the same layout that was drawn
        renderer.setMenuText(screen != Screen::Playing);

        // ---- Input ----
        while (SDL_PollEvent(&e)) {
            // F12 takes a screenshot from any screen, whatever menu or text box has the keys
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12 && !e.key.repeat) {
                screenshotPending = true;
                continue;
            }
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_TEXT_INPUT:
                // The # prompt takes digits only, as many as the top of the behavior's range has
                if (percentPrompt.open) {
                    const size_t most = std::to_string(percentRange(percentPrompt.kind).hi).size();
                    for (const char* c = e.text.text; *c; ++c) {
                        if (*c < '0' || *c > '9') continue;
                        if (percentPrompt.text == "0") percentPrompt.text.clear();   // no leading zero
                        if (percentPrompt.text.size() < most) percentPrompt.text += *c;
                    }
                    percentPrompt.error.clear();
                    break;
                }
                if (screen == Screen::PartySetup) {
                    // Typed text goes into the name being edited, up to the limit in characters. Any text an input
                    // method was composing is now committed, so it leaves the name box.
                    partyUi.composition.clear();
                    if (partyUi.editingName) appendUtf8Limited(pickedMember(party, partyUi).name, e.text.text, PARTY_NAME_CHARS);
                    break;
                }
                if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
                    saveName += e.text.text;
                }
                break;

            case SDL_EVENT_TEXT_EDITING:
                // An input method is composing text (for example Chinese or Japanese): it shows in the name box
                if (screen == Screen::PartySetup && partyUi.editingName) partyUi.composition = e.edit.text;
                break;

            case SDL_EVENT_KEY_DOWN:
                // Escape acts once per press: the repeats sent while it is held down are ignored, so holding it does
                // not back out of several menus at once
                if (e.key.key == SDLK_ESCAPE && e.key.repeat) break;
                // The # prompt takes every key: Enter sets the percent, Escape cancels, Backspace takes off a digit.
                // The game's keys (Space, M, the camera's) wait until it is answered.
                if (percentPrompt.open) {
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        answerPercentPrompt(true);
                    } else if (e.key.key == SDLK_ESCAPE) {
                        answerPercentPrompt(false);
                    } else if (e.key.key == SDLK_BACKSPACE && !percentPrompt.text.empty()) {
                        percentPrompt.text.pop_back();
                        percentPrompt.error.clear();
                    }
                    break;
                }
                if (screen == Screen::PartySetup) {
                    // Escape backs out one step at a time: the dropdown, then the name box, then the map list
                    if (e.key.key == SDLK_ESCAPE) {
                        if (partyUi.dropKind != DropKind::None) {
                            closeDropdown();
                        } else if (partyUi.editingName) {
                            stopNameEdit();
                        } else {
                            screen = backScreen;
                        }
                    } else if (partyUi.editingName) {
                        std::string& name = pickedMember(party, partyUi).name;
                        if (e.key.key == SDLK_BACKSPACE) {
                            popUtf8Char(name);
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            stopNameEdit();
                        }
                    }
                    break;
                }
                if (panelOpen) {
                    if (e.key.key == SDLK_ESCAPE) {
                        // Escape backs out of a question first, then closes the popout
                        if (ask != Ask::None) {
                            ask = Ask::None;
                            pendingOverwrite.reset();
                            pendingLoad.reset();
                            pendingDelete.reset();
                            deleteHolding = false;
                        } else {
                            closePanel();
                        }
                    } else if (panelSaveMode && ask == Ask::None) {
                        if (e.key.key == SDLK_BACKSPACE) {
                            popUtf8Char(saveName);
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            performAction(Action::ConfirmName, 0);
                        }
                    }
                    break;
                }
                // The title and pause menus: up and down (or W and S) light the next item, Enter (or Space on the title)
                // picks it. Items that can't be used are passed over.
                if (screen == Screen::Title || screen == Screen::Pause) {
                    const std::vector<Button> items = layoutButtons(screen, winW, winH, saves);
                    const SDL_Keycode k = e.key.key;
                    const bool up = k == SDLK_UP || k == SDLK_W, down = k == SDLK_DOWN || k == SDLK_S;
                    if ((up || down) && !items.empty()) {
                        const int n = (int)items.size();
                        int i = menuSel;
                        for (int tries = 0; tries < n; ++tries) {
                            i = ((i < 0 ? (down ? -1 : 0) : i) + (down ? 1 : -1) + n) % n;
                            if (items[(size_t)i].enabled) break;
                        }
                        if (items[(size_t)i].enabled && i != menuSel) menuSel = i;   // the hover sound plays as it lights
                        break;
                    }
                    const bool pick = k == SDLK_RETURN || k == SDLK_KP_ENTER || (k == SDLK_SPACE && screen == Screen::Title);
                    if (pick && !e.key.repeat && menuSel >= 0 && menuSel < (int)items.size() && items[(size_t)menuSel].enabled) {
                        performAction(items[(size_t)menuSel].action, items[(size_t)menuSel].param);
                        break;
                    }
                }
                // While a prop is picked to build, Escape puts the pick down, and does nothing else
                if (screen == Screen::Playing && buildPick.any() && e.key.key == SDLK_ESCAPE) {
                    buildPick = {};
                    playSfx(Sfx::UiBack);
                    break;
                }
                if (screen == Screen::Playing && !e.key.repeat && e.key.key == SDLK_M) {
                    mapOpen = !mapOpen;   // M opens and closes the world map
                    break;
                }
                // Space pauses and resumes the game, but not while a talk or a trade is open
                if (screen == Screen::Playing && e.key.key == SDLK_SPACE && !e.key.repeat && dialogueUnit == NULL_ENTITY) {
                    paused = !paused;
                    playSfx(paused ? Sfx::UiPause : Sfx::UiResume);
                }
                if (e.key.key == SDLK_ESCAPE) {
                    // In a talk, Escape backs out a step at a time, as the buttons do: a "how many" prompt is closed,
                    // the trade is Cancelled (back to the talk, or ended if it was a trade between your own units),
                    // and the talk itself is ended as with End Dialogue
                    if (screen == Screen::Playing && dialogueUnit != NULL_ENTITY) {
                        if (quantity.open && tradeOpen && !quantity.toStorage) {
                            quantity.open = false;
                        } else if (tradeOpen && freeTrade) {
                            endTalk();
                        } else if (tradeOpen) {
                            tradeOpen = false;
                            tradeNpc.clear();
                            tradePlayer.clear();
                            tradeNpcDrag = -1;
                        } else {
                            endTalk();
                        }
                        playSfx(Sfx::UiBack);
                        break;
                    }
                    if (screen == Screen::Playing && disbandTarget != NULL_ENTITY) {
                        disbandTarget = NULL_ENTITY;   // Escape is Cancel on the Disband popout
                        disbandHeldAt = 0;
                        playSfx(Sfx::UiBack);
                    } else if (screen == Screen::Playing && optimizeAllOpen) {
                        optimizeAllOpen = false;   // Escape is Cancel on the Optimize All dialog
                        optimizeSliderDrag = false;
                        playSfx(Sfx::UiBack);
                    } else if (screen == Screen::Playing && mapOpen) {
                        mapOpen = false;  // Escape closes the world map first
                    } else if (screen == Screen::Playing && itemMenu.open) {
                        // Escape closes the open menu or popout first, and only then pauses the game
                        itemMenu.open = false;
                    } else if (screen == Screen::Playing) {
                        leftDown = middleDown = false;
                        sel.dragging = false;
                        itemDrag = false;
                        itemMenu.open = false;
                        screen = Screen::Pause;
                        refreshSaves();
                        playSfx(Sfx::UiPause);
                    } else if (screen == Screen::Pause) {
                        screen = Screen::Playing;
                        playSfx(Sfx::UiResume);
                    } else if (screen == Screen::Settings) {
                        // Escape closes an open dropdown first, then leaves the menu
                        if (settingsUi.openRow >= 0) settingsCloseDropdown(settingsUi);
                        else leaveSettings();
                    } else if (screen == Screen::MapSelect || screen == Screen::ScenarioSelect) {
                        performAction(Action::ReturnToTitle, 0);
                    }
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL: {
                // The world map popout takes the wheel over everything under it
                const bool wheelOverMap = mapOpen && screen == Screen::Playing
                                          && worldMapFor(winW, winH).window.contains(e.wheel.mouse_x, e.wheel.mouse_y);
                if (panelOpen) {
                    // Scroll the save list, keeping the offset within range
                    int maxOffset = std::max(0, (int)saves.size() - (int)visibleSaveRows(winH));
                    int next = (int)listOffset - (int)e.wheel.y;
                    listOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::MapSelect) {
                    int maxOffset = std::max(0, (int)maps.size() - (int)visibleSaveRows(winH));
                    int next = (int)mapOffset - (int)e.wheel.y;
                    mapOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::ScenarioSelect) {
                    // The scenario list scrolls a row at a time; the layout keeps it in range
                    scenarioScroll = layoutScenario(winW, winH, (int)ScenarioId::Count,
                                                    scenarioScroll - (int)e.wheel.y).first;
                } else if (screen == Screen::PartySetup) {
                    // Scrolls an open dropdown. The rest of the menu does not scroll.
                    if (partyUi.dropKind != DropKind::None) {
                        const int count = (int)dropdownEntries(party, partyUi).size();
                        partyUi.dropScroll = clampDropScroll(partyUi.dropScroll - (int)e.wheel.y, count);
                    } else if (partyPanels(winW, winH).left.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
                        // The unit list scrolls, a unit at a time
                        const int maxScroll = partyListMaxScroll((int)party.size(), winH);
                        partyUi.listScroll = std::clamp(std::min(partyUi.listScroll, maxScroll) - (int)e.wheel.y, 0, maxScroll);
                    }
                } else if (screen == Screen::Settings) {
                    // Scrolls an open dropdown. Nothing else on the menu scrolls.
                    if (settingsUi.openRow >= 0) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        settingsScrollDropdown(settingsUi, layoutSettings(winW, winH, rows, settingsUi), (int)e.wheel.y);
                    }
                } else if (screen == Screen::Playing && !wheelOverMap && gameLogHovered(winW, winH)) {
                    // Scrolls the game log, three lines a notch. Wheel up goes back in time.
                    const int step = e.wheel.y > 0.0f ? 3 : e.wheel.y < 0.0f ? -3 : 0;
                    logOffset = std::clamp(logOffset + step, 0, gameLogPanel(winW, winH).maxOffset());
                } else if (wheelOverMap) {
                    // Zooms the world map about the point under the mouse: wheel up zooms in, down zooms out
                    const Rect box = worldMapFor(winW, winH).map;
                    const float px = std::clamp(e.wheel.mouse_x, box.x, box.x + box.w);
                    const float py = std::clamp(e.wheel.mouse_y, box.y, box.y + box.h);
                    const float oldK = mapTilesPerPixel(box, mapSpan);
                    const float offX = box.x + box.w * 0.5f - px, offY = box.y + box.h * 0.5f - py;
                    const float gx = mapCx + offX * oldK;   // the ground under the mouse
                    const float gz = mapCz + offY * oldK;
                    mapSpan = e.wheel.y > 0.0f ? mapSpan * 0.8f : mapSpan * 1.25f;
                    float cx = mapCx, cz = mapCz;
                    clampMapView(box, (float)terrain.size(), cx, cz, mapSpan);
                    const float k = mapTilesPerPixel(box, mapSpan);
                    mapCx = gx - offX * k;
                    mapCz = gz - offY * k;
                    clampMapView(box, (float)terrain.size(), mapCx, mapCz, mapSpan);
                } else if (screen == Screen::Playing && unitPanelHovered(winW, winH) && factionTab == FACTION_TAB_REPUTATION) {
                    // The Reputation tab's list scrolls a faction a notch
                    const int step = e.wheel.y > 0.0f ? -1 : e.wheel.y < 0.0f ? 1 : 0;
                    repScroll = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace,
                                                factionTab, (int)reputationFactions().size(), repScroll + step).repFirst;
                } else if (screen == Screen::Playing && unitPanelHovered(winW, winH)) {
                    // Scrolls the unit list very slowly: a row for every SQUAD_WHEEL_NOTCHES_PER_ROW notches of the
                    // wheel. Turning the wheel the other way starts the count again.
                    if (unitPanelWheel * e.wheel.y < 0.0f) unitPanelWheel = 0.0f;
                    unitPanelWheel += e.wheel.y;
                    const int rows = (int)(unitPanelWheel / SQUAD_WHEEL_NOTCHES_PER_ROW);
                    if (rows != 0) {
                        unitPanelWheel -= rows * SQUAD_WHEEL_NOTCHES_PER_ROW;
                        unitPanelRow = layoutUnitPanel(winW, winH, (int)ownUnitList().size(),
                                                       unitPanelRow - rows, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).scrollRow;
                    }
                } else if (screen == Screen::Playing && !onTalkOrTrade(winW, winH, e.wheel.mouse_x, e.wheel.mouse_y)
                           && sidebarFor(winW, winH, sidebarScroll).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
                    // Scrolls the sidebar, 40 pixels a notch. Wheel up goes back toward the top.
                    const float notch = 40.0f * (winH / 720.0f);
                    sidebarScroll = sidebarFor(winW, winH, sidebarScroll - e.wheel.y * notch).scroll;
                } else if (screen == Screen::Playing && !pointerOverPanels(winW, winH)) {
                    // Over the panels the wheel does nothing, so the camera doesn't zoom behind them
                    cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
                    cam.clampState();
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // The # prompt takes every click: Set and Cancel answer it, a click outside it cancels it
                if (percentPrompt.open) {
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const PercentPromptLayout pl = layoutPercentPrompt(winW, winH);
                        if (pl.ok.contains(e.button.x, e.button.y)) answerPercentPrompt(true);
                        else if (pl.cancel.contains(e.button.x, e.button.y) || !pl.box.contains(e.button.x, e.button.y)) answerPercentPrompt(false);
                    }
                    break;
                }
                // While a prop is picked to build, a right click puts the pick down and does nothing else
                if (buildPick.any() && screen == Screen::Playing && e.button.button == SDL_BUTTON_RIGHT) {
                    buildPick = {};
                    playSfx(Sfx::UiBack);
                    break;
                }
                if (panelOpen) {
                    // The popout takes all clicks while it's open
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                                           panelFrom == Screen::Title)) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                if (b.action == Action::DeleteHold) {
                                    // Held, not clicked: the timer deletes once it has been held long enough
                                    deleteHolding = true;
                                    deleteHoldStart = SDL_GetTicks();
                                    deleteHoldRect = b.rect;
                                } else {
                                    performAction(b.action, b.param);
                                }
                                break;
                            }
                        }
                    }
                    break;
                }

                if (screen == Screen::ScenarioSelect) {
                    // A row picks its scenario; Continue rolls the party for it; Back returns to the title
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const float mx = e.button.x, my = e.button.y;
                        const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
                        scenarioScroll = sl.first;
                        for (size_t i = 0; i < sl.rows.size(); ++i) {
                            if (sl.rows[i].contains(mx, my)) {
                                scenarioPick = (ScenarioId)(sl.first + (int)i);
                                playSfx(Sfx::UiClick);
                            }
                        }
                        if (sl.next.contains(mx, my)) {
                            playSfx(Sfx::UiClick);
                            openPartyMenu();
                        } else if (sl.back.contains(mx, my)) {
                            performAction(Action::ReturnToTitle, 0);
                        }
                    }
                    break;
                }

                if (screen == Screen::PartySetup) {
                    // The unit list's scroll bar: the thumb is taken where it was clicked; a click on the track moves
                    // the thumb's middle there, and it can be dragged on from that point
                    const PartyListBar bar = partyListBar(winW, winH, (int)party.size(), partyUi.listScroll);
                    const Rect barGrab{ bar.track.x - 4.0f, bar.track.y, bar.track.w + 8.0f, bar.track.h };
                    if (e.button.button == SDL_BUTTON_LEFT && partyUi.dropKind == DropKind::None && bar.track.h > 0.0f
                        && barGrab.contains(e.button.x, e.button.y)) {
                        partyUi.listGrab = bar.thumb.contains(bar.thumb.x, e.button.y) ? e.button.y - bar.thumb.y : bar.thumb.h * 0.5f;
                        partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.button.y - partyUi.listGrab);
                        partyUi.listDrag = true;
                        break;
                    }
                    // The right button held on the picked unit's model turns it about its vertical axis
                    if (e.button.button == SDL_BUTTON_RIGHT && partyUi.dropKind == DropKind::None && !party.empty()
                        && partyPreviewRect(winW, winH).contains(e.button.x, e.button.y)) {
                        partyUi.previewDrag = true;
                        break;
                    }
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const float mx = e.button.x, my = e.button.y;
                        const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
                        const std::vector<Button> buttons = layoutPartySetup(winW, winH, party, partyUi, drop);
                        // The last enabled button under the pointer is the one on top, so it gets the click
                        const Button* hit = nullptr;
                        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                            if (it->enabled && it->rect.contains(mx, my)) {
                                hit = &*it;
                                break;
                            }
                        }
                        const bool onDropdown = hit && (hit->action == Action::PartyDropPick
                                                        || hit->action == Action::PartyDropScroll
                                                        || hit->action == Action::PartyDropBack);
                        if (partyUi.dropKind != DropKind::None && !onDropdown) {
                            // A click outside an open dropdown only closes it
                            closeDropdown();
                        } else if (hit) {
                            if (partyUi.editingName && hit->action != Action::PartyNameField) stopNameEdit();
                            performAction(hit->action, hit->param);
                        } else {
                            stopNameEdit();
                        }
                    }
                    break;
                }

                if (screen == Screen::Settings) {
                    if (e.button.button == SDL_BUTTON_LEFT) settingsClick(e.button.x, e.button.y, winW, winH);
                    break;
                }

                if (screen != Screen::Playing) {
                    // Menus: a left click runs the enabled button under the cursor
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const std::vector<Button> menuButtons = screen == Screen::MapSelect
                            ? layoutMapPanel(winW, winH, maps, mapOffset)
                            : layoutButtons(screen, winW, winH, saves);
                        for (const Button& b : menuButtons) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                performAction(b.action, b.param);
                                break;
                            }
                        }
                    }
                    break;
                }

                // The Disband popout is modal: only it answers while it is open. Pressing Disband starts the hold;
                // letting go (or moving off it) before DISBAND_HOLD_MS starts it over.
                if (screen == Screen::Playing && disbandTarget != NULL_ENTITY) {
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const DisbandLayout dl = layoutDisband(winW, winH);
                        if (dl.disband.contains(e.button.x, e.button.y)) {
                            disbandHeldAt = std::max<unsigned long long>(1, SDL_GetTicks());
                            playSfx(Sfx::UiClick);
                        } else if (dl.cancel.contains(e.button.x, e.button.y)) {
                            disbandTarget = NULL_ENTITY;
                            disbandHeldAt = 0;
                            playSfx(Sfx::UiBack);
                        }
                    }
                    break;
                }
                // The Optimize All dialog is modal: only it answers while it is open
                if (screen == Screen::Playing && optimizeAllOpen) {
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const OptimizeAllLayout ol = layoutOptimizeAll(winW, winH);
                        const float x = e.button.x, y = e.button.y;
                        const Rect grab{ ol.track.x - 8.0f, ol.track.y - 10.0f, ol.track.w + 16.0f, ol.track.h + 20.0f };
                        if (grab.contains(x, y)) {
                            settings.optimizeAttackShare = levelAtX(ol.track, x);
                            optimizeSliderDrag = true;
                        } else if (ol.toggles[0].contains(x, y)) {
                            settings.optimizeAccessories = !settings.optimizeAccessories;
                            saveSettingsFile();
                            playSfx(Sfx::UiToggle);
                        } else if (ol.toggles[1].contains(x, y)) {
                            settings.optimizeTwoHanded = !settings.optimizeTwoHanded;
                            saveSettingsFile();
                            playSfx(Sfx::UiToggle);
                        } else if (ol.toggles[2].contains(x, y)) {
                            settings.optimizeValueTies = !settings.optimizeValueTies;
                            saveSettingsFile();
                            playSfx(Sfx::UiToggle);
                        } else if (ol.cancel.contains(x, y)) {
                            optimizeAllOpen = false;
                            playSfx(Sfx::UiBack);
                        } else if (ol.confirm.contains(x, y)) {
                            // Each unit of the squad, on its own: one without the room to change stays as it was
                            const std::vector<Entity> squad = ownUnitList();
                            const OptimizeAllResult r = optimizeAllGear(world, squad, gearPrefs());
                            optimizeAllOpen = false;
                            if (r.changed == 0 && r.noRoom == 0) {
                                showNotice(lang("MISC_OPTIMIZE_ALL_SAME"));
                            } else if (r.noRoom == 0) {
                                showNotice(langf("MISC_OPTIMIZE_ALL_DONE", { std::to_string(r.changed) }));
                            } else {
                                showNotice(langf("MISC_OPTIMIZE_ALL_SOME_ROOM", { std::to_string(r.changed), std::to_string(r.noRoom) }));
                            }
                            if (r.changed == 0) playSfx(r.noRoom > 0 ? Sfx::UiError : Sfx::UiClick);   // a change plays its own sound
                        }
                    }
                    break;
                }
                // The world map window is over everything, and a click inside it is its own: the X closes it, and a right
                // click on the map is a move order to that point. Outside the window, clicks go on as if it were shut.
                // The talk box and the trade are drawn over the map, so a click on them is theirs
                if (screen == Screen::Playing && mapOpen && !onTalkOrTrade(winW, winH, e.button.x, e.button.y) &&
                    worldMapFor(winW, winH).window.contains(e.button.x, e.button.y)) {
                    const WorldMapLayout wm = worldMapFor(winW, winH);
                    float mgx = 0.0f, mgz = 0.0f;
                    if (e.button.button == SDL_BUTTON_LEFT && wm.close.contains(e.button.x, e.button.y)) {
                        mapOpen = false;
                        playSfx(Sfx::UiClick);
                    } else if (e.button.button == SDL_BUTTON_LEFT && wm.bar.contains(e.button.x, e.button.y)) {
                        // The title bar moves the window while it is held
                        const float dx = wm.window.x - std::floor((winW - wm.window.w) * 0.5f);
                        const float dy = wm.window.y - std::floor((winH - wm.window.h) * 0.5f);
                        mapDragX = dx;
                        mapDragY = dy;
                        mapDragging = true;
                        mapGrabX = e.button.x - dx;
                        mapGrabY = e.button.y - dy;
                    } else if (e.button.button == SDL_BUTTON_RIGHT && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
                        contextMenu.open = false;
                        itemMenu.open = false;
                        beginRightHold(mgx, mgz);
                    }
                    break;
                }

                // The talk owns the mouse while it is open. During a trade the Unit Panel still works, so a click there
                // passes on to the panel; everything else is the talk's or the trade's.
                if (dialogueUnit != NULL_ENTITY && screen == Screen::Playing) {
                    // The quantity prompt is modal over the trade: only its buttons answer
                    if (quantity.open && tradeOpen && !quantity.toStorage) {
                        if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
                        break;
                    }
                    const float x = e.button.x, y = e.button.y;
                    // The trade's two panels are drawn over the Unit Panel, so a click on them is theirs even where the
                    // Unit Panel is under it; only a click on the Unit Panel's uncovered part goes to the Unit Panel
                    const TradeLayout tradeL = layoutTrade(winW, winH, renderer.pauseBarHeight(false));
                    const bool onTrade = tradeOpen && (tradeL.interaction.contains(x, y) || tradeL.tradePanel.contains(x, y));
                    const bool unitPanelClick = tradeOpen && !onTrade && sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y);
                    if (!unitPanelClick) {
                        if (tradeOpen) {
                            const TradeLayout tl = layoutTrade(winW, winH, renderer.pauseBarHeight(false));
                            const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                            const bool leftButton = e.button.button == SDL_BUTTON_LEFT;
                            if (leftButton && tl.cancelBtn.contains(x, y) && freeTrade) {
                                endTalk();   // a free trade has no talk to go back to
                            } else if (leftButton && tl.cancelBtn.contains(x, y)) {
                                tradeOpen = false;
                                tradeNpc.clear();
                                tradePlayer.clear();
                                tradeNpcDrag = -1;
                            } else if (leftButton && tl.tradeBtn.contains(x, y)) {
                                if (!tradeItemsPresent(world, dialogueUnit, dialoguePlayer)) {
                                    // Something offered is no longer in its inventory: the trade fails and the talk ends
                                    tradeLabels.push_back(TradeLabel{ dialogueUnit, lang("MISC_TRADE_FAILED"), 0.0f });
                                    tradeLabels.push_back(TradeLabel{ dialoguePlayer, lang("MISC_TRADE_FAILED"), 0.0f });
                                    playSfx(Sfx::TradeFailed);
                                    endTalk();
                                } else if (checkTrade(world, dialogueUnit, dialoguePlayer).ok) {
                                    // Each side shows its own gain or loss: what it receives minus what it gives
                                    const int theirGive = tradeValueOf(tradeNpc);
                                    const int yourGive = tradeValueOf(tradePlayer);
                                    auto signedValue = [](int v) { return (v > 0 ? "+" : "") + std::to_string(v); };
                                    if (!freeTrade) {   // a free trade between your own units has no value to show
                                        tradeLabels.push_back(TradeLabel{ dialogueUnit, langf("MISC_TRADE_CHANGE", { signedValue(yourGive - theirGive) }), 0.0f });
                                        tradeLabels.push_back(TradeLabel{ dialoguePlayer, langf("MISC_TRADE_CHANGE", { signedValue(theirGive - yourGive) }), 0.0f });
                                    }
                                    completeTrade(world, dialogueUnit, dialoguePlayer);
                                    playSfx(Sfx::TradeDone);
                                    endTalk();
                                    showNotice(lang("MISC_TRADE_COMPLETE"), TRADE_LABEL_LIFE);
                                }
                            } else if (takeTradeGhostAt(tradeNpc, tl.theirCells, x, y)
                                       || takeTradeGhostAt(tradePlayer, tl.yourCells, x, y)) {
                                // A ghost was taken back out of its cell
                            } else {
                                // An NPC item: left button drags it to the left box, right button puts it there
                                for (size_t i = 0; npcInv && i < tl.npcSlots.size(); ++i) {
                                    if (!tl.npcSlots[i].contains(x, y) || npcInv->slots[i].item == 0) continue;
                                    if (leftButton) {
                                        tradeNpcDrag = (int)i;
                                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                                        requestTrade(true, dialogueUnit, (int)i);
                                    }
                                    break;
                                }
                            }
                        } else if (e.button.button == SDL_BUTTON_LEFT) {
                            const DialogueLayout dl = dialogueBox(winW, winH);
                            if (dl.end.contains(x, y)) {
                                talkCooldown = TALK_COOLDOWN;
                                dialogueUnit = NULL_ENTITY;
                                dialogueConvo.clear();
                            } else {
                                for (size_t i = 0; i < dl.options.size(); ++i) {
                                    if (dl.options[i].contains(x, y)) {
                                        chooseOption(i);
                                        break;
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
                // The right-click menu: a left click on an option picks it. A click anywhere else closes the menu, and
                // that click still does what it would have done (a right-click elsewhere opens a new menu or moves).
                if (screen == Screen::Playing && contextMenu.open) {
                    const ItemMenuLayout ml = contextMenuLayout();
                    if (ml.box.contains(e.button.x, e.button.y)) {
                        const int hit = itemMenuButtonAt(ml, e.button.x, e.button.y);
                        if (e.button.button == SDL_BUTTON_LEFT && hit >= 0 && hit < (int)contextMenu.actions.size()) {
                            pickMenuAction(contextMenu.actions[(size_t)hit]);
                        }
                        break;
                    }
                    contextMenu.open = false;
                }
                // Choosing how many of a stack to store: the prompt is modal, and only its buttons answer
                if (screen == Screen::Playing && quantity.open && quantity.toStorage) {
                    if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
                    break;
                }
                // The looting panel takes every click on it: a click on an item takes it (as much as fits), Take All takes
                // everything that fits, Close closes it
                if (screen == Screen::Playing && lootUnit != NULL_ENTITY) {
                    const LootLayout ll = layoutLoot(winW, winH);
                    const float x = e.button.x, y = e.button.y;
                    if (ll.panel.contains(x, y)) {
                        if (e.button.button == SDL_BUTTON_LEFT && ll.closeBtn.contains(x, y)) {
                            closeLoot();
                            playSfx(Sfx::UiBack);
                        } else if (e.button.button == SDL_BUTTON_LEFT && ll.takeAllBtn.contains(x, y)) {
                            if (lootAll(world, lootBy, lootUnit) == 0) refuse(lang("MISC_LOOT_NOTHING"));
                        } else {
                            std::string why;
                            bool hitCell = false;
                            for (int g = 0; g < GEAR_SLOTS && !hitCell; ++g) {
                                if (!ll.gear[(size_t)g].contains(x, y)) continue;
                                hitCell = true;
                                if (!lootTake(world, lootBy, lootUnit, true, g, why) && !why.empty()) refuse(why);
                            }
                            for (int i = 0; i < INVENTORY_SLOTS && !hitCell; ++i) {
                                if (!ll.slots[(size_t)i].contains(x, y)) continue;
                                hitCell = true;
                                if (!lootTake(world, lootBy, lootUnit, false, i, why) && !why.empty()) refuse(why);
                            }
                        }
                        break;
                    }
                }
                // The storage panel takes every click on it. Left button on an item starts dragging it out; right button
                // moves its whole stack into the unit's inventory, if all of it fits.
                if (screen == Screen::Playing && storageProp != NULL_ENTITY) {
                    const StorageLayout stl = storageLayout(world, winW, winH);
                    const float x = e.button.x, y = e.button.y;
                    if (stl.panel.contains(x, y)) {
                        if (e.button.button == SDL_BUTTON_LEFT && stl.lockToggle && stl.lockBtn.contains(x, y)) {
                            // The owner opens the lock to everyone, or shuts it again for outsiders to pick
                            if (Lock* l = world.get<Lock>(storageProp)) l->locked = !l->locked;
                            playSfx(Sfx::UiToggle);
                        } else if (e.button.button == SDL_BUTTON_LEFT && stl.closeBtn.contains(x, y)) {
                            closeStorage();
                            playSfx(Sfx::UiBack);
                        } else {
                            // The input (or only) grid's cells are panel slots 0 and up, a forge's output cells follow
                            std::vector<Rect> cells = stl.slots;
                            cells.insert(cells.end(), stl.outSlots.begin(), stl.outSlots.end());
                            for (size_t i = 0; i < cells.size(); ++i) {
                                const ItemStack* at = storageSlot(world, (int)i);
                                if (!cells[i].contains(x, y) || !at || at->item == 0) continue;
                                if (e.button.button == SDL_BUTTON_LEFT) {
                                    storageDrag = (int)i;
                                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                                    std::string why;
                                    if (takeFromStorage(world, (int)i, why)) queueSound(Sfx::ItemMove);
                                    else if (!why.empty()) refuse(why);
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
                // The locks and the [-] and [+] buttons on the title bars. Folding keeps the panel's size for when it opens.
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const float tx = e.button.x, ty = e.button.y;
                    bool hitLock = false;
                    for (PanelId which : ALL_PANELS) {
                        if (hitLock) break;
                        if (lockRectOf(which, winW, winH).contains(tx, ty)) {
                            toggleLock(which);
                            hitLock = true;
                        }
                    }
                    if (hitLock) break;
                    const SidebarLayout sbl = sidebarFor(winW, winH, sidebarScroll);
                    if (sbl.plus.contains(tx, ty)) {
                        setFolded(PanelId::Unit, false, winW, winH);
                        break;
                    }
                    if (sbl.minus.contains(tx, ty)) {
                        setFolded(PanelId::Unit, true, winW, winH);
                        break;
                    }
                    if (infoPanelClick(winW, winH, tx, ty)) break;
                    if (layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).toggle.contains(tx, ty)) {
                        setFolded(PanelId::Squad, !squadCollapsed, winW, winH);
                        break;
                    }
                    if (gameLogPanel(winW, winH).toggle.contains(tx, ty)) {
                        setFolded(PanelId::Info, !combatCollapsed, winW, winH);
                        break;
                    }
                }
                // The Faction Panel's tabs, and in its Reputation tab a faction's row: that faction's reputations show
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const UnitPanelLayout fp = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed,
                                                               squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
                    const float tx = e.button.x, ty = e.button.y;
                    if (fp.tabSquad.contains(tx, ty) || fp.tabReputation.contains(tx, ty)) {
                        const int next = fp.tabSquad.contains(tx, ty) ? FACTION_TAB_SQUAD : FACTION_TAB_REPUTATION;
                        if (next != factionTab) playSfx(Sfx::UiClick);
                        factionTab = next;
                        break;
                    }
                    bool picked = false;
                    for (size_t i = 0; i < fp.repRows.size(); ++i) {
                        const size_t idx = (size_t)fp.repFirst + i;
                        if (!fp.repRows[i].contains(tx, ty) || idx >= factions().size()) continue;
                        if (reputationFactions()[idx].id != repView) playSfx(Sfx::UiClick);
                        repView = reputationFactions()[idx].id;
                        picked = true;
                        break;
                    }
                    if (picked) break;
                    if (factionTab == FACTION_TAB_REPUTATION && fp.repHeading.contains(tx, ty)) break;
                }
                // The Squad Panel's scroll bar: an arrow moves the list a row, the thumb can be dragged, and a press on
                // the track moves a row toward it
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const UnitPanelLayout sq = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
                    const float tx = e.button.x, ty = e.button.y;
                    if (sq.scrollUp.contains(tx, ty) || sq.scrollDown.contains(tx, ty)) {
                        const int next = std::clamp(sq.scrollRow + (sq.scrollUp.contains(tx, ty) ? -1 : 1), 0, sq.maxScroll());
                        if (next != sq.scrollRow) playSfx(Sfx::UiClick);
                        unitPanelRow = next;
                        break;
                    }
                    if (sq.scrollable() && sq.thumb.contains(tx, ty)) {
                        squadThumbDrag = true;
                        squadThumbGrab = ty - sq.thumb.y;
                        break;
                    }
                    if (sq.track.contains(tx, ty)) {
                        unitPanelRow = std::clamp(sq.scrollRow + (ty < sq.thumb.y ? -1 : 1), 0, sq.maxScroll());
                        break;
                    }
                }
                // An edge, a corner or the title bar of an unlocked panel grabs it, to resize or move it
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const auto g = grabAt(winW, winH, e.button.x, e.button.y);
                    if (g.first) {
                        const Rect r = panelRect(*g.first, placeOf(*g.first), winW, winH);
                        grabWhich = g.first;
                        grabKind = g.second;
                        grabMouseX = e.button.x;
                        grabMouseY = e.button.y;
                        grabX = r.x;
                        grabY = r.y;
                        grabW = r.w;
                        grabH = r.h;
                        break;
                    }
                }
                if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
                    // The unit panel and sidebar take the click, so it never reaches the ground
                    const float mx = e.button.x, my = e.button.y;
                    const std::vector<Entity> own = ownUnitList();
                    const UnitPanelLayout pl = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);

                    // The right-click menu: a click on one of its buttons does that action. A click
                    // anywhere else just closes it, and the click still does what it would have done.
                    if (itemMenu.open) {
                        const std::vector<std::string> labels = menuLabels();
                        if (labels.empty()) {
                            itemMenu.open = false;
                        } else {
                            const ItemMenuLayout ml = renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH);
                            const int hit = itemMenuButtonAt(ml, mx, my);
                            const bool onMenu = ml.box.contains(mx, my);
                            const Entity unit = itemMenu.unit;
                            const int slot = itemMenu.slot;
                            itemMenu.open = false;
                            if (hit >= 0 && labels[hit] == lang("UILABEL_USE")) {
                                useSlot(unit, slot);
                                playSfx(Sfx::UiClick);
                            } else if (hit >= 0 && labels[hit] == lang("UILABEL_DROP")) {
                                // A drop from the inventory plays its own sound, in place of the menu click
                                if (!dropStack(world, unit, slot)) playSfx(Sfx::UiError);
                            }
                            if (onMenu) break;
                        }
                    }

                    // The Unit Panel's tabs pick what it shows
                    {
                        const SidebarLayout st = sidebarFor(winW, winH, sidebarScroll);
                        bool tabHit = false;
                        for (int t = 0; t < UNIT_TAB_COUNT && st.open; ++t) {
                            if (st.tabs[t].contains(mx, my)) {
                                if (!unitTabEnabled(world, sidebarUnitOf(world), (UnitTab)t)) {
                                    tabHit = true;   // off for a unit not in your squad: the click does nothing
                                    break;
                                }
                                if (unitTab != (UnitTab)t) playSfx(Sfx::UiTab);
                                unitTab = (UnitTab)t;
                                sidebarScroll = 0.0f;
                                tabHit = true;
                                break;
                            }
                        }
                        if (tabHit) break;
                    }

                    if (overPanels(mx, my, winW, winH)) {
                        // The pace toggle and the View Map button on the bottom panel
                        if (pl.paceButton.contains(mx, my)) { runMode = !runMode; playSfx(Sfx::UiToggle); break; }
                        if (pl.optimizeAllButton.contains(mx, my)) {
                            contextMenu.open = false;
                            itemMenu.open = false;
                            optimizeAllOpen = true;
                            optimizeSliderDrag = false;
                            playSfx(Sfx::UiMenuOpen);
                            break;
                        }

                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const bool onSidebar = sl.panel.contains(mx, my);
                        if (onSidebar) {
                            // The Build tab: a row picks its prop to place, and picking the same one again puts it down
                            if (sl.open && sl.tab == UnitTab::Build) {
                                for (size_t i = 0; i < sl.buildRows.size(); ++i) {
                                    if (!sl.buildRows[i].contains(mx, my)) continue;
                                    const BuildPick pick = buildRowPick((int)i);
                                    if (!pick.any()) break;
                                    // Grayed out: only Destroy, the Bedroll and the Campfire, unless a selected
                                    // unit of yours stands in one of your claims
                                    if (!buildAnywhere(pick) && !selectionInClaim(world)) break;
                                    if (buildPick == pick) {
                                        buildPick = {};
                                    } else {
                                        buildPick = pick;
                                        showNotice(lang(!pick.placing() ? "UILABEL_BUILD_DESTROY_HINT"
                                                                              : "UILABEL_BUILD_PLACE_HINT"), 4.0f);
                                    }
                                    playSfx(Sfx::UiClick);
                                    break;
                                }
                                break;
                            }
                            // Sort and the behavior rows change your unit, when the sidebar shows one. They
                            // come before the drags, which start on the inventory and gear.
                            if (shownIsYours()) {
                                const Entity shown = sidebarUnitOf(world);
                                if (sl.sortButton.contains(mx, my)) {
                                    itemMenu.open = false;
                                    if (!inventoryLocked(world, shown)) sortInventory(shown);   // a mob's is shut
                                    break;
                                }
                                // The Gear tab: Optimize wears the best gear on hand, Remove All takes it all off.
                                // Either one is checked as a whole first; if the inventory can't take what comes off,
                                // nothing changes.
                                if (sl.optimizeButton.contains(mx, my) || sl.removeAllButton.contains(mx, my)) {
                                    itemMenu.open = false;
                                    const bool optimize = sl.optimizeButton.contains(mx, my);
                                    std::string why;
                                    const GearAction done = optimize ? optimizeGear(world, shown, why, gearPrefs()) : removeAllGear(world, shown, why);
                                    if (done == GearAction::Done) {
                                        queueSound(Sfx::ItemMove);
                                    } else if (done == GearAction::NoRoom) {
                                        refuse(why);
                                    } else {
                                        showNotice(lang(optimize ? "MISC_GEAR_ALREADY_BEST" : "MISC_GEAR_NONE_WORN"));
                                    }
                                    break;
                                }
                                // The fighting modes: Passive, Block and Sneak switch on and off; Ranged follows the
                                // weapon. Passive and Block end any fight the unit was picking.
                                {
                                    int mode = -1;
                                    for (int i = 0; i < 3; ++i) {
                                        if (sl.modes[(size_t)i].contains(mx, my)) mode = i;
                                    }
                                    if (sl.modes[3].contains(mx, my)) break;
                                    if (mode >= 0) {
                                        if (Behaviors* b = world.get<Behaviors>(shown)) {
                                            bool& flag = mode == 0 ? b->passive : mode == 1 ? b->block : b->sneak;
                                            flag = !flag;
                                            if (flag && mode < 2 && !world.has<AttackOrder>(shown)) {
                                                if (Combat* c = world.get<Combat>(shown)) c->target = NULL_ENTITY;
                                            }
                                            playSfx(Sfx::UiClick);
                                        }
                                        break;
                                    }
                                }
                                const BehaviorHit bh = behaviorHitAt(sl, mx, my);
                                if (bh.row >= 0) {
                                    behaviorClick(shown, bh);
                                    break;
                                }
                                if (sl.tab == UnitTab::Behavior && behaviorExtraClick(shown, sl, mx, my)) break;
                            }

                            // A filled inventory slot starts dragging its stack out of the inventory. A filled
                            // gear slot starts dragging the gear item out of the unit's gear. A red unit's
                            // sidebar has no unit of yours in it, so nothing there can be dragged.
                            const std::vector<Entity> picked = panelUnits(world);
                            const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                            const Equipment* worn = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                            const int slot = sidebarSlotAt(sl, mx, my);
                            const int gear = gearSlotAt(sl, mx, my);
                            if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                                itemDrag = true;
                                dragUnit = picked[0];
                                dragSlot = slot;
                                dragGear = -1;
                            } else if (worn && gear >= 0 && worn->items[gear] != 0) {
                                itemDrag = true;
                                dragUnit = picked[0];
                                dragSlot = -1;
                                dragGear = gear;
                            }
                        }
                        for (const UnitCell& c : pl.cells) {
                            if (!c.rect.contains(mx, my)) continue;
                            const Entity u = own[c.index];
                            // Shift toggles the unit in the selection. Alt adds it and never clears or removes anything.
                            const SDL_Keymod mods = SDL_GetModState();
                            const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                            const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                            inspectUnit = NULL_ENTITY;
                            if (shift && world.has<Selected>(u)) {
                                // Shift takes it out of the selection, but never the last one
                                if (selectedUnits(world).size() > 1) world.remove<Selected>(u);
                            } else {
                                if (!additive) clearSelection(world);
                                if (!world.has<Selected>(u)) world.add(u, Selected{});
                            }
                            // Double-click moves the camera to the unit, wherever it is, and keeps it on the unit as it moves
                            if (e.button.clicks == 2) {
                                focusCameraOnUnit(world, cam, u);
                                followUnit = u;
                            }
                            break;
                        }
                        break;
                    }

                    // While a prop is picked to build, a left click on the ground places it. A refusal says why, and
                    // the pick stays, so another spot can be tried. The click finds the same spot the preview shows.
                    if (buildPick.any() && dialogueUnit == NULL_ENTITY && !mapOpen) {
                        if (!buildPick.placing()) {
                            // Destroy takes down the prop under the mouse nearest the eye, whatever it is: its
                            // items fall where it stood and it becomes Debris (Debris itself is cleared away). Anyone on
                            // it gets off first (breakBuilding). Another faction's prop costs standing with it.
                            Entity target = NULL_ENTITY;
                            float ex = 0.0f, ey = 0.0f, ez = 0.0f, best = 0.0f;
                            cam.eye(ex, ey, ez);
                            for (Entity e : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
                                const Transform* pt = world.get<Transform>(e);
                                const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
                                if (target == NULL_ENTITY || d2 < best) {
                                    target = e;
                                    best = d2;
                                }
                            }
                            const Owner* own = target != NULL_ENTITY ? world.get<Owner>(target) : nullptr;
                            const int owner = own ? own->faction : FACTION_PLAYER;
                            if (target != NULL_ENTITY && storageProp == target) closeStorage();
                            if (target != NULL_ENTITY && destroyBuilding(world, target)) {
                                if (owner != FACTION_PLAYER) changePlayerReputation(owner, -REP_DESTROY_PENALTY);
                                playSfx(Sfx::Drop);
                            } else {
                                refuse(lang("MISC_DESTROY_NOTHING"));
                            }
                        } else if (auto hit = pickGround(cam, terrain, mx, my, winW, winH)) {
                            float bx = 0.0f, bz = 0.0f;
                            snapBuildSpot(buildPick.kind, hit->first, hit->second, bx, bz);
                            const BuildCheck why = checkBuildSpot(world, terrain, buildPick.kind, bx, bz, buildYaw);
                            if (why == BuildCheck::Ok) {
                                placeBuiltProp(world, terrain, buildPick.kind, bx, bz, buildYaw);
                                playSfx(Sfx::UiClick);
                            } else {
                                refuse(lang(buildRefusalKey(why)));
                            }
                        }
                        break;
                    }

                    // A click on the visible part of an item on the ground (or, while Alt shows them, on its name)
                    // sends the selected units to pick it up
                    {
                        const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
                        const Entity item = renderer.groundItemAtPixel(world, terrain, cam, mx, my, winW, winH, alt);
                        if (item != NULL_ENTITY) {
                            const std::vector<Entity> picked = selectedUnits(world);
                            if (picked.empty()) refuse(lang("MISC_PICKUP_SELECT"));
                            else {
                                noteLockedIn(picked);
                                issuePickupOrder(world, terrain, picked, item);
                            }
                            break;
                        }
                    }
                }

                if (e.button.button == SDL_BUTTON_LEFT) {
                    leftDown = true;
                    pressX = e.button.x;
                    pressY = e.button.y;
                    sel.dragging = false;
                } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                    // Pressing alone changes nothing. Dragging orbits around the current focus.
                    // Pressed over the panels, it orbits nothing.
                    if (!overPanels(e.button.x, e.button.y, winW, winH)) {
                        middleDown = true;
                        lastX = e.button.x;
                        lastY = e.button.y;
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    float mgx = 0.0f, mgz = 0.0f;
                    if (screen == Screen::Playing && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
                        // The minimap: the selected units walk to the point clicked on it
                        beginRightHold(mgx, mgz);
                    } else if (overPanels(e.button.x, e.button.y, winW, winH)) {
                        // Right-click on a unit in the Squad Panel: its menu, as on the unit in the world
                        {
                            const std::vector<Entity> own = ownUnitList();
                            const UnitPanelLayout squadL = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
                            bool onCell = false;
                            for (const UnitCell& c : squadL.cells) {
                                if (!c.rect.contains(e.button.x, e.button.y) || c.index < 0 || c.index >= (int)own.size()) continue;
                                onCell = true;
                                openContextMenuOn(own[(size_t)c.index], e.button.x, e.button.y, true);
                                break;
                            }
                            if (onCell) break;
                        }
                        // Right-click on an inventory item: gear goes straight to its slot, and anything
                        // else opens the menu with Use and Drop. Right-click on a worn gear item takes it off
                        // into the inventory. Elsewhere on the panels, nothing happens. A red unit's sidebar
                        // has no unit of yours in it, so neither of those applies there.
                        itemMenu.open = false;
                        const std::vector<Entity> picked = panelUnits(world);
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const int slot = sidebarUp() ? sidebarSlotAt(sl, e.button.x, e.button.y) : -1;
                        const int gear = sidebarUp() ? gearSlotAt(sl, e.button.x, e.button.y) : -1;
                        const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                        const Equipment* wornHere = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                        if (wornHere && gear >= 0 && wornHere->items[gear] != 0) {
                            std::string why;
                            if (unequipToInventory(world, picked[0], gear, why)) queueSound(Sfx::ItemMove);
                            else if (!why.empty()) refuse(why);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && tradeOpen) {
                            // During a trade a right-click puts the item in the trade, whatever it is
                            requestTrade(false, picked[0], slot);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && storageProp != NULL_ENTITY &&
                                   picked[0] == storageUnit) {
                            // With a storage panel open, a right-click puts the item in the storage. A stack asks how many.
                            requestStore(slot);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                            const int item = inv->slots[slot].item;
                            const ItemDef* def = findItem(item);
                            if (def && !def->equipSlot.empty()) {
                                const Equipment* worn = world.get<Equipment>(picked[0]);
                                const int target = worn ? gearSlotFor(*worn, item) : -1;
                                std::string why;
                                if (target < 0 || !equipFromInventory(world, picked[0], slot, target, why)) {
                                    refuse(why.empty() ? langf("MISC_CANT_BE_WORN", { itemDisplayName(*def) }) : why);
                                } else {
                                    queueSound(Sfx::ItemMove);
                                }
                            } else {
                                itemMenu = ItemMenu{ true, picked[0], slot, e.button.x, e.button.y };
                                playSfx(Sfx::UiMenuOpen);
                            }
                        }
                    } else {
                        // A unit or a usable prop under the pointer opens the right-click menu: Talk, Trade, Follow,
                        // Defend, Attack, Inventory or Use, as fits it. Anywhere else the selected units walk there.
                        if (openContextMenu(e.button.x, e.button.y)) {
                            rightHold = false;
                        } else {
                            float gx = 0.0f, gz = 0.0f;
                            if (rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH, gx, gz)) beginRightHold(gx, gz);
                        }
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (disbandHeldAt != 0 && e.button.button == SDL_BUTTON_LEFT) {
                    disbandHeldAt = 0;   // let go before it filled: the hold starts over
                    break;
                }
                if (squadThumbDrag && e.button.button == SDL_BUTTON_LEFT) {
                    squadThumbDrag = false;
                    break;
                }
                if (mapDragging && e.button.button == SDL_BUTTON_LEFT) {
                    mapDragging = false;
                    break;
                }
                if (partyUi.listDrag && e.button.button == SDL_BUTTON_LEFT) {
                    partyUi.listDrag = false;
                    break;
                }
                if (partyUi.previewDrag && e.button.button == SDL_BUTTON_RIGHT) {
                    partyUi.previewDrag = false;
                    break;
                }
                if (optimizeSliderDrag && e.button.button == SDL_BUTTON_LEFT) {
                    optimizeSliderDrag = false;
                    saveSettingsFile();   // its last place is kept
                    playSfx(Sfx::UiSlider);
                    break;
                }
                // Letting go of the right button plans the route to where it was held
                if (rightHold && e.button.button == SDL_BUTTON_RIGHT) {
                    rightHold = false;
                    moveSelectedTo(holdX, holdZ);
                    break;
                }
                if (grabWhich && e.button.button == SDL_BUTTON_LEFT) {
                    endGrab(winW, winH);
                    setGrabCursor(grabAt(winW, winH, e.button.x, e.button.y).second);
                    break;
                }
                // A slider let go of: save its final level
                if (e.button.button == SDL_BUTTON_LEFT && settingsUi.dragRow >= 0) {
                    settingsUi.dragRow = -1;
                    saveSettingsFile();
                }
                // An item dragged out of the storage: let go over the panel it stays; over the Unit Panel it goes into the
                // unit's inventory if it all fits; anywhere else it drops where the unit stands, not at the prop
                if (e.button.button == SDL_BUTTON_LEFT && storageDrag >= 0) {
                    const int from = storageDrag;
                    storageDrag = -1;
                    if (storageProp != NULL_ENTITY) {
                        const float x = e.button.x, y = e.button.y;
                        if (storageLayout(world, winW, winH).panel.contains(x, y)) {
                            // back where it was
                        } else if (sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y)) {
                            std::string why;
                            if (takeFromStorage(world, from, why)) queueSound(Sfx::ItemMove);
                            else if (!why.empty()) refuse(why);
                        } else if (dropFromStorage(world, from)) {
                            queueSound(Sfx::ItemMove);
                        }
                    }
                    break;
                }
                // An inventory stack let go over the storage panel goes into the storage, if all of it fits
                if (e.button.button == SDL_BUTTON_LEFT && itemDrag && storageProp != NULL_ENTITY &&
                    storageLayout(world, winW, winH).panel.contains(e.button.x, e.button.y)) {
                    if (dragGear < 0 && dragUnit == storageUnit) requestStore(dragSlot);
                    itemDrag = false;
                    dragUnit = NULL_ENTITY;
                    dragSlot = -1;
                    dragGear = -1;
                    break;
                }
                // The trade: an NPC item let go over its side becomes a ghost there. A unit's item let go over your side
                // does too, if it is the unit being traded with. Either way the drag ends with nothing moved.
                if (e.button.button == SDL_BUTTON_LEFT && tradeOpen && dialogueUnit != NULL_ENTITY) {
                    const float x = e.button.x, y = e.button.y;
                    const TradeLayout tl = layoutTrade(winW, winH, renderer.pauseBarHeight(false));
                    if (tradeNpcDrag >= 0) {
                        if (tl.theirBox.contains(x, y)) requestTrade(true, dialogueUnit, tradeNpcDrag);
                        tradeNpcDrag = -1;
                    }
                    // The trade's panels are over the Unit Panel: a drop on them is theirs, wherever the Unit Panel is
                    const bool onTrade = tl.interaction.contains(x, y) || tl.tradePanel.contains(x, y);
                    if (itemDrag && (onTrade || !sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y))) {
                        if (tl.yourBox.contains(x, y) && dragGear < 0) {
                            if (dragUnit == dialoguePlayer) requestTrade(false, dragUnit, dragSlot);
                            else refuse(lang("MISC_TRADE_ONLY_TALKING"));
                        }
                        itemDrag = false;
                        dragUnit = NULL_ENTITY;
                        dragSlot = -1;
                        dragGear = -1;
                    }
                }
                if (e.button.button == SDL_BUTTON_LEFT && itemDrag) {
                    itemDrag = false;
                    if (world.has<Unit>(dragUnit)) {
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const float rx = e.button.x, ry = e.button.y;
                        const bool overSidebar = sl.panel.contains(rx, ry);
                        const int gear = gearSlotAt(sl, rx, ry);
                        const bool onGrid = sidebarSlotAt(sl, rx, ry) >= 0;
                        std::string why;
                        if (dragGear < 0) {
                            // An inventory stack: onto a gear slot it equips, out of the sidebar it drops
                            // under the unit. Released on the rest of the sidebar, nothing happens.
                            if (gear >= 0) {
                                if (equipFromInventory(world, dragUnit, dragSlot, gear, why)) queueSound(Sfx::ItemMove);
                                else if (!why.empty()) refuse(why);
                            } else if (!overSidebar) {
                                if (dropStack(world, dragUnit, dragSlot)) queueSound(Sfx::ItemMove);
                            }
                        } else {
                            // A gear item: onto another gear slot it moves, onto the inventory it comes off,
                            // out of the sidebar it drops. Released on the rest of the sidebar, nothing happens.
                            if (gear >= 0 && gear != dragGear) {
                                if (!moveEquipped(world, dragUnit, dragGear, gear, why) && !why.empty()) refuse(why);
                            } else if (onGrid) {
                                if (unequipToInventory(world, dragUnit, dragGear, why)) queueSound(Sfx::ItemMove);
                                else if (!why.empty()) refuse(why);
                            } else if (!overSidebar) {
                                dropEquipped(world, dragUnit, dragGear);
                            }
                        }
                    }
                    dragUnit = NULL_ENTITY;
                    dragSlot = -1;
                    dragGear = -1;
                }
                if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
                    leftDown = false;
                    // Shift adds to the selection. Alt never clears it, so Alt-click on empty ground does nothing.
                    const SDL_Keymod mods = SDL_GetModState();
                    const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                    if (sel.dragging) {
                        // A box with none of your units in it but items on the ground sends the selected units to
                        // pick them all up, as one chain shared between them
                        if (!boxSelect(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH, additive)) {
                            const std::vector<Entity> items =
                                groundItemsInBox(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH);
                            if (!items.empty()) {
                                const std::vector<Entity> picked = selectedUnits(world);
                                if (picked.empty()) refuse(lang("MISC_PICKUP_SELECT"));
                                else {
                                    noteLockedIn(picked);
                                    issuePickupChain(world, terrain, picked, items);
                                }
                            }
                        }
                    } else {
                        const std::vector<Entity> under = renderer.unitsAtPixel(world, cam, e.button.x, e.button.y, winW, winH);
                        clickSelect(world, unitClicked(world, terrain, cam, under, e.button.x, e.button.y, winW, winH), additive);
                    }
                    sel.dragging = false;
                }
                if (e.button.button == SDL_BUTTON_MIDDLE) middleDown = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                // The party menu's unit list follows its scroll bar thumb while it is held
                if (screen == Screen::PartySetup && partyUi.listDrag) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.motion.y - partyUi.listGrab);
                    } else {
                        partyUi.listDrag = false;
                    }
                    break;
                }
                // The held model turns with the mouse's sideways movement only: about its vertical axis, never tipping
                if (screen == Screen::PartySetup && partyUi.previewDrag) {
                    if (e.motion.state & SDL_BUTTON_RMASK) {
                        partyUi.previewYaw = std::remainder(partyUi.previewYaw + e.motion.xrel * 0.012f, 6.2831853f);
                    } else {
                        partyUi.previewDrag = false;
                    }
                    break;
                }
                // The Optimize All slider follows the mouse while its button is held
                if (screen == Screen::Playing && optimizeAllOpen && optimizeSliderDrag) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        settings.optimizeAttackShare = levelAtX(layoutOptimizeAll(winW, winH).track, e.motion.x);
                    } else {
                        optimizeSliderDrag = false;
                        saveSettingsFile();
                    }
                    break;
                }
                // The world map window follows the mouse while its title bar is held, kept inside the screen
                if (mapDragging) {
                    if ((e.motion.state & SDL_BUTTON_LMASK) && mapOpen && screen == Screen::Playing) {
                        mapDragX = e.motion.x - mapGrabX;
                        mapDragY = e.motion.y - mapGrabY;
                        const WorldMapLayout wm = worldMapFor(winW, winH);   // clamped
                        mapDragX = wm.window.x - std::floor((winW - wm.window.w) * 0.5f);
                        mapDragY = wm.window.y - std::floor((winH - wm.window.h) * 0.5f);
                    } else {
                        mapDragging = false;
                    }
                    break;
                }
                if (grabWhich) {
                    applyGrab(winW, winH, e.motion.x, e.motion.y);
                    break;
                }
                // The Squad Panel follows its scroll bar thumb while it is held
                if (squadThumbDrag) {
                    if ((e.motion.state & SDL_BUTTON_LMASK) && screen == Screen::Playing) {
                        const UnitPanelLayout sq = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
                        unitPanelRow = squadRowAtThumb(sq, e.motion.y - squadThumbGrab);
                    } else {
                        squadThumbDrag = false;
                    }
                    break;
                }
                if (screen == Screen::Playing) {
                    const bool onMap = mapOpen && worldMapFor(winW, winH).window.contains(e.motion.x, e.motion.y);
                    setGrabCursor(onMap ? 0 : grabAt(winW, winH, e.motion.x, e.motion.y).second);
                }
                // A slider follows the mouse while its button is held
                if (screen == Screen::Settings && settingsUi.dragRow >= 0) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        const SettingsLayout L = layoutSettings(winW, winH, rows, settingsUi);
                        if (settingsUi.dragRow < (int)L.rows.size()) {
                            const int level = levelAtX(L.rows[settingsUi.dragRow].track, e.motion.x);
                            settingsCommit(setRowLevel(rows[settingsUi.dragRow].id, level, settings), false);
                        }
                    } else {
                        // The button came up outside the window, so no button-up arrived: save here
                        settingsUi.dragRow = -1;
                        saveSettingsFile();
                    }
                }
                if (screen == Screen::Playing) {
                    if (screen == Screen::Playing && leftDown) {
                        float dx = e.motion.x - pressX, dy = e.motion.y - pressY;
                        if (!sel.dragging && dx * dx + dy * dy > 36.0f) sel.dragging = true;
                        if (sel.dragging) {
                            sel.x0 = pressX; sel.y0 = pressY;
                            sel.x1 = e.motion.x; sel.y1 = e.motion.y;
                        }
                    }
                    if (middleDown) {
                        cam.yaw += (e.motion.x - lastX) * 0.005f;
                        cam.pitch += (e.motion.y - lastY) * 0.005f;
                        cam.clampState();
                        lastX = e.motion.x;
                        lastY = e.motion.y;
                    }
                }
                break;
            }
        }

        // ---- Delete hold: the save is deleted once the button has been held for DELETE_HOLD_MS ----
        if (deleteHolding) {
            float hx = 0.0f, hy = 0.0f;
            const bool leftHeld = (SDL_GetMouseState(&hx, &hy) & SDL_BUTTON_LMASK) != 0;
            if (!leftHeld || !deleteHoldRect.contains(hx, hy)) {
                deleteHolding = false;          // let go early, or moved off the button: start over
            } else if (SDL_GetTicks() - deleteHoldStart >= DELETE_HOLD_MS) {
                performDelete();
            }
        }

        // ---- Timing ----
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.25f) dt = 0.25f;
        if (noticeSec > 0.0f) noticeSec -= dt;
        // The stats popout and the item menu belong to one lone selected unit. They close when that changes.
        if (screen == Screen::Playing) {
            const Entity shown = sidebarUnitOf(world);
            const bool lone = shown != NULL_ENTITY;
            // The Unit Panel stays as the player left it, open or folded, whatever is selected. Its contents start at the
            // top for each new unit.
            if (lone && shown != lastLone) sidebarScroll = 0.0f;
            lastLone = shown;
            // A folded panel opens toward the middle of the window from wherever its bar is, also after the window
            // changes size (not while it is being dragged). An open panel's bar changes side only when it is moved or
            // resized (endGrab), so it never jumps just after opening.
            for (PanelId which : ALL_PANELS) {
                if (which != grabWhich && panelFolded(which)) placeOf(which) = settlePanel(which, placeOf(which), winW, winH, true);
            }
            if (itemMenu.open) {
                const Inventory* inv = world.get<Inventory>(itemMenu.unit);
                const bool same = lone && shown == itemMenu.unit;
                const bool hasItem = inv && itemMenu.slot >= 0 && itemMenu.slot < INVENTORY_SLOTS
                                  && inv->slots[itemMenu.slot].item != 0;
                if (!same || !hasItem) itemMenu.open = false;
            }
            // The sidebar starts at the top for a unit it was not scrolled for. The editor closes when its rule is gone.
            if (shown != sidebarScrollUnit) {
                sidebarScrollUnit = shown;
                sidebarScroll = 0.0f;
            }
        }
        pivotAlpha = std::clamp(pivotAlpha + (middleDown ? 1.0f : -1.0f) * dt * 4.0f, 0.0f, 1.0f);
        if (rightHold && screen == Screen::Playing) {
            // The hold's point follows the mouse, and the units walk straight at it: no route is planned until release
            float hx = holdX, hz = holdZ, mhx = 0.0f, mhy = 0.0f;
            SDL_GetMouseState(&mhx, &mhy);
            if (holdTarget(winW, winH, mhx, mhy, hx, hz)) {
                holdX = hx;
                holdZ = hz;
            }
            steerUnits(world, selectedUnits(world), Vec2{ holdX, holdZ });
            actionX = holdX;
            actionZ = holdZ;
            actionAlpha = 1.0f;
            markMove(holdX, holdZ);
        } else {
            rightHold = false;
            actionAlpha = std::max(0.0f, actionAlpha - dt);   // fades out over about a second
        }
        moveMarkTime = std::max(0.0f, moveMarkTime - dt);

        // Game messages written outside a fight (XP, pickups, drops) join the log every frame
        flushQueuedLog(gameLog);
        cam.mapSize = (float)terrain.size();
        cam.viewRange = (float)settings.viewRange;

        // ---- Game update: only while playing ----
        // The game stands still while a talk is open, and only End Dialogue starts it again
        talkCooldown = std::max(0.0f, talkCooldown - dt);
        // A talk order ends the first time one of its units is in talk range of its NPC
        if (screen == Screen::Playing && dialogueUnit == NULL_ENTITY && !paused) {
            Entity talkNpc = NULL_ENTITY, talker = NULL_ENTITY;
            bool tradeTalk = false, shopTalk = false;
            for (Entity u : world.view<TalkOrder>()) {
                const Entity npcOf = world.get<TalkOrder>(u)->npc;
                if (!world.has<Unit>(npcOf)) {
                    clearTalkOrders(world);
                    break;
                }
                if (gapBetween(world, u, npcOf) <= TALK_RANGE) {
                    talkNpc = npcOf;
                    talker = u;
                    tradeTalk = world.get<TalkOrder>(u)->trade;
                    shopTalk = world.get<TalkOrder>(u)->shop;
                    break;
                }
            }
            if (talker != NULL_ENTITY && tradeTalk) startFreeTrade(talkNpc, talker);
            else if (talker != NULL_ENTITY) tryStartTalk(talkNpc, talker, {}, shopTalk);
        }
        if (screen == Screen::Playing && dialogueUnit == NULL_ENTITY && !paused) {
            panWithKeys(dt, settings.edgeScroll && !leftDown && !middleDown);
            if (followUnit != NULL_ENTITY && !followUnitCamera(world, cam, followUnit)) followUnit = NULL_ENTITY;
            cam.updateClearance(terrain);

            // Fighting first, so a unit that dies this frame is gone before the others move. One sound covers the
            // swings of the frame: a hit if any landed, otherwise a miss if any were missed or dodged.
            factionSystem(world, dt);   // who is hostile to your units, from the reputations and provocations
            defendSystem(world);   // defenders pick the units attacking what they defend, before the fight
            combatSystem(world, rng, dt, gameLog, floatingNumbers);
            patrolSpawnSystem(world, terrain, dt);   // a mob gone too far from its spawn gives up and walks home
            // Behaviors after the fight, so a unit hit this frame can drink in the same frame
            behaviorSystem(world, gameLog, dt);
            // Anyone left at 0 health falls Unconscious
            downedSystem(world);
            // A log scrolled up keeps its place as lines arrive. One at its newest line follows them.
            const size_t logCount = gameLog.lines.size();
            if (logCount > logSeen && logOffset > 0) logOffset += (int)(logCount - logSeen);
            logSeen = logCount;
            squadSystem(world, dt, runMode);
            steeringSystem(world, terrain, dt, runMode);
            pickupSystem(world, terrain);   // a pickup plays its own sound
            // Units that reached the prop they were sent to. Inventory opens the panel. Use says if the bed was taken, the
            // prop had all its workers, or a campfire had nothing to cook or no room.
            for (const PropArrival& a : propOrderSystem(world)) {
                if (a.automatic) continue;   // an automated behavior's step: nothing to tell you, and no panel to open
                const Prop* p = world.get<Prop>(a.prop);
                if (!p) continue;
                const PropInfo& info = propInfo(p->kind);
                if (a.locked || a.craft == WorkStart::Locked) {
                    refuse(lang("MISC_PROP_LOCKED"));   // locked: it can't be opened or used until it is unlocked
                    continue;
                }
                if (a.action == PropAction::Unlock) {
                    if (a.craft == WorkStart::SkillTooLow) {
                        const Lock* lock = world.get<Lock>(a.prop);
                        refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock ? lock->level : 1) }));
                    } else if (a.craft == WorkStart::Full) {
                        refuse(lang("MISC_PROP_TAKEN"));
                    }
                    continue;
                }
                if (a.action == PropAction::Lock) {
                    if (a.started) playSfx(Sfx::UiToggle);
                    continue;
                }
                if (a.action == PropAction::Inventory) {
                    if (!(storageProp == a.prop && storageUnit == a.unit)) {
                        closeStorage();
                        storageProp = a.prop;
                        storageUnit = a.unit;
                        itemMenu.open = false;
                    }
                    continue;
                }
                if (a.corpse) {
                    refuse(lang("MISC_CORPSE_NO_PLACE"));
                    continue;
                }
                if (a.placed) continue;
                if (a.unreachable) {
                    refuse(lang("MISC_PROP_UNREACHABLE"));
                } else if (a.taken && info.use == PropUse::Occupy) {
                    refuse(lang("MISC_PROP_TAKEN"));
                } else if (a.taken) {
                    refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
                } else if (info.use == PropUse::Crafting && a.craft == WorkStart::NoInput) {
                    refuse(langf("MISC_CRAFT_NEEDS", { info.input == INPUT_COOKABLE ? std::string(lang("MISC_FOOD_TO_COOK"))
                                                                                         : itemNameOf(info.input) }));
                }
            }
            // Units with automated behaviors (a camp's woodcutter) take their next step: to a tree, or to the vault with
            // a load
            wildMobSystem(world, terrain, dt);   // wild mobs keep to their temperament: the Avoidant ones run off
            automationSystem(world, terrain, dt, rng);
            // Picking up, putting down and carrying Unconscious units; looters that got there open the panel
            const CarryEvents carried = carrySystem(world);
            for (const auto& [looter, looted] : carried.loot) {
                closeStorage();
                lootUnit = looted;
                lootBy = looter;
                playSfx(Sfx::UiMenuOpen);
            }
            cleanupActiveSquads(world);
            // Open worlds: read in the ground near the camera and the units, a few chunks each frame
            if (terrain.streamed()) terrain.stream(streamFocuses(world, cam), STREAM_RADIUS, STREAM_LOADS_PER_FRAME);
            // The in-game clock runs while it is not stopped. Hunger ticks once for each in-game hour it passes,
            // and health comes back by the in-game seconds that passed.
            int hoursPassed = 0;
            double gameSecs = 0.0;
            if (sky.clockRunning) {
                gameSecs = dt * GAME_SECONDS_PER_REAL_SECOND;
                hoursPassed = advanceClock(gs.clockSeconds, dt);
            }
            needsSystem(world, hoursPassed, gameSecs);
            if (hoursPassed > 0) forgetSpotted(world);   // each in-game hour, spotting a unit gives Perception XP again
            downedSystem(world);   // starving to 0 knocks a unit out too
            // Doom: up while Unconscious, down in a bed or bedroll; 0 there wakes the unit, 100 makes it a corpse
            const DoomChanges doom = doomSystem(world, gameSecs);
            playDeadSystem(world);
            for (Entity u : doom.woke) {
                tradeLabels.push_back(TradeLabel{ u, langf("MISC_EFFECT_LABEL", { lang("UILABEL_HEALTH"), "+" + std::to_string(RECOVERY_HEALTH) }), 0.0f });
                queueSoundFrom(world, Sfx::Recovers, u);
            }
            for (Entity u : doom.died) queueSoundFrom(world, Sfx::Dies, u);
            // Resource and crafting props: the units working them make items by the in-game time that passed
            // Each item a worker receives shows over it, as "+Iron Ore", like a trade's label
            for (const WorkGain& g : workSystem(world, gameSecs, rng)) {
                switch (g.kind) {
                case WorkGain::Kind::Item:
                    tradeLabels.push_back(TradeLabel{ g.unit, itemGainLabel(1, itemNameOf(g.item)), 0.0f });
                    // Each prop has its own sound for an item finished
                    switch (g.propKind) {
                    case PropKind::Ore: queueSoundFrom(world, Sfx::DoneOre, g.unit); break;
                    case PropKind::Tree: queueSoundFrom(world, Sfx::DoneTree, g.unit); break;
                    case PropKind::Mushroom: queueSoundFrom(world, Sfx::DoneMushroom, g.unit); break;
                    case PropKind::Stove: queueSoundFrom(world, Sfx::DoneCooking, g.unit); break;
                    case PropKind::WaterPump: queueSoundFrom(world, Sfx::DoneWater, g.unit); break;
                    default: queueSoundFrom(world, Sfx::ItemMove, g.unit); break;
                    }
                    break;
                case WorkGain::Kind::Unlocked:
                    tradeLabels.push_back(TradeLabel{ g.unit, lang("MISC_UNLOCKED"), 0.0f });
                    queueSoundFrom(world, Sfx::Unlocked, g.unit);
                    break;
                case WorkGain::Kind::LockpickFailed:
                    tradeLabels.push_back(TradeLabel{ g.unit, lang("MISC_LOCKPICK_FAILED"), 0.0f });
                    queueSoundFrom(world, Sfx::LockpickFailed, g.unit);
                    break;
                case WorkGain::Kind::Trained:
                    queueSoundFrom(world, Sfx::DoneTraining, g.unit);
                    tradeLabels.push_back(TradeLabel{ g.unit, langf("MISC_XP_GAINED", { std::to_string((int)std::lround(g.xp)), statName((StatId)g.item) }), 0.0f });
                    break;
                }
            }
            // Passive crafting props (the forge) burn their fuel and fill their output by themselves
            passiveCraftSystem(world, gameSecs);
            // Flags and foundations wear when their vault is dry; what is worn through breaks into debris
            claimDecaySystem(world, gameSecs);
            // While a prop is worked, its working sound now and then: the pick on the ore, the axe on the tree, the
            // lockpick in the lock. Only props near the camera are heard, and each kind of sound plays at most once
            // at a time.
            if (sky.clockRunning) {
                std::map<Entity, float> nextTicks;
                for (Entity e : world.view<PropWork, Prop, Transform>()) {
                    const PropWork* pw = world.get<PropWork>(e);
                    if (pw->workers <= 0) continue;
                    const Transform* pt = world.get<Transform>(e);
                    if (std::hypot(pt->x - cam.focusX, pt->z - cam.focusZ) > WORK_SOUND_RANGE) continue;
                    const PropKind kind = world.get<Prop>(e)->kind;
                    bool picking = false;
                    for (Entity u : world.view<Working>()) {
                        const Working* wk = world.get<Working>(u);
                        if (wk->prop == e && wk->unlocking) picking = true;
                    }
                    Sfx sound = Sfx::Count;
                    if (picking) sound = Sfx::WorkLockpick;
                    else if (kind == PropKind::Ore) sound = Sfx::WorkMining;
                    else if (kind == PropKind::Tree) sound = Sfx::WorkWoodcutting;
                    else if (kind == PropKind::Mushroom) sound = Sfx::WorkHerbalism;
                    else if (kind == PropKind::Campfire || kind == PropKind::Stove) sound = Sfx::WorkCooking;
                    else if (kind == PropKind::WaterPump) sound = Sfx::WorkPump;
                    else if (kind == PropKind::TrainingDummy) sound = Sfx::WorkTraining;
                    else if (kind == PropKind::Forge) sound = Sfx::ForgeBurning;
                    if (sound == Sfx::Count) continue;
                    const float every = sound == Sfx::ForgeBurning ? FORGE_SOUND_SECONDS : WORK_SOUND_SECONDS;
                    float t = workSoundTimers.count(e) ? workSoundTimers[e] : every * 0.5f;
                    t += dt;
                    if (t >= every) {
                        t = 0.0f;
                        queueSoundFrom(world, sound, e);
                    }
                    nextTicks[e] = t;
                }
                workSoundTimers.swap(nextTicks);
            }

            // Autosave every N minutes of play
            autoTimer += dt;
            if (settings.autosaveMinutes > 0 && autoTimer >= settings.autosaveMinutes * 60.0f) {
                autoTimer = 0.0f;
                if (autosaveNow()) {
                    showNotice(lang("MISC_AUTOSAVED"));
                    playSfx(Sfx::UiSaved);
                }
            }
        }

        // The storage panel closes once its unit is out of reach of the prop, is downed, or either is gone, or when the
        // selection becomes one unit other than the panel's. A how-many prompt for it closes with it.
        if (storageProp != NULL_ENTITY) {
            const Needs* sn = world.get<Needs>(storageUnit);
            const std::vector<Entity> picked = selectedUnits(world);
            if (!world.has<Storage>(storageProp) || !world.has<Unit>(storageUnit) ||
                (sn && sn->hp <= 0) || gapBetween(world, storageUnit, storageProp) > STORAGE_CLOSE_RANGE ||
                (picked.size() == 1 && picked[0] != storageUnit)) {
                closeStorage();
            }
        }
        if (quantity.open && quantity.toStorage && storageProp == NULL_ENTITY) quantity.open = false;
        // The looting panel closes once its looter is out of reach, is knocked out, or either is gone, or the unit it
        // shows wakes up
        if (lootUnit != NULL_ENTITY) {
            if (!world.has<Unconscious>(lootUnit) || !world.has<Unit>(lootBy) || world.has<Unconscious>(lootBy) ||
                gapBetween(world, lootBy, lootUnit) > LOOT_RANGE) {
                closeLoot();
            }
        }
        // A unit of your squad is always selected while there is one: when none is (a new game, the selected unit
        // died), the first of the squad is (firstSquadUnit), the one the camera starts on. With none left, nobody is.
        if (screen == Screen::Playing && selectedUnits(world).empty()) {
            const Entity first = firstSquadUnit(world);
            if (first != NULL_ENTITY) world.add(first, Selected{});
        }
        if (inspectUnit != NULL_ENTITY && !world.has<Unit>(inspectUnit)) inspectUnit = NULL_ENTITY;
        // A unit that is not in your squad shows only its Profile
        if (!unitTabEnabled(world, sidebarUnitOf(world), unitTab)) unitTab = UnitTab::Profile;
        // The right-click menu closes if its target is gone or a talk or trade opens
        if (contextMenu.open && ((!world.has<Unit>(contextMenu.target) && !world.has<Prop>(contextMenu.target)) ||
                                 dialogueUnit != NULL_ENTITY)) {
            contextMenu.open = false;
        }

        // ---- Sounds the systems asked for this frame. The plain item sound gives way to any more particular one. ----
        {
            // A world sound is as loud as its nearest copy this frame, and is panned to its side of the screen. A
            // frame of fighting plays one swing sound: a hit if any landed, else a miss.
            const std::vector<QueuedSound> queued = takeQueuedSounds();
            std::array<float, (size_t)Sfx::Count> volume{}, pan{};
            volume.fill(-1.0f);
            float ex = 0.0f, ey = 0.0f, ez = 0.0f;
            cam.eye(ex, ey, ez);
            const Mat4 vp = cam.viewProj((float)winW / (float)std::max(1, winH));
            for (const QueuedSound& q : queued) {
                if (!settings.craftGatherSounds && isCraftGatherSfx(q.sfx)) continue;
                float v = 1.0f, p = 0.0f;
                if (q.placed) {
                    v = soundGain(std::sqrt((q.x - ex) * (q.x - ex) + (q.y - ey) * (q.y - ey) + (q.z - ez) * (q.z - ez)));
                    const float cx = vp.m[0] * q.x + vp.m[4] * q.y + vp.m[8] * q.z + vp.m[12];
                    const float cw = vp.m[3] * q.x + vp.m[7] * q.y + vp.m[11] * q.z + vp.m[15];
                    p = cw > 1e-4f ? soundPan(cx / cw) : 0.0f;   // behind the eye: centred
                }
                const size_t i = (size_t)q.sfx;
                if (i < volume.size() && v > volume[i]) {
                    volume[i] = v;
                    pan[i] = p;
                }
            }
            auto heard = [&](Sfx s) { return volume[(size_t)s] >= 0.0f; };
            const bool particular = heard(Sfx::Equip) || heard(Sfx::Unequip) || heard(Sfx::Drop) || heard(Sfx::Pickup) ||
                                    heard(Sfx::Store) || heard(Sfx::Take) || heard(Sfx::GearChanged) || heard(Sfx::Eat) ||
                                    heard(Sfx::Drink) || heard(Sfx::Poison);
            if (heard(Sfx::Hit)) volume[(size_t)Sfx::Miss] = -1.0f;
            if (particular) volume[(size_t)Sfx::ItemMove] = -1.0f;
            for (size_t i = 0; i < volume.size(); ++i) {
                if (volume[i] >= 0.0f) playSfx((Sfx)i, volume[i], pan[i]);
            }
            // Units selected: a sound when the selection changes to some units
            size_t selKey = 0, selCount = 0;
            for (Entity u : world.view<Selected>()) {
                selKey = selKey * 31u + (size_t)u + 1u;
                ++selCount;
            }
            if (screen == Screen::Playing && selKey != lastSelectionKey && selCount > 0) playSfx(Sfx::UiSelect);
            lastSelectionKey = selKey;
            // (A unit falling Unconscious plays its own sound, from knockOut)
        }

        // ---- HUD: shown in the window title, since there's no text rendering there ----
        std::string title = gameTitle() + "   " + formatDate(gs.clockSeconds);
        if (title != lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastTitle = title;
        }

        // ---- Hover list: what is under the mouse, in the top-left corner. Units first (white), then items on the
        // ground (orange-yellow), then props (gray), up to the limit in all, then "..." ----
        std::vector<HudEntry> hud;
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);
        renderer.setMouse(mouseX, mouseY);
        if (screen == Screen::Playing) {
            std::vector<HudEntry> under;
            for (Entity u : renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH))
                under.push_back({ world.get<Name>(u)->text, HUD_UNIT_COLOR });

            std::vector<Entity> items = renderer.groundItemsAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
            // While Alt shows the name boxes, the name line under the mouse counts too
            if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                const Entity labelled = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
                if (labelled != NULL_ENTITY && std::find(items.begin(), items.end(), labelled) == items.end())
                    items.insert(items.begin(), labelled);
            }
            for (Entity e : items) {
                const std::string name = itemNameOf(world.get<GroundItem>(e)->item);
                under.push_back({ name.empty() ? std::string(lang("MISC_UNKNOWN_ITEM")) : name, HUD_ITEM_COLOR });
            }

            for (Entity e : renderer.propsAtPixel(world, cam, mouseX, mouseY, winW, winH)) {
                // A prop locked against the player says so: "Chest (Locked)". The player's own, or one in a player
                // claim, does not.
                const std::string name = lang(propInfo(world.get<Prop>(e)->kind).nameKey);
                under.push_back({ lockShutFor(world, e, NULL_ENTITY) ? langf("MISC_LOCKED_NAME", { name }) : name, HUD_PROP_COLOR });
                // A flag also shows its faction, health and vault
                for (const std::string& line : flagInfoLines(world, e)) under.push_back({ line, HUD_PROP_COLOR });
            }

            for (size_t i = 0; i < under.size() && i < HOVER_NAME_LIMIT; ++i) hud.push_back(under[i]);
            if (under.size() > HOVER_NAME_LIMIT) hud.push_back({ "...", HUD_UNIT_COLOR });
        }

        // ---- Build placement: while a prop is picked, its footprint follows the mouse over the ground. Holding R,
        // the mouse's left and right movement turns it. The preview shows exactly what a click would do. ----
        {
            // A kind that needs a claim is put down once no selected unit of yours stands in one
            const bool inClaim = selectionInClaim(world);
            if (buildPick.any() && !buildAnywhere(buildPick) && !inClaim) buildPick = {};
            renderer.setBuildInClaim(inClaim);
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float bmx = 0.0f, bmy = 0.0f;
            SDL_GetMouseState(&bmx, &bmy);
            const bool placing = buildPick.placing() && screen == Screen::Playing && !panelOpen && !mapOpen
                                 && dialogueUnit == NULL_ENTITY && !optimizeAllOpen && disbandTarget == NULL_ENTITY;
            if (placing && keys[SDL_SCANCODE_R] && buildPrevX >= 0.0f) buildYaw += (bmx - buildPrevX) * BUILD_TURN_PER_PIXEL;
            buildPrevX = bmx;
            BuildPreview bp;
            if (placing && !overPanels(bmx, bmy, winW, winH)) {
                if (auto hit = pickGround(cam, terrain, bmx, bmy, winW, winH)) {
                    bp.on = true;
                    bp.kind = buildPick.kind;
                    bp.yaw = buildYaw;
                    snapBuildSpot(buildPick.kind, hit->first, hit->second, bp.x, bp.z);
                    bp.valid = checkBuildSpot(world, terrain, buildPick.kind, bp.x, bp.z, buildYaw) == BuildCheck::Ok;
                }
            }
            if (bp.on && buildPick.places(PropKind::Flag)) {
                bp.ringR = CLAIM_RADIUS;
                bp.ringTone = bp.valid ? 1 : 2;
            }
            Entity hoverFlag = NULL_ENTITY;
            // Not placing anything: an existing flag under the mouse shows its claim circle
            if (!placing && screen == Screen::Playing && !panelOpen && !mapOpen && !overPanels(bmx, bmy, winW, winH)) {
                for (Entity e : renderer.propsAtPixel(world, cam, bmx, bmy, winW, winH)) {
                    const Prop* fp = world.get<Prop>(e);
                    const Transform* ft = world.get<Transform>(e);
                    if (!fp || !ft || fp->kind != PropKind::Flag) continue;
                    bp.ringR = CLAIM_RADIUS;
                    bp.ringTone = 3;
                    bp.x = ft->x;
                    bp.z = ft->z;
                    hoverFlag = e;
                    break;
                }
            }
            // When one claim circle shows, every flag's circle shows with it
            if (bp.ringR > 0.0f) {
                for (Entity e : world.view<Prop, Transform>()) {
                    if (e == hoverFlag || world.get<Prop>(e)->kind != PropKind::Flag) continue;
                    const Transform* ft = world.get<Transform>(e);
                    bp.otherRings.push_back({ ft->x, ft->z });
                }
            }
            renderer.setBuildPreview(bp);
            renderer.setBuildSelected(buildPick);
        }

        // ---- Render ----
        renderer.setDayMinutes(timeOfDayMinutes(gs.clockSeconds));
        renderer.setSky(sky);
        auto drawButtons = [&](const std::vector<Button>& buttons) {
            for (const Button& b : buttons) {
                renderer.drawButton(b.rect, b.label, b.enabled, b.rect.contains(mouseX, mouseY));
            }
        };
        // The title and pause menus' items. A new screen lights its first usable item, and its items slide in one after
        // another. Moving the mouse onto an item lights it.
        auto drawMenuItems = [&](Screen which, const std::vector<Button>& buttons) {
            const unsigned long long now = SDL_GetTicks();
            if (menuScreen != which) {
                menuScreen = which;
                menuShownAt = now;
                menuSel = -1;
                for (size_t i = 0; i < buttons.size(); ++i) {
                    if (buttons[i].enabled) { menuSel = (int)i; break; }
                }
            }
            const bool moved = mouseX != menuMouseX || mouseY != menuMouseY;
            menuMouseX = mouseX;
            menuMouseY = mouseY;
            for (size_t i = 0; i < buttons.size(); ++i) {
                if (moved && buttons[i].enabled && buttons[i].rect.contains(mouseX, mouseY)) menuSel = (int)i;
            }
            const float since = (float)(now - menuShownAt) / 1000.0f;
            for (size_t i = 0; i < buttons.size(); ++i) {
                const Button& b = buttons[i];
                const float appear = std::clamp((since - (0.055f * (float)i + 0.1f)) / 0.34f, 0.0f, 1.0f);
                const std::string sub = b.action == Action::Language ? languageDisplayName() : std::string();
                renderer.drawMenuItem(b.rect, b.label, sub, (int)i == menuSel && b.enabled, b.enabled, appear);
            }
        };
        // Notices sit at the top middle of the window
        auto drawNotice = [&]() {
            if (noticeSec > 0.0f) renderer.drawNotice(notice, winW, winH);
        };
        auto dim = [&](float alpha) {
            renderer.drawRect(Rect{ 0.0f, 0.0f, (float)winW, (float)winH }, RGBA{ 0.0f, 0.0f, 0.0f, alpha });
        };

        if (screen != Screen::Title && screen != Screen::Pause) menuScreen = screen;   // the next menu opens fresh
        renderer.setMenuText(screen != Screen::Playing);
        renderer.hoverFrameStart();
        switch (screen) {
        case Screen::Title:
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            {
                // The game's name and the notice board, top left; the menu down the right; along the bottom, the build
                // stamp and the keys
                const TitleLayout tl = layoutTitle(winW, winH);
                renderer.drawTitleHeader(tl, lang("GAME_TITLE_FIRST"), lang("GAME_TITLE_SECOND"),
                                         messageOfTheDay());
                drawMenuItems(Screen::Title, layoutButtons(Screen::Title, winW, winH, saves));
                renderer.drawTitleFooter(tl.footer, BUILD_STAMP, lang("MISC_TITLE_KEYS"));
            }
            renderer.endUi();
            break;

        case Screen::Settings: {
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            drawSettingsMenu(renderer, layoutSettings(winW, winH, rows, settingsUi), rows, settingsUi, mouseX, mouseY);
            renderer.endUi();
            break;
        }

        case Screen::Playing: {
            // The world map's view starts out showing the whole level, and starts over each time it opens
            if (!mapOpen) {
                mapSpan = 0.0f;
            } else {
                const float size = (float)terrain.size();
                const Rect box = worldMapFor(winW, winH).map;
                if (mapSpan <= 0.0f) {
                    mapSpan = mapMaxSpan(box, size);
                    mapCx = size * 0.5f;
                    mapCz = size * 0.5f;
                }
                // From one chunk across to the whole level
                clampMapView(box, size, mapCx, mapCz, mapSpan);
            }
            renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH);
            renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
            renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
            // In-game panels, drawn over the world: item names while Alt is held, the unit list along the
            // bottom, the inventory of a lone selected unit, and the stack being dragged under the mouse
            renderer.beginUi(winW, winH);
            // The world map's marks: every prop of any kind as a purple square, every player unit as a green dot and
            // the other units as red or gray dots
            std::vector<MapDot> mapDots;
            for (Entity e : world.view<Prop, Transform>()) {
                const Transform* t = world.get<Transform>(e);
                mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, MapDotKind::Prop });
            }
            for (Entity e : world.view<Unit, Transform>()) {
                const Transform* t = world.get<Transform>(e);
                MapDotKind kind = MapDotKind::Player;
                if (world.has<Hostile>(e)) kind = MapDotKind::Hostile;
                else if (world.has<Neutral>(e)) kind = MapDotKind::Neutral;
                else if (!world.has<Commandable>(e)) continue;
                mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, kind });
            }
            if (settings.showPropNames) renderer.drawPropNames(world, cam, winW, winH, settings.showPropInfo);
            if (settings.showUnitNames || settings.showNpcNames) {
                renderer.drawUnitNames(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
            }
            // What each unit is doing: mining, woodcutting, cooking, sleeping, encumbered
            renderer.drawEmotes(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
            renderer.drawPropEmotes(world, cam, winW, winH, settings.showPropNames);
            // What a right-click would do to the unit under the pointer: put down a unit your selected unit carries, or
            // talk to an NPC
            if (!overPanels(mouseX, mouseY, winW, winH) && !contextMenu.open && !itemMenu.open && dialogueUnit == NULL_ENTITY) {
                for (Entity u : renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH)) {
                    const Carried* cd = world.get<Carried>(u);
                    const bool names = world.has<Commandable>(u) ? settings.showUnitNames : settings.showNpcNames;
                    if (cd && world.has<Selected>(cd->by)) {
                        renderer.drawHoverCue(world, cam, winW, winH, u, EmoteId::CuePutDown, names);
                        break;
                    }
                    if (talkable(u)) {
                        renderer.drawHoverCue(world, cam, winW, winH, u, EmoteId::CueTalk, names);
                        break;
                    }
                }
            }
            // Damage numbers rise over the units they hit
            renderer.drawFloatingNumbers(floatingNumbers, cam, winW, winH);
            // Labels the systems queued this frame: items gained, and what used items did
            for (const TradeLabel& l : takeQueuedLabels()) tradeLabels.push_back(l);
            for (TradeLabel& l : tradeLabels) l.age += dt;
            tradeLabels.erase(std::remove_if(tradeLabels.begin(), tradeLabels.end(),
                                             [](const TradeLabel& l) { return l.age >= TRADE_LABEL_LIFE; }),
                              tradeLabels.end());
            renderer.drawTradeLabels(tradeLabels, world, cam, winW, winH);
            renderer.drawPropProgress(world, cam, winW, winH, settings.showPropNames);
            if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
            } else if (!overPanels(mouseX, mouseY, winW, winH) && !contextMenu.open && !itemMenu.open) {
                // The item under the pointer (its visible part) shows its name over it; a click picks it up
                const Entity hovered = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH, false);
                if (hovered != NULL_ENTITY) renderer.drawGroundItemName(world, cam, winW, winH, hovered);
            }
            {
                const std::vector<Entity> own = ownUnitList();
                const UnitPanelLayout L = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
                unitPanelRow = L.scrollRow;
                renderer.drawUnitPanel(world, L, own, runMode, repView);
                drawPanelLock(PanelId::Squad);
                // The Information Panel, top left. Its offset is written back, so it stays in range.
                const GameLogLayout cl = gameLogPanel(winW, winH);
                logOffset = cl.offset;
                InfoTime timeInfo;
                if (infoTab == 2) {
                    timeInfo.clock = formatTime(gs.clockSeconds);
                    timeInfo.date = formatDate(gs.clockSeconds);
                    timeInfo.ymd = formatYmd(gs.clockSeconds);
                    timeInfo.days = langf("UILABEL_DAYS_PASSED", { std::to_string(dayIndexOf(gs.clockSeconds)) });
                }
                renderer.drawGameLog(cl, infoLines(gameLog, infoTab, logFilter), logFilter, mouseX, mouseY, timeInfo);
                if (!cl.filterOpen) drawPanelLock(PanelId::Info);   // the open filter menu covers it
                // The sidebar shows the lone selected unit, yours or a red one. A red one's gear and inventory
                // are only shown: the mouse handling never starts a drag or a menu on them.
                const Entity shown = sidebarUnitOf(world);
                {
                    // The item being dragged, so the gear slots can show where it fits
                    int draggedId = 0;
                    if (itemDrag && dragGear >= 0) {
                        const Equipment* worn = world.get<Equipment>(dragUnit);
                        if (worn) draggedId = worn->items[dragGear];
                    } else if (itemDrag) {
                        const Inventory* inv = world.get<Inventory>(dragUnit);
                        if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS) draggedId = inv->slots[dragSlot].item;
                    }
                    const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                    sidebarScroll = sl.scroll;   // kept within range as the list grows and shrinks
                    renderer.drawInventorySidebar(sl, world, shown, mouseX, mouseY,
                                                  itemDrag && dragGear < 0 ? dragSlot : -1,
                                                  itemDrag && dragGear >= 0 ? dragGear : -1,
                                                  draggedId, shownIsYours());
                    // What the behavior under the mouse does, when nothing else is open over it
                    if (shown != NULL_ENTITY && !itemDrag) renderer.drawBehaviorTip(sl, world, shown, mouseX, mouseY);
                }
                drawPanelLock(PanelId::Unit);
                if (itemMenu.open) {
                    const std::vector<std::string> labels = menuLabels();
                    if (!labels.empty()) {
                        renderer.drawItemMenu(renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH),
                                              labels, mouseX, mouseY);
                    }
                }
                if (itemDrag) {
                    // The item under the mouse: a gear item, or a stack from the inventory
                    if (dragGear >= 0) {
                        const Equipment* worn = world.get<Equipment>(dragUnit);
                        if (worn && worn->items[dragGear] != 0) renderer.drawDraggedStack(mouseX, mouseY, worn->items[dragGear], 1);
                    } else {
                        const Inventory* inv = world.get<Inventory>(dragUnit);
                        if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS && inv->slots[dragSlot].item != 0) {
                            renderer.drawDraggedStack(mouseX, mouseY, inv->slots[dragSlot].item, inv->slots[dragSlot].count);
                        }
                    }
                }
            }
            // The looting panel over an Unconscious unit or a corpse
            if (lootUnit != NULL_ENTITY) {
                const std::string who = world.has<Corpse>(lootUnit) ? langf("UILABEL_CORPSE_OF", { nameOf(world, lootUnit) })
                                                                   : nameOf(world, lootUnit);
                renderer.drawLoot(layoutLoot(winW, winH), who, world.get<Equipment>(lootUnit), world.get<Inventory>(lootUnit),
                                  mouseX, mouseY);
            }
            // The storage panel, and the item being dragged out of it
            if (storageProp != NULL_ENTITY) {
                const Storage* st = world.get<Storage>(storageProp);
                const Prop* sp = world.get<Prop>(storageProp);
                const PropInfo& sinfo = propInfo(sp ? sp->kind : PropKind::Wagon);
                std::string hint, status;
                if (sinfo.use == PropUse::Crafting) {
                    hint = langf("UILABEL_CRAFT_HINT", { propNameOf(world, storageProp) });
                } else if (sinfo.use == PropUse::PassiveCrafting) {
                    if (sinfo.input == INPUT_COOKABLE) {   // the campfire: any raw meat, cooked
                        hint = langf("UILABEL_PASSIVE_HINT", { lang("MISC_FOOD_TO_COOK"),
                                                               std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                               lang("MISC_COOKED_FOOD") });
                    } else if (sinfo.item == 0) hint = langf("UILABEL_UPKEEP_HINT", { itemNameOf(sinfo.input) });
                    else hint = langf("UILABEL_PASSIVE_HINT", { itemNameOf(sinfo.input),
                                                           std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                           itemNameOf(sinfo.item) });
                    const Fuel* fuel = world.get<Fuel>(storageProp);
                    switch (passiveStateOf(world, storageProp)) {
                    case PassiveState::OutputFull: status = lang("MISC_OUTPUT_FULL"); break;
                    case PassiveState::NoFuel: status = lang(sinfo.item == 0 ? "MISC_NO_UPKEEP" : "MISC_NO_FUEL"); break;
                    case PassiveState::Burning: {
                        const std::string mins = std::to_string((int)std::ceil((fuel ? fuel->hours : 0.0) * 60.0 - 1e-6));
                        status = langf(sinfo.item == 0 ? "MISC_UPKEEP_LEFT" : "MISC_FUEL_LEFT", { mins });
                        break;
                    }
                    }
                } else if (sinfo.accepts != ItemFilter::Any) {
                    hint = langf("UILABEL_STORAGE_TAKES", { lang(itemFilterKey(sinfo.accepts)) }) + " " + lang("UILABEL_STORAGE_HINT");
                } else {
                    hint = lang("UILABEL_STORAGE_HINT");
                }
                const Lock* ownLock = ownedLockOf(world, storageProp) ? world.get<Lock>(storageProp) : nullptr;
                renderer.drawStorage(storageLayout(world, winW, winH), propNameOf(world, storageProp), hint, st,
                                     world.get<Output>(storageProp), status, storageDrag, mouseX, mouseY,
                                     ownLock ? lang(ownLock->locked ? "UILABEL_UNLOCK" : "UILABEL_LOCK") : std::string());
                if (const ItemStack* dragged = storageSlot(world, storageDrag); dragged && dragged->item != 0) {
                    renderer.drawDraggedStack(mouseX, mouseY, dragged->item, dragged->count);
                }
                if (quantity.open && quantity.toStorage) {
                    renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                quantity.available, mouseX, mouseY);
                }
            }
            if (contextMenu.open) renderer.drawItemMenu(contextMenuLayout(), contextLabels(), mouseX, mouseY);
            // The Disband popout: the hold fills Disband's bar while the button is held and the pointer stays on it;
            // full, the unit is disbanded. A unit no longer yours (it died, or was disbanded) shuts it.
            if (disbandTarget != NULL_ENTITY && !world.has<Commandable>(disbandTarget)) {
                disbandTarget = NULL_ENTITY;
                disbandHeldAt = 0;
            }
            if (disbandTarget != NULL_ENTITY) {
                const DisbandLayout dl = layoutDisband(winW, winH);
                float held = 0.0f;
                if (disbandHeldAt != 0) {
                    if (!dl.disband.contains(mouseX, mouseY)) disbandHeldAt = 0;   // moved off it: starts over
                    else held = (float)(SDL_GetTicks() - disbandHeldAt) / (float)DISBAND_HOLD_MS;
                }
                if (held >= 1.0f) {
                    const Entity u = disbandTarget;
                    disbandTarget = NULL_ENTITY;
                    disbandHeldAt = 0;
                    // Panels that were the unit's shut with it
                    if (storageUnit == u) closeStorage();
                    if (lootBy == u) closeLoot();
                    if (dialoguePlayer == u) {
                        endTalk();
                        dialoguePlayer = NULL_ENTITY;
                    }
                    if (contextMenu.open && contextMenu.target == u) contextMenu.open = false;
                    const bool mob = world.has<Mob>(u);
                    const std::string who = nameOf(world, u);
                    if (disbandUnit(world, u)) {
                        showNotice(langf(mob ? "MISC_DISBANDED_MOB" : "MISC_DISBANDED", { who }));
                        playSfx(Sfx::UiOrder);
                    }
                } else {
                    renderer.drawDisband(world, dl, disbandTarget, held, mouseX, mouseY);
                }
            }
            if (optimizeAllOpen) {
                renderer.drawOptimizeAll(layoutOptimizeAll(winW, winH), (int)ownUnitList().size(), settings.optimizeAttackShare,
                                         { settings.optimizeAccessories, settings.optimizeTwoHanded, settings.optimizeValueTies },
                                         mouseX, mouseY);
            }
            // The world map popout is over everything, the panels included
            if (mapOpen) {
                WorldMapMarks marks;
                marks.camX = cam.focusX;
                marks.camZ = cam.focusZ;
                marks.camYaw = cam.yaw;
                marks.moveX = moveMarkX;
                marks.moveZ = moveMarkZ;
                marks.moveAlpha = std::clamp(moveMarkTime, 0.0f, 1.0f);   // the last second fades it out
                renderer.drawWorldMap(worldMapFor(winW, winH), mapDots, mapCx, mapCz, mapSpan, marks);
            }
            // The talk box, over everything. It ends if the unit it is with is gone.
            if (dialogueUnit != NULL_ENTITY) {
                if (!world.has<Unit>(dialogueUnit)) {
                    dialogueUnit = NULL_ENTITY;
                    dialogueConvo.clear();
                    tradeOpen = false;
                } else {
                    float mhx = 0.0f, mhy = 0.0f;
                    SDL_GetMouseState(&mhx, &mhy);
                    if (dialogueUnit == NULL_ENTITY) quantity.open = false;
                    if (tradeOpen) {
                        const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                        renderer.drawTrade(layoutTrade(winW, winH, renderer.pauseBarHeight(false)), nameOf(world, dialogueUnit), nameOf(world, dialoguePlayer),
                                           npcInv, tradeNpc, tradePlayer, tradeNpcDrag,
                                           checkTrade(world, dialogueUnit, dialoguePlayer), !freeTrade, mhx, mhy);
                        if (npcInv && tradeNpcDrag >= 0 && npcInv->slots[tradeNpcDrag].item != 0) {
                            renderer.drawDraggedStack(mhx, mhy, npcInv->slots[tradeNpcDrag].item, npcInv->slots[tradeNpcDrag].count);
                        }
                        if (quantity.open) {
                            renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                        quantity.available, mhx, mhy);
                        }
                    } else {
                        const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
                        renderer.drawDialogue(dialogueBox(winW, winH), dialogueConvo,
                                              dg ? dg->options : std::vector<DialogueOption>{}, mhx, mhy);
                    }
                }
            }
            // The PAUSED bar, over everything in the game and its popouts, while the game stands still
            if (paused || dialogueUnit != NULL_ENTITY) renderer.drawPauseBar(paused && dialogueUnit == NULL_ENTITY);
            renderer.endUi();
            }
            break;

        case Screen::MapSelect: {
            // The same popout as the load list: a name and date for each map, sorted, scrolled in a window
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.68f, 0.60f, 0.54f, 1.0f };
            const RGBA hover{ 0.961f, 0.788f, 0.459f, 1.0f };
            const RGBA hint{ 0.957f, 0.902f, 0.796f, 1.0f };
            const RGBA builtIn{ 1.0f, 0.62f, 0.62f, 1.0f };
            const float cx = g.box.x + g.box.w * 0.5f;

            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawThemedPanel(g.box);
            renderer.drawUiTextCentered(uiUpper(lang("UILABEL_NEW_GAME")), cx, g.box.y + g.pad, white);
            renderer.drawUiTextCentered(maps.empty() ? lang("UILABEL_NO_MAPS")
                                                     : lang("UILABEL_CLICK_MAP"),
                                        cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);

            // Column headers. The active sort column shows its direction.
            const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
            const float textInset = 12.0f * s;
            const float listH = renderer.listTextHeight("Ag");
            const float hy = g.headerY + (g.headerH - listH) * 0.5f;
            auto headerLabel = [&](const std::string& label, bool active) {
                if (!active) return label;
                return label + (mapSortAsc ? "  ^" : "  v");
            };
            renderer.drawListText(headerLabel(lang("UILABEL_NAME"), mapSortByName), innerX + textInset, hy,
                                  mapSortByName ? white : gray);
            renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
            renderer.drawListText(headerLabel(lang("UILABEL_CREATED"), !mapSortByName), innerX + COL_TIME * innerW, hy,
                                  !mapSortByName ? white : gray);

            // "..." rows: how many maps are hidden above and below the visible rows
            const size_t total = maps.size();
            const size_t shownFirst = std::min(mapOffset, total);
            const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
            if (shownFirst > 0) {
                const bool hot = g.aboveBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                      innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }
            if (shownLast < total) {
                const bool hot = g.belowBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                      innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }

            for (const Button& b : layoutMapPanel(winW, winH, maps, mapOffset)) {
                const bool isHover = b.rect.contains(mouseX, mouseY);
                if (b.action == Action::PickMap) {
                    // A map row: background, then its name, type and date in the columns
                    const MapChoice& m = maps[b.param];
                    renderer.drawButton(b.rect, "", true, isHover);
                    const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                    renderer.drawListText(m.name, b.rect.x + textInset, ry, isHover ? hover : white);
                    renderer.drawListText(mapTypeLabel(m), b.rect.x + COL_TYPE * b.rect.w, ry,
                                          (m.source == "data" || m.source == "packed") ? builtIn : white);
                    const std::string when = (m.source == "data" || m.source == "packed") ? std::string(lang("UILABEL_BUILT_IN_WHEN"))
                                           : m.created > 0 ? timeString(m.created) : std::string(lang("UILABEL_UNKNOWN"));
                    renderer.drawListText(when, b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                } else if (b.action == Action::MapScrollUp || b.action == Action::MapScrollDown
                           || b.action == Action::MapSortName || b.action == Action::MapSortTime) {
                    // Drawn above as text: the "..." rows and the headers only need their click areas
                    continue;
                } else {
                    drawButtons({ b });
                }
            }
            renderer.endUi();
            break;
        }

        case Screen::ScenarioSelect: {
            // The picked scenario's name and description at the top, the list of scenarios at the bottom
            const float s = winH / 720.0f;
            const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
            scenarioScroll = sl.first;
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
            const RGBA accent{ 1.0f, 0.78f, 0.30f, 1.0f };
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawThemedPanel(sl.box);
            renderer.drawListText(lang("UILABEL_SCENARIO"), sl.heading.x, sl.heading.y, gray);
            const std::string name = lang(scenarioNameKey(scenarioPick));
            renderer.drawUiTextInBox(name, Rect{ sl.name.x, sl.name.y, sl.name.w, sl.name.h }, accent);
            float ty = sl.desc.y;
            const float lh = renderer.listTextHeight("Ag");
            for (const std::string& ln : renderer.wrapListText(lang(scenarioDescKey(scenarioPick)), sl.desc.w)) {
                if (ty + lh > sl.desc.y + sl.desc.h + 0.5f) break;
                renderer.drawListText(ln, sl.desc.x, ty, white);
                ty += lh;
            }
            renderer.drawRect(Rect{ sl.list.x, sl.list.y - 8.0f * s, sl.list.w, 1.0f * s }, RGBA{ 0.588f, 0.196f, 0.259f, 1.0f });
            for (size_t i = 0; i < sl.rows.size(); ++i) {
                const ScenarioId id = (ScenarioId)(sl.first + (int)i);
                const Rect& r = sl.rows[i];
                renderer.drawButton(r, lang(scenarioNameKey(id)), true, r.contains(mouseX, mouseY));
                if (id == scenarioPick) {
                    // The picked one: a bar down its left edge and a frame
                    renderer.drawRect(Rect{ r.x, r.y, 5.0f * s, r.h }, accent);
                    renderer.drawRect(Rect{ r.x, r.y, r.w, 2.0f * s }, accent);
                    renderer.drawRect(Rect{ r.x, r.y + r.h - 2.0f * s, r.w, 2.0f * s }, accent);
                    renderer.drawRect(Rect{ r.x + r.w - 2.0f * s, r.y, 2.0f * s, r.h }, accent);
                }
            }
            if (sl.track.h > 0.0f) {
                renderer.drawRect(sl.track, RGBA{ 0.20f, 0.09f, 0.11f, 0.9f });
                renderer.drawRect(sl.thumb, RGBA{ 0.910f, 0.376f, 0.431f, 0.9f });
            }
            renderer.drawButton(sl.back, lang("UILABEL_BACK"), true, sl.back.contains(mouseX, mouseY));
            renderer.drawButton(sl.next, lang("UILABEL_CONTINUE"), true, sl.next.contains(mouseX, mouseY));
            renderer.endUi();
            break;
        }

        case Screen::PartySetup: {
            const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawPartySetup(layoutPartySetup(winW, winH, party, partyUi, drop), party, partyUi,
                                    partyPanels(winW, winH), mouseX, mouseY);
            renderer.endUi();
            break;
        }

        case Screen::Pause:
            // The world and its PAUSED bar as in the game, then the menu over them in the menu fonts
            renderer.setMenuText(false);
            renderer.drawFrame(world, terrain, cam, sel, {}, winW, winH);
            renderer.beginUi(winW, winH);
            dim(0.55f);
            renderer.drawPauseBar(false);
            renderer.setMenuText(true);
            drawMenuItems(Screen::Pause, layoutButtons(Screen::Pause, winW, winH, saves));
            renderer.endUi();
            break;
        }

        // ---- Save/load popout, drawn over whichever screen is showing ----
        if (panelOpen) {
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.68f, 0.60f, 0.54f, 1.0f };
            const RGBA hover{ 0.961f, 0.788f, 0.459f, 1.0f };
            const RGBA hint{ 0.957f, 0.902f, 0.796f, 1.0f };
            const float cx = g.box.x + g.box.w * 0.5f;

            renderer.beginUi(winW, winH);
            dim(0.25f);
            renderer.drawThemedPanel(g.box);
            renderer.drawUiTextCentered(uiUpper(panelSaveMode ? lang("UILABEL_SAVE_GAME") : lang("UILABEL_LOAD_GAME")), cx, g.box.y + g.pad, white);

            if (ask != Ask::None) {
                DialogGeometry d = questionGeometry(winW, winH);
                renderer.drawThemedPanel(d.box);
                if (ask == Ask::Overwrite) {
                    std::string who = pendingOverwrite ? pendingOverwrite->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_OVERWRITE_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s, white);
                } else if (ask == Ask::LoadUnsaved) {
                    std::string who = pendingLoad ? pendingLoad->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_LOAD_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered(lang("UILABEL_UNSAVED_WARNING"),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                } else {
                    // Delete: the name is shown so it's clear which save goes
                    const std::string who = pendingDelete ? pendingDelete->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_DELETE_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered(lang("UILABEL_HOLD_DELETE"),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                }
                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                                   panelFrom == Screen::Title)) {
                    if (b.action == Action::DeleteHold) {
                        const float fill = deleteHolding
                            ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS)
                            : 0.0f;
                        renderer.drawHoldButton(b.rect, b.label, fill);
                    } else {
                        drawButtons({ b });
                    }
                }
            } else {
                if (panelSaveMode) {
                    // Name field with a blinking cursor, shown even while the name is empty
                    // so it's clear the field has focus as soon as the popout opens
                    renderer.drawRect(g.nameBox, RGBA{ 0.14f, 0.06f, 0.075f, 1.0f });
                    float textY = g.nameBox.y + g.nameBox.h * 0.22f;
                    bool cursorOn = (SDL_GetTicks() / 500) % 2 == 0;
                    renderer.drawUiText(saveName + (cursorOn ? "|" : ""), g.nameBox.x + 12.0f, textY, white);
                } else {
                    renderer.drawUiTextCentered(lang("UILABEL_CLICK_SAVE"),
                                                cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);
                }

                // Column headers. The active sort column shows its direction.
                const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
                const float textInset = 12.0f * s;
                const float listH = renderer.listTextHeight("Ag");
                const float hy = g.headerY + (g.headerH - listH) * 0.5f;
                auto headerLabel = [&](const std::string& label, bool active) {
                    if (!active) return label;
                    return label + (sortAsc ? "  ^" : "  v");
                };
                renderer.drawListText(headerLabel(lang("UILABEL_NAME"), sortKey == SortKey::Name),
                                      innerX + textInset, hy, sortKey == SortKey::Name ? white : gray);
                renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
                renderer.drawListText(headerLabel(lang("UILABEL_TIME"), sortKey == SortKey::Time),
                                      innerX + COL_TIME * innerW, hy, sortKey == SortKey::Time ? white : gray);

                if (saves.empty()) {
                    renderer.drawUiTextCentered(lang("UILABEL_NO_SAVES"), cx, g.listTop + g.nameBox.h * 0.22f, gray);
                }

                // "..." rows: how many saves are hidden above and below the visible rows
                const size_t total = saves.size();
                const size_t shownFirst = std::min(listOffset, total);
                const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
                if (shownFirst > 0) {
                    const bool hot = g.aboveBand.contains(mouseX, mouseY);
                    renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                          innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                          hot ? hover : hint);
                }
                if (shownLast < total) {
                    const bool hot = g.belowBand.contains(mouseX, mouseY);
                    renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                          innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                          hot ? hover : hint);
                }

                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, Ask::None, saves, listOffset,
                                                   panelFrom == Screen::Title)) {
                    const bool isHover = b.rect.contains(mouseX, mouseY);
                    if (b.action == Action::LoadSlot) {
                        // A save row: background, then left-aligned name and the type and time columns
                        const SaveSummary& sv = saves[b.param];
                        renderer.drawButton(b.rect, "", true, isHover);
                        const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                        renderer.drawListText(sv.name, b.rect.x + textInset, ry, isHover ? hover : white);
                        renderer.drawListText(sv.autosave ? lang("UILABEL_AUTO") : lang("UILABEL_MANUAL"),
                                              b.rect.x + COL_TYPE * b.rect.w, ry,
                                              sv.autosave ? RGBA{ 1.0f, 0.62f, 0.62f, 1.0f } : white);
                        renderer.drawListText(timeString(sv.timestamp),
                                              b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                    } else if (b.action == Action::ScrollUp || b.action == Action::ScrollDown
                               || b.action == Action::SortName || b.action == Action::SortTime) {
                        // Drawn above as text: the "..." rows and the headers only need their click areas
                        continue;
                    } else if (b.action == Action::DeleteSave) {
                        renderer.drawListButton(b.rect, lang("UILABEL_DELETE"), isHover);
                    } else {
                        renderer.drawButton(b.rect, b.label, b.enabled, isHover);
                    }
                }
            }
            renderer.endUi();
        }

        renderer.beginUi(winW, winH);
        // The Behavior tab's # prompt, over the whole game
        if (percentPrompt.open) {
            if (screen != Screen::Playing || !world.has<Behaviors>(percentPrompt.unit)) {
                answerPercentPrompt(false);   // the game it was asked in is gone
            } else {
                const char* title = percentPrompt.kind == BehaviorKind::Eat    ? "UILABEL_BEHAVIOR_PROMPT_EAT"
                                    : percentPrompt.kind == BehaviorKind::Heal ? "UILABEL_BEHAVIOR_PROMPT_HEAL"
                                                                               : "UILABEL_BEHAVIOR_PROMPT_REST";
                const PercentRange range = percentRange(percentPrompt.kind);
                renderer.drawPercentPrompt(layoutPercentPrompt(winW, winH), lang(title), percentPrompt.text,
                                           langf("UILABEL_BEHAVIOR_PROMPT_RANGE", { std::to_string(range.lo), std::to_string(range.hi) }),
                                           percentPrompt.error, mouseX, mouseY);
            }
        }
        drawNotice();
        // The frame rate in the bottom-left corner, white on a black box, over everything else
        ++fpsFrames;
        if (const Uint64 at = SDL_GetTicks(); at - fpsSince >= 500) {
            fpsText = std::to_string((int)std::lround(fpsFrames * 1000.0 / (double)(at - fpsSince))) + " FPS";
            fpsSince = at;
            fpsFrames = 0;
        }
        if (settings.showFps) {
            const float pad = 4.0f, tw = renderer.uiTextWidth(fpsText), th = renderer.uiTextHeight(fpsText);
            const float bx = 6.0f, by = (float)winH - th - 2.0f * pad - 6.0f;
            renderer.drawRect(Rect{ bx, by, tw + 2.0f * pad, th + 2.0f * pad }, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f });
            renderer.drawUiText(fpsText, bx + pad, by + pad, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        }
        renderer.endUi();

        // The frame is complete but not yet shown, so it is the one saved
        if (screenshotPending) {
            screenshotPending = false;
            const std::string saved = saveScreenshot(window);
            showNotice(saved.empty() ? std::string(lang("MISC_SCREENSHOT_FAILED")) : langf("MISC_SCREENSHOT_SAVED", { saved }));
        }

        if (renderer.hoverFrameEnd()) playSfx(Sfx::UiHover);   // the pointer came onto another button
        SDL_GL_SwapWindow(window);
        // Holds the frame to the limit, counted from the frame's start
        limitFrameRate(frameStart, settings.fpsLimit);
    }

    // ---- Shutdown ----
    audio::shutdown();
    renderer.shutdown();
    renderer.setTitleFont(nullptr);
    closeFonts(nameFontOwned);
    closeUiFonts(fonts);
    closeUiFonts(menuFonts);
    closeFonts(titleFontOwned);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Log("Closed normally");
    SDL_Quit();
    return 0;
}
