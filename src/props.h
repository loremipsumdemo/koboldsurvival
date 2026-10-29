#pragma once

// Props: 3D models placed on the map, such as wagons, campfires and trees. They are scenery: they do not block
// movement and cannot be picked up. Each prop is an entity with a Prop component (components.h) and a Transform.

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

// What a kind of prop looks like and is called.
// file: its model, in the pak. A missing model is logged and that kind is not drawn.
// nameKey: its name in the language files, shown in the hover list at the top left.
// size: the model is scaled so its longest side is this many tiles.
struct PropInfo {
    const char* file;
    const char* nameKey;
    float size;
};

inline const PropInfo& propInfo(int kind) {
    static const PropInfo info[PROP_KIND_COUNT] = {
        { "data/3d/wagon.glb",    "PROP_WAGON",    1.5f },
        { "data/3d/campfire.glb", "PROP_CAMPFIRE", 0.8f },
        { "data/3d/bedroll.glb",  "PROP_BEDROLL",  1.0f },
        { "data/3d/bed.glb",      "PROP_BED",      1.4f },
        { "data/3d/barrel.glb",   "PROP_BARREL",   0.7f },
        { "data/3d/ore.glb",      "PROP_ORE",      1.0f },
        { "data/3d/tree.glb",     "PROP_TREE",     3.0f },
        { "data/3d/boulder.glb",  "PROP_BOULDER",  1.2f },
        { "data/3d/mushroom.glb", "PROP_MUSHROOM", 0.6f },
        { "data/3d/bones.glb",    "PROP_BONES",    1.2f },
        { "data/3d/wall.glb",     "PROP_WALL",     2.0f },
        { "data/3d/stall.glb",    "PROP_STALL",    2.0f },
    };
    return info[(kind >= 0 && kind < PROP_KIND_COUNT) ? kind : 0];
}

constexpr int PROP_COPIES = 2;         // how many of each kind a new level places
constexpr float PROP_RANGE = 40.0f;    // props are placed within this many tiles of the spawn, each on its own tile
