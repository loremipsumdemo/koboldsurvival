#pragma once
#include <string>
#include <vector>
#include "ecs.h"
#include "grid.h"

// World position. y is the terrain height under the entity.
struct Transform {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// A player-controlled unit
struct Unit {
    float speed = 3.0f;      // tiles per second
    float radius = 0.4f;     // used for separation from other units
};

struct Selected {};

// Player units. Only these can be ordered to move.
struct Commandable {};

// Red units. They stand still and can be selected, but can't be commanded.
struct Hostile {};

// Display name for a unit
struct Name {
    std::string text = "Unnamed Unit";
};

// Which unit image this unit uses: assets/unit/01.png .. 38.png
struct UnitSprite {
    int id = 1;
};

// Links a unit to its squad and to its formation slot
struct SquadMember {
    Entity squad = NULL_ENTITY;
    int slot = 0;
};

// A squad is one group movement order. Its center walks a path, and its
// members seek formation slots around the center.
struct Squad {
    float cx = 0.0f, cz = 0.0f;       // formation center
    float headX = 0.0f, headZ = 1.0f; // facing, from the direction of travel
    std::vector<GridPos> waypoints;
    size_t next = 0;
    bool arrived = false;
    bool wantsDoor = false;           // ordered to enter the building
    bool enterDoor = false;           // unlocked and at the entrance, so members go through
    float speed = 2.8f;               // slower than units, so members can keep up
    int size = 0;                     // number of units, which sets the formation's spacing
};

// A single unit's own route. Used instead of a squad when one unit is ordered.
struct SoloMove {
    std::vector<GridPos> waypoints;
    size_t next = 0;
    bool wantsDoor = false;           // ordered to enter the building
    bool enterDoor = false;           // unlocked and at the entrance, so the unit goes through
};

struct Key {};

// Decorative wagon. It is drawn as a symbol and does not block movement.
struct Wagon {};

// The 3x3 building. Its entrance is the south-centre tile of the footprint.
struct Door {
    GridPos origin;     // top-left tile of the 3x3 footprint
    GridPos entrance;   // tile units step onto to enter
    GridPos front;      // tile just outside the entrance, where locked squads wait
    bool unlocked = false;
};
