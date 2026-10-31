#pragma once
#include "emotes.h"
#include "stats.h"

// Props: 3D models placed on the map, such as wagons, campfires and trees. They do not block movement and cannot be
// picked up. Each prop is an entity with a Prop component (components.h) and a Transform. By use:
//   Decor props (the wagon, tables, torches) are only scenery.
//   Storage props (the barrel) hold items that a unit standing by them can take or put in. Some take only one sort of
//   item (PropInfo::accepts): the Weapon Rack only weapons, the Water Tank only water.
//   Occupiable props (beds, chairs) hold one unit, which stays on them until it is given another order.
//   Resource props (ore, trees and mushrooms) are worked by units, and give an item now and then.
//   Crafting props (the campfire) have an inventory that takes only the items they use (raw meat for the campfire).
//   A unit working one is like one working a resource prop, except that each item it makes uses up one input item,
//   checked at the moment the item is finished, and goes straight into the worker's inventory. A crafting prop with
//   no input has no inventory: the Water Pump gives water for the work alone, and the Training Dummy gives no item,
//   only skill XP.
//   Passive crafting props (the forge) work by themselves, with no unit at them. Each input item they take is burned
//   as fuel for fuelHours in-game hours, and while it burns they make one output item every PASSIVE_CRAFT_HOURS into
//   their own output inventory. They pause while the output has no room, and go on once it has.
// Storage and crafting props keep their items in a Storage component; a passive crafting prop's output is an Output.
// Lockable: a tag any storage or occupiable prop kind can have (PropInfo::lockLevel above 0). A lockable prop has a
// Lock (components.h), which starts locked. While locked it can't be opened or used; a unit with at least lockLevel in
// Lockpicking can right-click it and Unlock it, a timed attempt at a time, like working ore. A lockable occupiable
// prop (the Prisoner Cage) locks again as soon as a unit gets in, and the unit can't leave until it is unlocked, by
// itself from the inside or by another unit from the outside.

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
    PROP_LOCKED_CHEST,
    PROP_PRISONER_CAGE,
    PROP_CHAIR,
    PROP_BENCH,
    PROP_STOOL,
    PROP_TABLE,
    PROP_END_TABLE,
    PROP_ROUND_TABLE,
    PROP_METAL_TABLE,
    PROP_STANDING_TORCH,
    PROP_SUPPORT_BEAM,
    PROP_TRAINING_DUMMY,
    PROP_RAW_FOOD_STORAGE,
    PROP_FOOD_STORAGE,
    PROP_WEAPON_RACK,
    PROP_ARMOR_STAND,
    PROP_WATER_TANK,
    PROP_WATER_PUMP,
    PROP_FORGE,
    PROP_KIND_COUNT
};

// What a prop can be used for, by right-clicking it with units selected. None is a decor prop.
enum class PropUse { None, Storage, Occupy, Resource, Crafting, PassiveCrafting };

// Which items a storage prop takes. A crafting or passive crafting prop takes only its input item, whatever this says.
//   RawFood: items tagged raw_food in data/itemdb.txt that can't be eaten as they are (not consumables).
//   Food: consumables. Weapon: weapons. Armor: armor, shields
//   included. Water: items tagged water.
enum class ItemFilter { Any, RawFood, Food, Weapon, Armor, Water };

// The language key for what a filtered storage takes, as in "Takes only weapons"
inline const char* itemFilterKey(ItemFilter f) {
    switch (f) {
    case ItemFilter::RawFood: return "FILTER_RAW_FOOD";
    case ItemFilter::Food: return "FILTER_FOOD";
    case ItemFilter::Weapon: return "FILTER_WEAPON";
    case ItemFilter::Armor: return "FILTER_ARMOR";
    case ItemFilter::Water: return "FILTER_WATER";
    default: return "";
    }
}

// What a unit sent to a prop does there, as picked from the right-click menu: Inventory opens a storage or crafting
// prop's items; Use takes a bed, works a resource prop, or starts crafting; Unlock picks a locked prop's lock
enum class PropAction { Inventory, Use, Unlock };

