#pragma once

constexpr int DEFAULT_MAP_SIZE = 1024;      // tiles per side for a new map. Each map sets its own size.
constexpr int START_UNIT_COUNT = 3;         // player units a new game starts with
constexpr int SQUAD_COLUMNS = 5;            // formation width
constexpr float SLOT_SPACING = 1.3f;        // distance between formation slots in a full squad
constexpr float MIN_SLOT_SPACING = 1.0f;    // closest slots may sit, so units don't overlap
constexpr float MAX_HEIGHT = 4096.0f;        // highest or lowest a corner may be, in world units. Generated maps scale with their size.

struct GridPos {
    int x = 0, y = 0;
};

inline bool operator==(GridPos a, GridPos b) { return a.x == b.x && a.y == b.y; }
