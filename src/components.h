#pragma once
#include <array>
#include <string>
#include <vector>
#include "ecs.h"
#include "grid.h"
#include "geometry.h"

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

// Which unit image this unit uses: data/unit/01.png .. 38.png
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
    std::vector<Vec2> waypoints;      // smooth path, world units
    size_t next = 0;
    bool arrived = false;
    float speed = 2.8f;               // slower than units, so members can keep up
    int size = 0;                     // number of units, which sets the formation's spacing
};

// A single unit's own route. Used instead of a squad when one unit is ordered.
struct SoloMove {
    std::vector<Vec2> waypoints;      // smooth path, world units
    size_t next = 0;
};

// One inventory slot: an item id and how many of it. Item 0 means the slot is empty.
struct ItemStack {
    int item = 0;
    int count = 0;
};

// Items a unit carries, one stack per slot
constexpr int INVENTORY_SLOTS = 32;
constexpr int INVENTORY_COLUMNS = 8;
struct Inventory {
    std::array<ItemStack, INVENTORY_SLOTS> slots{};
};

// Health and hunger, for every unit. Both start at their maximum. Hunger falls by one each in-game
// minute. A tick while hunger is already 0 costs one health instead. Neither goes below zero.
struct Needs {
    int hp = 100, maxHp = 100;
    int hunger = 100, maxHunger = 100;
    float hungerProgress = 0.0f;   // in-game minutes since the last tick
};

// An item lying on the ground. Its Transform gives the place.
struct GroundItem {
    int item = 0;
    int count = 1;
};

// A unit's order to pick up a ground item. The unit walks to it and takes what fits.
struct PickupOrder {
    Entity item = NULL_ENTITY;
};

// Decorative wagon. It is drawn as a symbol and does not block movement.
struct Wagon {};
