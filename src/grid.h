#pragma once

constexpr int DEFAULT_MAP_SIZE = 1024;
constexpr int START_UNIT_COUNT = 3;
constexpr int SQUAD_COLUMNS = 5;
constexpr float SLOT_SPACING = 1.3f;
constexpr float MIN_SLOT_SPACING = 1.0f;
constexpr float MAX_HEIGHT = 4096.0f;

struct GridPos {
    int x = 0, y = 0;
};

inline bool operator==(GridPos a, GridPos b) { return a.x == b.x && a.y == b.y; }
