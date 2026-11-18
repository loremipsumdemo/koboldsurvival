#pragma once
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "biome.h"
#include "map_edits.h"
#include "ui_types.h"

// The Map Editor (app_map_editor.cpp): reshapes a generated map's ground with brushes and paints its ground types, before the Level Editor.
// Its edits are kept beside the map (map_edits.h).
enum class MapTool : int { Raise = 0, Lower, Smooth, Flatten, Roughen, Paint, Unpaint, Restore, Count };
enum class BrushShape : int { Circle = 0, Square, Diamond, Count };
enum class BrushFall : int { Soft = 0, Linear, Hard, Count };
inline constexpr int BRUSH_MOST_RADIUS = 96;   // tiles
inline constexpr float MAP_HEIGHT_LEAST = -20.0f, MAP_HEIGHT_MOST = 45.0f;   // in tiles, sea level at 0

// What one stroke of a brush changed, with what each thing was before, to undo it
struct MapStroke {
    struct Corner {
        int x, z;
        bool had;
        float old;
    };
    struct Tile {
        int x, z;
        bool had;
        uint8_t old;
    };
    std::vector<Corner> corners;
    std::vector<Tile> tiles;
    bool empty() const { return corners.empty() && tiles.empty(); }
};

struct MapEditorUi {
    std::shared_ptr<MapEdits> edits;
    MapTool tool = MapTool::Raise;
    BrushShape shape = BrushShape::Circle;
    BrushFall fall = BrushFall::Soft;
    int radius = 10, strength = 50;   // tiles; percent
    int biome = (int)Biome::Grass;     // what Paint lays down
    bool dragging = false;            // a stroke is under way
    int dragBar = -1;                 // a slider being dragged: 0 size, 1 strength
    float flatHeight = 0.0f;          // what Flatten levels to: the height where the stroke began
    unsigned long long lastApply = 0;
    unsigned applyCount = 0;
    MapStroke current;
    std::unordered_set<long long> seenCorners, seenTiles;
    std::vector<MapStroke> undo;
    bool dirty = false, exitWarned = false;
    std::string stem;
};

struct MapEditorLayout {
    Rect panel, title, status;
    Rect tools[(int)MapTool::Count], shapes[(int)BrushShape::Count], falls[(int)BrushFall::Count];
    Rect sizeDown, sizeBar, sizeUp, strengthDown, strengthBar, strengthUp;
    std::vector<Rect> biomes;
    Rect undo, save, levels, back;
};
MapEditorLayout layoutMapEditor(int W, int H);
