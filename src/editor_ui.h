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

enum class EditorTab { Entities, Props, Items, Theme };
enum class EditorTool { None, Unit, Mob, Prop, Item, Spawn, Erase };
inline constexpr int EDITOR_NAME_CHARS = 32;

// The spawn list: the level editor's, and the Build tab's Developer panel in a game, which work the same way (app_spawn.cpp).
// A pick of a unit, mob, prop or item is placed with a left click, cluster of them at once around the spot.
struct SpawnPicker {
    bool open = false;   // the Developer panel is showing (a game only; the level editor's is always there)
    EditorTab tab = EditorTab::Entities;   // Theme in the level editor shows its theme panel in place of the list
    EditorTool tool = EditorTool::None;
    int picked = 0;      // by the tool: a unit's faction, a MobKind, a PropKind or an item id
    int listScroll = 0;
    int cluster = 1;     // how many each click places
    float yaw = 0.0f;
};

// The three list tabs, the cluster's - and + around its count, and the list's rows, in the area given
struct SpawnPickerLayout {
    Rect tabs[3];
    Rect clusterLabel, clusterDown, clusterBox, clusterUp;
    Rect listArea;
    std::vector<Rect> rows;
};
SpawnPickerLayout layoutSpawnPicker(const Rect& area, float scale);

// The Developer panel in a game: its title and close button over the spawn list
struct DeveloperLayout {
    Rect panel, title, close, help;
    SpawnPickerLayout picker;
};
DeveloperLayout layoutDeveloper(int W, int H);

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