// What a kind of prop looks like, is called and does.
// file: its model, in the pak: data/3d/<number>.glb, numbered from 1 in PropKind order (data/3dinfo.txt lists them).
//       A missing model is logged and that kind is not drawn.
// nameKey: its name in the language files, shown in the hover list and the storage panel.
// size: the model is scaled so its longest side is this many tiles.
// use: what right-clicking it does.
// For resource, crafting and passive crafting props only:
//   stat: the skill that sets how fast a unit works it (workHoursFor)
//   item: the item it gives the worker: what a resource prop yields, or what a crafting prop makes (an id from
//         data/itemdb.txt)
//   input: the item a crafting prop uses up, one for each item it makes; 0 for a resource prop, and for a crafting
//          prop that needs none (then it has no inventory). A passive crafting prop burns it as fuel.
//          INPUT_COOKABLE: any item with a cooks_into in data/itemdb.txt, made into that item (the campfire). Its item
//          is then 0, as what it makes depends on what goes in.
//   item 0 at a crafting prop: nothing is made, and each finished bar gives the worker skill XP only (training).
//   workers: the most units that can work it at once (0 for a passive crafting prop, which no unit works)
// lockLevel: the Lockable tag. 0 for none; otherwise the prop starts locked, and this is the Lockpicking level a unit
//            needs to try to unlock it.
// accepts: for a storage prop, which items it takes (ItemFilter). Any if left out.
// fuelHours: for a passive crafting prop, the in-game hours each input item burns. An hour if left out.
// altItem, altOneIn: for a resource prop, an item it gives instead of its item one time in altOneIn (the Mushroom's
//                    Poison Mushroom, 1 in 10). None if left out.
constexpr int INPUT_COOKABLE = -1;
struct PropInfo {
    const char* file;
    const char* nameKey;
    float size;
    PropUse use;
    int stat;
    int item;
    int input;
    int workers;
    int lockLevel;
    ItemFilter accepts = ItemFilter::Any;
    double fuelHours = 1.0;
    int altItem = 0;
    int altOneIn = 0;
};

