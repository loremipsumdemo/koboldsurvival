#pragma once
#include <string>
#include <vector>
#include "level.h"
#include "ui_types.h"
#include "world_gen.h"

// The World Generation screen, and the level editor that follows it. Both only open with the developer option on (F2).
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

struct WorldGenUi {
    WorldGenSettings settings;
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
    Rect panel, nameBox, seedBox, seedRandom, sizeDown, sizeUp, sizeBox;
    std::vector<Rect> bars, stepDown, stepUp;
    Rect preview, previewLabel, mapsBox, generate, proceed, back;
    std::vector<Rect> mapRows;
};
WorldGenLayout layoutWorldGen(int W, int H, int mapCount, int mapScroll);

enum class EditorTab { Props, Mobs, Items };
enum class EditorTool { None, Prop, Mob, Item, Spawn, Erase };
inline constexpr int EDITOR_NAME_CHARS = 32;

struct LevelEditorUi {
    bool useOwnUi = true;
    EditorTab tab = EditorTab::Props;
    EditorTool tool = EditorTool::None;
    int picked = 0;
    int listScroll = 0;
    float yaw = 0.0f;
    std::string name;
    bool editingName = false;
    std::string savedText;
    std::vector<std::string> undo;
    bool exitWarned = false;
};

struct EditorLayout {
    Rect panel, tabs[3], nameBox, save, load, spawnTool, eraseTool, exit, status;
    std::vector<Rect> rows;
    Rect uiToggle;
};
EditorLayout layoutEditor(int W, int H, bool ownUi);
