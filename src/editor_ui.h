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
    std::string name = "world";
    bool editingName = false;
    int dragRow = -1;
    bool previewDirty = true;
    unsigned long long previewAt = 0;
    bool generated = false;
    std::string generatedName;
    std::vector<std::string> maps;
    int mapScroll = 0;
};

struct WorldGenLayout {
    Rect panel, tabWorld, tabTheme, themeArea, nameBox, seedBox, seedRandom, sizeDown, sizeUp, sizeBox, styleDown, styleUp, styleBox, styleAbout;
    std::vector<Rect> bars, stepDown, stepUp;
    Rect preview, previewLabel, mapsBox, generate, proceed, back;
    std::vector<Rect> mapRows;
};
WorldGenLayout layoutWorldGen(int W, int H, int mapCount, int mapScroll);

enum class EditorTab { Entities, Props, Items, Theme };
enum class EditorTool { None, Unit, Mob, Prop, Item, Spawn, Erase };
inline constexpr int EDITOR_NAME_CHARS = 32;

struct LevelEditorUi {
    bool useOwnUi = true;
    EditorTab tab = EditorTab::Entities;
    EditorTool tool = EditorTool::None;
    int picked = 0;      // by the tool: a unit's faction, a MobKind, a PropKind or an item id
    int listScroll = 0;
    float yaw = 0.0f;
    std::string name;
    bool editingName = false;
    std::string savedText;
    std::vector<std::string> undo;
    bool exitWarned = false;
    WorldGenSettings map;   // the map being edited, for its theme
    std::string savedMapText;
};

struct EditorLayout {
    Rect panel, tabs[4], themeArea, nameBox, save, load, spawnTool, eraseTool, exit, status;
    std::vector<Rect> rows;
    Rect uiToggle;
};
EditorLayout layoutEditor(int W, int H, bool ownUi);
