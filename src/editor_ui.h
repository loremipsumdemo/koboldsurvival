#pragma once
#include <string>
#include <vector>
#include "level.h"
#include "ui_types.h"
#include "world_gen.h"

// The World Generation screen, and the level editor that follows it
inline constexpr int WORLD_GEN_NAME_CHARS = 32;

struct WorldGenRow {
    const char* labelKey;
    int WorldGenSettings::* value;
};
inline constexpr WorldGenRow WORLD_GEN_ROWS[] = {
    { "UILABEL_GEN_LAND", &WorldGenSettings::land },
    { "UILABEL_GEN_CONTINENTS", &WorldGenSettings::continents },
    { "UILABEL_GEN_WARP", &WorldGenSettings::warp },
    { "UILABEL_GEN_HILLS", &WorldGenSettings::hills },
    { "UILABEL_GEN_MOUNTAINS", &WorldGenSettings::mountains },
    { "UILABEL_GEN_MOISTURE", &WorldGenSettings::moisture },
    { "UILABEL_GEN_TEMPERATURE", &WorldGenSettings::temperature },
    { "UILABEL_GEN_FORESTS", &WorldGenSettings::forests },
};
inline constexpr int WORLD_GEN_ROW_COUNT = (int)(sizeof(WORLD_GEN_ROWS) / sizeof(WORLD_GEN_ROWS[0]));

// The Theme tab, the same on the World Generation screen and in the level editor
struct ThemeUi {
    int scroll = 0;
    int openColor = -1;   // the color row showing its red, green and blue bars
    int dragRow = -1;
    int dragChannel = -1;
};
struct ThemeLine {
    int row;       // into themeRows()
    int channel;   // -1: the row itself. 0 to 2: red, green, blue of a color row.
    Rect box, control;
};
struct ThemeLayout {
    Rect presetDown, presetBox, presetUp;
    std::vector<ThemeLine> lines;
    int total = 0, shown = 0, first = 0;
};
ThemeLayout layoutThemePanel(const Rect& area, const ThemeUi& ui, float scale);

struct WorldGenUi {
    WorldGenSettings settings;
    bool themeTab = false;
    std::string name;          // UILABEL_GEN_DEFAULT_NAME and the seed until typed or picked
    bool nameEdited = false;   // typed or picked, so a language change leaves it alone
    bool editingName = false;
    int dragRow = -1;
    bool previewDirty = true;
    unsigned long long previewAt = 0;
    bool generated = false;
    std::string generatedName;
    std::vector<std::string> maps;
    std::vector<std::string> playedMaps;   // lowercased names of the maps game saves are played on, once per save
    int mapScroll = 0;
    std::string deleteMap;                 // the map the delete question is asked about, empty when none is
    int deleteLevels = 0;                  // how many levels are made on it, counted when the question opens
};

struct WorldGenLayout {
    Rect panel, tabWorld, tabTheme, themeArea, nameBox, seedBox, seedRandom, sizeDown, sizeUp, sizeBox, styleDown, styleUp, styleBox, styleAbout;
    std::vector<Rect> bars, stepDown, stepUp;
    Rect preview, previewLabel, mapsBox, generate, proceed, back;
    std::vector<Rect> mapRows, mapDelete;  // a map's row, and the X beside it that asks to delete it
};

// The question asked before a map is deleted: its box, the Delete button held down to delete, and Cancel
struct MapDeleteLayout {
    Rect box, hold, cancel;
    std::vector<Rect> lines;
};
MapDeleteLayout layoutMapDelete(int W, int H, int lineCount);
WorldGenLayout layoutWorldGen(int W, int H, int mapCount, int mapScroll);

enum class EditorTab { Entities, Props, Items, Theme, Kill, Clean, Destroy };   // Kill: the Developer panel's own, never the level editor's
enum class EditorTool { None, Unit, Mob, Prop, Item, Spawn, Erase };
inline constexpr int EDITOR_NAME_CHARS = 32;

// The spawn list: the level editor's, and the Build tab's Developer panel in a game, which work the same way (app_spawn.cpp).
// A pick of a unit, mob, prop or item is placed with a left click, cluster of them at once around the spot.
struct SpawnPicker {
    bool open = false;   // the Developer panel is showing (a game only; the level editor's is always there)
    EditorTab tab = EditorTab::Entities;   // Theme in the level editor shows its theme panel in place of the list
    EditorTool tool = EditorTool::None;
    int picked = 0;      // by the tool: 0 for a Kobold, a MobKind, a PropKind or an item id
    int faction = 0;     // what a unit, mob or built prop placed belongs to (FACTION_PLAYER at first); Factionless: a wild mob
    int listScroll = 0;
    int cluster = 1;     // how many each click places
    int stack = 1;       // an item's count in each stack placed, up to its stack limit
    bool stackMax = false;   // Max was pressed: each item picked is placed as a full stack
    enum class Typing { None, Cluster, Stack };
    Typing typing = Typing::None;   // a count being typed into its box
    std::string typed;
    float yaw = 0.0f;
};

// The three list tabs, the faction's < and > around its name, the cluster's - and + around its count, and the list's
// rows, in the area given
struct SpawnPickerLayout {
    Rect tabs[3];
    Rect factionLabel, factionDown, factionBox, factionUp;
    Rect clusterLabel, clusterDown, clusterBox, clusterUp;
    // The Items tab's row, where the other tabs have the faction's: the stack's - and + around its count, then 1 and Max
    Rect stackLabel, stackDown, stackBox, stackUp, stackOne, stackMax;
    Rect listArea;
    std::vector<Rect> rows;
};
SpawnPickerLayout layoutSpawnPicker(const Rect& area, float scale);

