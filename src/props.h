#pragma once
#include "emotes.h"
#include "stats.h"

// Props: 3D models placed on the map, such as wagons, campfires and trees. They do not block movement and cannot be
// picked up. Each prop is an entity with a Prop component (components.h) and a Transform. Some props can be used:
//   Storage props (the barrel) hold items that a unit standing by them can take or put in.
//   Occupiable props (the bed and the bedroll) hold one unit, which stays on them until it is given another order.
//   Resource props (ore and trees) are worked by units, and give an item now and then.
//   Crafting props (the campfire) have an inventory that takes only the item they use (Raw Meat for the campfire).
//   A unit working one is like one working a resource prop, except that each item it makes uses up one input item,
//   checked at the moment the item is finished, and goes straight into the worker's inventory.
// Storage and crafting props keep their items in a Storage component.

enum PropKind : int {
    PROP_WAGON,
    PROP_CAMPFIRE,
    PROP_BEDROLL,
    PROP_BED,
    PROP_BARREL,
    PROP_ORE,
    PROP_TREE,
    PROP_BOULDER,
    PROP_MUSHROOM,
    PROP_BONES,
    PROP_WALL,
    PROP_STALL,
    PROP_KIND_COUNT
};

// What a prop can be used for, by right-clicking it with units selected
enum class PropUse { None, Storage, Occupy, Resource, Crafting };

// What a unit sent to a prop does there, as picked from the right-click menu: Inventory opens a storage or crafting
// prop's items; Use takes a bed, works a resource prop, or starts crafting
enum class PropAction { Inventory, Use };

// What a kind of prop looks like, is called and does.
// file: its model, in the pak. A missing model is logged and that kind is not drawn.
// nameKey: its name in the language files, shown in the hover list and the storage panel.
// size: the model is scaled so its longest side is this many tiles.
// use: what right-clicking it does.
// For resource and crafting props only:
//   stat: the skill that sets how fast a unit works it (workHoursFor)
//   item: the item it gives the worker: what a resource prop yields, or what a crafting prop makes (an id from
//         data/itemdb.txt)
//   input: the item a crafting prop uses up, one for each item it makes; 0 for a resource prop
//   workers: the most units that can work it at once
struct PropInfo {
    const char* file;
    const char* nameKey;
    float size;
    PropUse use;
    int stat;
    int item;
    int input;
    int workers;
};

inline const PropInfo& propInfo(int kind) {
    static const PropInfo info[PROP_KIND_COUNT] = {
        { "data/3d/wagon.glb",    "PROP_WAGON",    1.5f, PropUse::None,     -1, 0, 0, 0 },
        { "data/3d/campfire.glb", "PROP_CAMPFIRE", 0.8f, PropUse::Crafting, STAT_COOKING, 90, 89, 1 },    // Raw Meat -> Cooked Meat
        { "data/3d/bedroll.glb",  "PROP_BEDROLL",  1.0f, PropUse::Occupy,   -1, 0, 0, 0 },
        { "data/3d/bed.glb",      "PROP_BED",      1.4f, PropUse::Occupy,   -1, 0, 0, 0 },
        { "data/3d/barrel.glb",   "PROP_BARREL",   0.7f, PropUse::Storage,  -1, 0, 0, 0 },
        { "data/3d/ore.glb",      "PROP_ORE",      1.0f, PropUse::Resource, STAT_MINING, 1, 0, 3 },       // Iron Ore
        { "data/3d/tree.glb",     "PROP_TREE",     3.0f, PropUse::Resource, STAT_WOODCUTTING, 88, 0, 3 }, // Wood Log
        { "data/3d/boulder.glb",  "PROP_BOULDER",  1.2f, PropUse::None,     -1, 0, 0, 0 },
        { "data/3d/mushroom.glb", "PROP_MUSHROOM", 0.6f, PropUse::None,     -1, 0, 0, 0 },
        { "data/3d/bones.glb",    "PROP_BONES",    1.2f, PropUse::None,     -1, 0, 0, 0 },
        { "data/3d/wall.glb",     "PROP_WALL",     2.0f, PropUse::None,     -1, 0, 0, 0 },
        { "data/3d/stall.glb",    "PROP_STALL",    2.0f, PropUse::None,     -1, 0, 0, 0 },
    };
    return info[(kind >= 0 && kind < PROP_KIND_COUNT) ? kind : 0];
}

// The emote shown over a unit using a prop of this kind: working it, or lying on it. EMOTE_NONE for none.
inline int propEmote(int kind) {
    switch (kind) {
    case PROP_ORE: return EMOTE_MINING;
    case PROP_TREE: return EMOTE_WOODCUTTING;
    case PROP_CAMPFIRE: return EMOTE_COOKING;
    case PROP_BED:
    case PROP_BEDROLL: return EMOTE_SLEEPING;
    default: return EMOTE_NONE;
    }
}

// Whether units are put to work on a prop of this use
inline bool isWorkProp(PropUse use) { return use == PropUse::Resource || use == PropUse::Crafting; }
// Whether a prop of this use keeps items, and opens the storage panel
inline bool hasStorage(PropUse use) { return use == PropUse::Storage || use == PropUse::Crafting; }

// The slots of a storage or crafting prop. A crafting prop's inventory takes only its input item.
constexpr int STORAGE_SLOTS = 12;
constexpr int STORAGE_COLUMNS = 6;

constexpr int PROP_COPIES = 2;         // how many of each kind a new level places
constexpr float PROP_RANGE = 40.0f;    // props are placed within this many tiles of the spawn, each on its own tile

// A unit within this many tiles of a prop can use it: work a resource or crafting prop, open a storage, take a bed.
// It need not stand right on the prop, so several can work one without crowding each other off it.
constexpr float PROP_REACH = 2.0f;
constexpr float STORAGE_CLOSE_RANGE = 2.5f;   // the storage panel closes when its unit is further away than this
// Each prop stands on its own tile. For an occupiable prop's line check, another prop is in the way when the straight
// line from the unit to the bed passes within this many tiles of that prop's centre.
constexpr float PROP_BLOCK_RADIUS = 0.45f;

// In-game hours one unit takes to make one item at a resource or crafting prop: one hour, plus up to one more for a
// low skill. The extra time falls evenly from a full hour at level 1 to nothing at level 99.
inline double workHoursFor(int level) {
    const int lv = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    return 1.0 + (double)(MAX_LEVEL - lv) / (double)(MAX_LEVEL - 1);
}

constexpr float WORK_XP_PER_ITEM = 20.0f;   // skill XP for each item made, to the unit credited with it
