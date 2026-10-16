#pragma once

constexpr int MAP_SIZE = 26;                // tiles per side
constexpr int VERT_SIZE = MAP_SIZE + 1;     // corner vertices per side
constexpr int UNIT_COUNT = 20;
constexpr int KEY_COUNT = 3;
constexpr int SQUAD_COLUMNS = 5;            // formation width
constexpr float SLOT_SPACING = 1.3f;        // distance between formation slots
constexpr float HEIGHT_SCALE = 4.0f;        // max terrain height above/below zero, in world units

struct GridPos {
    int x = 0, y = 0;
};

inline bool operator==(GridPos a, GridPos b) { return a.x == b.x && a.y == b.y; }
