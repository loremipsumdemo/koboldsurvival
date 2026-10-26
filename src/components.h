#pragma once
#include <array>
#include <string>
#include <vector>
#include "behavior_rules.h"
#include "ecs.h"
#include "grid.h"
#include "geometry.h"
#include "stats.h"

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

// Red units. They can be selected but not commanded. They stand until a player unit comes within
// aggro range, then chase it.
struct Hostile {};

// Neutral units: like red units, but not hostile. They can be talked to, and nothing attacks them.
struct Neutral {};

// What a unit says when it is talked to. Every red and neutral unit has the same default.
struct Dialogue {
    std::string text = "Hello world.";
};

// Display name for a unit
struct Name {
    std::string text = "Unnamed Unit";
};

// Which unit image this unit uses: data/unit/01.png up to UNIT_SPRITE_COUNT (party.h)
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

// The gear a unit wears, one item per slot. The order is the order of the sidebar's gear panel.
enum GearSlot : int {
    GEAR_HEAD = 0,
    GEAR_TORSO,
    GEAR_MAIN_HAND,
    GEAR_OFF_HAND,
    GEAR_LEGS,
    GEAR_FEET,
    GEAR_ACCESSORY_1,
    GEAR_ACCESSORY_2,
    GEAR_SLOTS
};

struct Equipment {
    std::array<int, GEAR_SLOTS> items{};   // item id in each slot, 0 for empty
};

// Health and hunger, for every unit. Hunger falls by one each in-game hour, on the clock's hour. A tick
// while hunger is already 0 costs one health instead. Health is set from Vitality each frame, and it
// stays between zero and the maximum. A unit at zero health is downed. Out of combat, with hunger over
// half, health comes back one point per tenth of an in-game hour.
// The last three fields are not saved: a loaded unit starts with no run mark, no part of a regen point,
// and both values marked changed, so its behaviors are checked once.
constexpr unsigned NEEDS_HP_CHANGED = 1u;
constexpr unsigned NEEDS_HUNGER_CHANGED = 2u;
struct Needs {
    int hp = 100, maxHp = 100;
    int hunger = 100, maxHunger = 100;
    bool hurried = false;        // moved at run pace this in-game hour, so the next hunger tick costs double
    double healSeconds = 0.0;    // in-game seconds saved toward the next point of health
    // Which values changed since the behavior pass last checked them. Every place that changes one sets its bit,
    // and the pass clears the bits it has read.
    unsigned changed = NEEDS_HP_CHANGED | NEEDS_HUNGER_CHANGED;
};

// A unit's behavior rules, highest priority first. pending is set when a rule is added or edited, so the next
// behavior pass checks every rule, not only those whose values changed.
struct Behaviors {
    std::vector<BehaviorRule> rules = defaultBehaviors();
    bool pending = true;
};

// Fighting state for a unit. Nothing here is saved: a loaded unit starts fresh, with its stamina full.
struct Combat {
    float swingLeft = 0.0f;           // seconds until the unit may swing again
    Entity target = NULL_ENTITY;      // the unit it is fighting: a hostile for a player unit, and the reverse
    float buffAttack = 0.0f;          // attack added by an elixir
    float buffLeft = 0.0f;            // seconds the elixir has left
    float stamina = -1.0f;            // below zero until the first combat frame sets it to the maximum
    float encumbrance = 0.0f;         // 0..1, from the last frame's movement
    float moved = 0.0f;               // tiles walked in the last frame
    std::vector<Entity> spotted;      // hostiles this unit has already spotted, for Perception XP
};

// Fractional XP waiting to become whole points, one per stat. Walking and fighting give fractions of a point.
struct XpBuffer {
    std::array<float, STAT_COUNT> frac{};
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

// A player unit's order to fight one red unit. The unit walks at it and keeps it as its target, choosing no
// other, until the red unit is gone. A move or pickup order cancels it.
struct AttackOrder {
    Entity target = NULL_ENTITY;
};

// Decorative wagon. It is drawn as a symbol and does not block movement.
struct Wagon {};