inline const PropInfo& propInfo(int kind) {
    static const PropInfo info[PROP_KIND_COUNT] = {
        { "data/3d/1.glb",  "PROP_WAGON",          1.5f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/2.glb",  "PROP_CAMPFIRE",       0.8f, PropUse::Crafting, STAT_COOKING, 0, INPUT_COOKABLE, 1, 0 },   // raw meat -> cooked
        { "data/3d/3.glb",  "PROP_BEDROLL",        1.0f, PropUse::Occupy,   -1, 0, 0, 0, 0 },
        { "data/3d/4.glb",  "PROP_BED",            1.4f, PropUse::Occupy,   -1, 0, 0, 0, 0 },
        { "data/3d/5.glb",  "PROP_BARREL",         0.7f, PropUse::Storage,  -1, 0, 0, 0, 0 },
        { "data/3d/6.glb",  "PROP_ORE",            1.0f, PropUse::Resource, STAT_MINING, 1, 0, 3, 0 },       // Iron Ore
        { "data/3d/7.glb",  "PROP_TREE",           3.0f, PropUse::Resource, STAT_WOODCUTTING, 88, 0, 3, 0 }, // Wood Log
        { "data/3d/8.glb",  "PROP_BOULDER",        1.2f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/9.glb",  "PROP_MUSHROOM",       0.6f, PropUse::Resource, STAT_HERBALISM, 125, 0, 3, 0,
          ItemFilter::Any, 1.0, 126, 10 },                                                                   // Mushroom; 1 in 10 Poison Mushroom
        { "data/3d/10.glb", "PROP_BONES",          1.2f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/11.glb", "PROP_WALL",           2.0f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/12.glb", "PROP_STALL",          2.0f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/13.glb", "PROP_LOCKED_CHEST",   0.9f, PropUse::Storage,  -1, 0, 0, 0, 5 },    // Lockable
        { "data/3d/14.glb", "PROP_PRISONER_CAGE",  1.8f, PropUse::Occupy,   -1, 0, 0, 0, 15 },   // Lockable
        { "data/3d/15.glb", "PROP_CHAIR",          0.9f, PropUse::Occupy,   -1, 0, 0, 0, 0 },
        { "data/3d/16.glb", "PROP_BENCH",          1.5f, PropUse::Occupy,   -1, 0, 0, 0, 0 },
        { "data/3d/17.glb", "PROP_STOOL",          0.5f, PropUse::Occupy,   -1, 0, 0, 0, 0 },
        { "data/3d/18.glb", "PROP_TABLE",          1.5f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/19.glb", "PROP_END_TABLE",      0.7f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/20.glb", "PROP_ROUND_TABLE",    1.2f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/21.glb", "PROP_METAL_TABLE",    1.5f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/22.glb", "PROP_STANDING_TORCH", 1.6f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/23.glb", "PROP_SUPPORT_BEAM",   2.5f, PropUse::None,     -1, 0, 0, 0, 0 },
        { "data/3d/24.glb", "PROP_TRAINING_DUMMY", 1.6f, PropUse::Crafting, STAT_MELEE, 0, 0, 1, 0 },        // XP only
        { "data/3d/25.glb", "PROP_RAW_FOOD_STORAGE", 1.0f, PropUse::Storage, -1, 0, 0, 0, 0, ItemFilter::RawFood },
        { "data/3d/26.glb", "PROP_FOOD_STORAGE",   1.0f, PropUse::Storage,  -1, 0, 0, 0, 0, ItemFilter::Food },
        { "data/3d/27.glb", "PROP_WEAPON_RACK",    1.4f, PropUse::Storage,  -1, 0, 0, 0, 0, ItemFilter::Weapon },
        { "data/3d/28.glb", "PROP_ARMOR_STAND",    1.7f, PropUse::Storage,  -1, 0, 0, 0, 0, ItemFilter::Armor },
        { "data/3d/29.glb", "PROP_WATER_TANK",     1.4f, PropUse::Storage,  -1, 0, 0, 0, 0, ItemFilter::Water },
        { "data/3d/30.glb", "PROP_WATER_PUMP",     1.4f, PropUse::Crafting, STAT_STRENGTH, 91, 0, 1, 0 },    // Water
        { "data/3d/31.glb", "PROP_FORGE",          1.6f, PropUse::PassiveCrafting, STAT_SMELTING, 92, 1, 0, 0,
          ItemFilter::Any, 1.0 },                                                                            // Iron Ore -> Iron Ingot
    };
    return info[(kind >= 0 && kind < PROP_KIND_COUNT) ? kind : 0];
}

// The emote shown over a unit using a prop of this kind: working it, or lying on it. EMOTE_NONE for none.
inline int propEmote(int kind) {
    switch (kind) {
    case PROP_ORE: return EMOTE_MINING;
    case PROP_TREE: return EMOTE_WOODCUTTING;
    case PROP_MUSHROOM: return EMOTE_HERBALISM;
    case PROP_CAMPFIRE: return EMOTE_COOKING;
    case PROP_BED:
    case PROP_BEDROLL: return EMOTE_SLEEPING;
    default: return EMOTE_NONE;
    }
}

// Whether units are put to work on a prop of this use. A passive crafting prop works by itself.
inline bool isWorkProp(PropUse use) { return use == PropUse::Resource || use == PropUse::Crafting; }
// Whether a prop of this kind keeps items, and opens the storage panel: a storage prop, a passive crafting prop, and a
// crafting prop that has an input item
inline bool hasStorage(const PropInfo& info) {
    return info.use == PropUse::Storage || info.use == PropUse::PassiveCrafting ||
           (info.use == PropUse::Crafting && info.input != 0);
}
// The occupiable props a unit sits on, rather than lies in
inline bool isSeat(int kind) { return kind == PROP_CHAIR || kind == PROP_BENCH || kind == PROP_STOOL; }

// The slots of a storage or crafting prop. A crafting prop's inventory takes only its input item.
constexpr int STORAGE_SLOTS = 12;
constexpr int STORAGE_COLUMNS = 6;

constexpr int PROP_COPIES = 3;         // how many of each kind a new level places
// Names how a level lays its props out, so a save can tell whether its prop serials still name the same props. It
// changes whenever the number of kinds or copies does. Props are placed a round of every kind at a time, so serial s
// is kind s % PROP_KIND_COUNT.
constexpr int PROP_LAYOUT = PROP_KIND_COUNT * 1000 + PROP_COPIES * 10 + 1;
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

// Lockpicking: each attempt takes LOCKPICK_SHARE of the time an item of work would at the same skill (a quarter hour to
// half an hour, in-game), and succeeds with a chance from 0% at Lockpicking 1 to 100% at 99
constexpr double LOCKPICK_SHARE = 0.25;
inline double lockpickChance(int level) {
    const int lv = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    return (double)(lv - 1) / (double)(MAX_LEVEL - 1);
}
constexpr float LOCKPICK_XP_SUCCESS = 20.0f;   // Lockpicking XP for opening a lock
constexpr float LOCKPICK_XP_FAIL = 5.0f;       // and for each attempt that fails

// A passive crafting prop makes one output item for each this many in-game hours that its fuel burns
constexpr double PASSIVE_CRAFT_HOURS = 1.0;