// The Developer panel's Kill Entity and Clean tabs work alike: one thing clicked, everything within a radius of a click, or
// everything on the map. The last is held down for SWEEP_ALL_HOLD_MS to go through, or cancelled.
enum class SweepMode { Single, Radius, All };
inline constexpr int SWEEP_MODE_COUNT = 3;
inline constexpr int SWEEP_RADIUS_MIN = 1, SWEEP_RADIUS_MAX = 50;
inline constexpr unsigned long long SWEEP_ALL_HOLD_MS = 3000;
struct SweepUi {
    SweepMode mode = SweepMode::Single;
    bool dropOpen = false;       // the mode list is showing under its box
    int radius = 5;              // tiles, for Radius
    bool holding = false;        // the All button is held down
    unsigned long long holdStart = 0;
};
// Kill Entity: what becomes of a corpse and what it held. With none of these on, the corpse stays with its things.
struct KillUi : SweepUi {
    bool deleteCorpse = false;   // the corpse is removed, and what it held goes with it unless dropped
    bool dropItems = false;      // what it held is put on the ground where it fell
    bool deleteItems = false;    // what it held is removed
};

// Destroy: a prop broken the way the game breaks one, which leaves Debris and puts what it holds (a store, a forge's
// output, a pile of bones, a blueprint's logs) on the ground. Exactly one of the two item toggles is on.
struct DestroyUi : SweepUi {
    bool deleteDebris = false;   // no Debris is left behind
    bool dropItems = true;       // what it held is put on the ground
    bool deleteItems = false;    // what it held is removed
};

// The Developer panel in a game: its title and close button, the Spawn and Theme tabs, the Kill, Clean and Destroy tabs under them, then either the spawn
// list with its help, or (on the Theme tab) the theme panel with the temporary-theme status and its buttons, or (on the
// Kill, Clean and Destroy tabs) the mode box and what the mode needs
struct DeveloperLayout {
    Rect panel, title, close, help;
    Rect spawnTab, themeTab, killTab, cleanTab, destroyTab;
    SpawnPickerLayout picker;
    Rect themeArea, status, revert, makePermanent;
    Rect modeLabel, modeBox, modeRows[SWEEP_MODE_COUNT];
    Rect toggles[3];   // Kill: delete corpse, drop items, delete items. Destroy: delete debris, drop items, delete items.
    Rect radiusLabel, radiusDown, radiusBox, radiusUp;
    Rect sweepText, sweepHold, sweepCancel;
};
DeveloperLayout layoutDeveloper(int W, int H, EditorTab tab = EditorTab::Entities);

struct LevelEditorUi {
    bool useOwnUi = true;
    SpawnPicker spawn;
    std::string name;
    bool editingName = false;
    std::string savedText;
    std::vector<std::string> undo;
    bool exitWarned = false;
    WorldGenSettings map;   // the map being edited, for its theme
    std::string savedMapText;
};

struct EditorLayout {
    Rect panel, themeTab, themeArea, nameBox, save, load, spawnTool, eraseTool, exit, status;
    SpawnPickerLayout picker;
    Rect uiToggle;
};
EditorLayout layoutEditor(int W, int H, bool ownUi);

// The Building Blueprints editor (app_blueprint.cpp): a flat test area, with the Build tab's tools put down finished at once,
// and a panel for the plan's name, its box, and saving, opening and leaving. Its plans go to MAP/BLUEPRINT (build_plan.h).
inline constexpr int BLUEPRINT_NAME_CHARS = 32;
inline constexpr int BLUEPRINT_MOST_FLOORS = 8;   // the game's top floor, STOREY_MAX, and the ground floor under it
inline constexpr int BLUEPRINT_AREA = 192;      // the test area, in tiles each way
inline constexpr int BLUEPRINT_CORNER = 64;     // where the plan's box starts, in tiles from the area's corner
struct BlueprintUi {
    bool active = false;
    std::string name;
    bool editingName = false;
    int w = 5, d = 5, floors = BLUEPRINT_MOST_FLOORS;   // the plan's box, and the floors that can be looked at (up to 8)
    std::string savedText;          // the plan as last saved or opened, to tell what has changed
    bool exitWarned = false;
    bool saveWarned = false;        // Save was pressed with pieces outside the box: pressed again, it saves anyway
    bool dirty = false;             // changed since it was saved or opened, looked at twice a second
    float dirtyIn = 0.0f;
    float x = -1.0f, y = -1.0f;     // where the panel is: -1, the top left corner
    bool folded = false;            // only its title bar shows
    bool dragging = false;          // the title bar is held, to move the panel
    float grabX = 0.0f, grabY = 0.0f;
    bool listOpen = false;          // the plans to open
    std::vector<std::string> plans;
    int listScroll = 0;
};
struct BlueprintLayout {
    Rect panel, title, fold, nameBox;
    Rect widthLabel, widthDown, widthBox, widthUp;
    Rect depthLabel, depthDown, depthBox, depthUp;
    Rect floorsLabel, floorsDown, floorsBox, floorsUp;
    Rect save, load, clear, exit, status;
    Rect list;                      // the plans to open, under the panel
    std::vector<Rect> rows;
};
BlueprintLayout layoutBlueprint(int W, int H, const BlueprintUi& ui);
