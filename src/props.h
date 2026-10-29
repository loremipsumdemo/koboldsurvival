#pragma once
#include "stats.h"

// Props: 3D models placed on the map, such as wagons, campfires and trees. They do not block movement and cannot be
// picked up. Each prop is an entity with a Prop component (components.h) and a Transform. Some props can be used:
//   storage props (the barrel) hold items that a unit standing by them can take or put in;
//   occupiable props (the bed and the bedroll) hold one unit, which stays on them until it is given another order;
//   gathering props (ore and trees) are worked by up to PROP_WORKER_LIMIT units, and give an item now and then.

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
enum class PropUse { None, Storage, Occupy, Gather };

// What a kind of prop looks like, is called and does.
// file: its model, in the pak. A missing model is logged and that kind is not drawn.
// nameKey: its name in the language files, shown in the hover list and the storage panel.
// size: the model is scaled so its longest side is this many tiles.
// use: what right-clicking it does. gatherStat and gatherItem are for gathering props only: the skill that sets
// how fast a unit works it, and the item it gives (an id from data/itemdb.txt).
struct PropInfo {
    const char* file;
    const char* nameKey;
    float size;
    PropUse use;
    int gatherStat;
    int gatherItem;
};

inline const PropInfo& propInfo(int kind) {
    static const PropInfo info[PROP_KIND_COUNT] = {
        { "data/3d/wagon.glb",    "PROP_WAGON",    1.5f, PropUse::None,    -1, 0 },
        { "data/3d/campfire.glb", "PROP_CAMPFIRE", 0.8f, PropUse::None,    -1, 0 },
        { "data/3d/bedroll.glb",  "PROP_BEDROLL",  1.0f, PropUse::Occupy,  -1, 0 },
        { "data/3d/bed.glb",      "PROP_BED",      1.4f, PropUse::Occupy,  -1, 0 },
        { "data/3d/barrel.glb",   "PROP_BARREL",   0.7f, PropUse::Storage, -1, 0 },
        { "data/3d/ore.glb",      "PROP_ORE",      1.0f, PropUse::Gather,  STAT_MINING, 1 },       // Iron Ore
        { "data/3d/tree.glb",     "PROP_TREE",     3.0f, PropUse::Gather,  STAT_WOODCUTTING, 88 }, // Wood Log
        { "data/3d/boulder.glb",  "PROP_BOULDER",  1.2f, PropUse::None,    -1, 0 },
        { "data/3d/mushroom.glb", "PROP_MUSHROOM", 0.6f, PropUse::None,    -1, 0 },
        { "data/3d/bones.glb",    "PROP_BONES",    1.2f, PropUse::None,    -1, 0 },
        { "data/3d/wall.glb",     "PROP_WALL",     2.0f, PropUse::None,    -1, 0 },
        { "data/3d/stall.glb",    "PROP_STALL",    2.0f, PropUse::None,    -1, 0 },
    };
    return info[(kind >= 0 && kind < PROP_KIND_COUNT) ? kind : 0];
}

constexpr int PROP_COPIES = 2;         // how many of each kind a new level places
constexpr float PROP_RANGE = 40.0f;    // props are placed within this many tiles of the spawn, each on its own tile

constexpr float PROP_REACH = 1.2f;            // a unit this close to a prop (in tiles) can use it
constexpr float STORAGE_CLOSE_RANGE = 1.5f;   // the storage panel closes when its unit is further away than this
constexpr int PROP_WORKER_LIMIT = 3;          // most units that can work one gathering prop at once

// In-game hours one unit takes to gather one item: one hour, plus up to one more for a low skill. The extra time
// falls evenly from a full hour at level 1 to nothing at level 99.
inline double gatherHoursFor(int level) {
    const int lv = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    return 1.0 + (double)(MAX_LEVEL - lv) / (double)(MAX_LEVEL - 1);
}

constexpr float GATHER_XP_PER_ITEM = 20.0f;   // skill XP for each item gathered, to the unit that receives it
