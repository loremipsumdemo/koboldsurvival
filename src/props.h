#pragma once
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>
#include "emotes.h"
#include "enum_index.h"
#include "stats.h"

// Props: 3D models placed on the map, such as wagons, campfires and trees. They do not block movement and cannot be
// picked up. Each prop is an entity with a Prop component (components.h) and a Transform. By use:
//   Decor props (the wagon, tables, torches) are only scenery.
//   Storage props (the barrel) hold items that a unit standing by them can take or put in. Some take only one sort of
//   item (PropInfo::accepts): the Weapon Rack only weapons, the Water Tank only water.
//   Occupiable props (beds, chairs) hold one unit, which stays on them until it is given another order.
//   Resource props (ore, trees and mushrooms) are worked by units, and give an item now and then.
//   Crafting props (the stove) have an inventory that takes only the items they use (raw meat for the stove).
//   A unit working one is like one working a resource prop, except that each item it makes uses up one input item,
//   checked at the moment the item is finished, and goes straight into the worker's inventory. A crafting prop with
//   no input has no inventory: the Water Pump gives water for the work alone, and the Training Dummy gives no item,
//   only skill XP.
//   Passive crafting props (the forge, the campfire) work by themselves, with no unit at them. Each input item they
//   take is burned as fuel for fuelHours in-game hours, and while it burns they make one output item every
//   PASSIVE_CRAFT_HOURS into their own output inventory. They pause while the output has no room, and go on once it
//   has. The campfire (INPUT_COOKABLE) takes any raw meat, and what it makes is that meat cooked.
// Storage and crafting props keep their items in a Storage component; a passive crafting prop's output is an Output.
// Lockable: a tag any storage or occupiable prop kind can have (PropInfo::lockLevel above 0). A lockable prop has a
// Lock (components.h), which starts locked. While locked it can't be opened or used; a unit with at least lockLevel in
// Lockpicking can right-click it and Unlock it, a timed attempt at a time, like working ore. A lockable occupiable
// prop (the Prisoner Cage) locks again as soon as a new unit gets in, and the unit can't leave until it is unlocked, by
// itself from the inside or by another unit from the outside. A mob can't work a lock, so a mob in a locked cage is shut
// in even when the lock is its own faction's. Otherwise an unlocked prop stays unlocked until a unit is sent to Lock it
// (the right-click menu); one whose lock is open to the selected units (their own) can likewise be Unlocked with no
// lockpicking. A prop with no lock owner takes the faction of whoever locks it.
// Camps keep their mobs in locked Prisoner Cages, and such a mob's name ends in " [!]" (mobs.h).
// Built props: the kinds from PropKind::Altar on are not placed by a new level. The player builds them from the Build
// tab. An Altar is a passive crafting prop that burns Wood Logs as upkeep (the Forge's fuel code, with no output) and
// holds its faction's claim (claims.h).

enum class PropKind : int {
    Wagon,
    Campfire,
    Bedroll,
    Bed,
    Barrel,
    Ore,
    Tree,
    Boulder,
    Mushroom,
    Bones,
    Wall,
    Stall,
    LockedChest,
    PrisonerCage,
    Chair,
    Bench,
    Stool,
    Table,
    EndTable,
    RoundTable,
    MetalTable,
    StandingTorch,
    SupportBeam,
    TrainingDummy,
    RawFoodStorage,
    FoodStorage,
    WeaponRack,
    ArmorStand,
    WaterTank,
    WaterPump,
    Forge,
    Altar,                // built: holds its faction's claim, and burns its upkeep
    Debris,               // built: what is left of a decayed or destroyed building
    Shop,                 // built: a storage prop
    Farm,                 // built: a storage prop
    Turbine,              // built: decor
    Stove,                // built: a crafting prop that cooks raw meat, worked by a unit, fast
    Count
};
constexpr int PROP_KIND_COUNT = idx(PropKind::Count);
// The Build tab's rows: row 0 is Destroy, a tool that takes any prop down (Debris is cleared away). Then the Bedroll,
// the Campfire, the Altar, and every other kind in order.
// What the Build tab has picked: nothing, the Destroy tool, or a kind of prop to place
struct BuildPick {
    enum class Tool { None, Destroy, Place };
    Tool tool = Tool::None;
    PropKind kind = PropKind::Wagon;   // the kind to place, when tool is Place

    static BuildPick destroy() { return BuildPick{ Tool::Destroy }; }
    static BuildPick place(PropKind k) { return BuildPick{ Tool::Place, k }; }
    bool any() const { return tool != Tool::None; }
    bool placing() const { return tool == Tool::Place; }
    bool places(PropKind k) const { return tool == Tool::Place && kind == k; }
    bool operator==(const BuildPick& o) const { return tool == o.tool && (tool != Tool::Place || kind == o.kind); }
    bool operator!=(const BuildPick& o) const { return !(*this == o); }
};
// The kinds a new level places (PROP_COPIES of each, props.h below). The kinds after them are built by the player.
constexpr int PROP_PLACED_KIND_COUNT = idx(PropKind::Altar);

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
// prop's items; Use works a resource prop or starts crafting; Occupy takes an occupiable prop (a bed, a chair, a cage)
// itself; Place puts the unit it carries on an occupiable prop, or a corpse it carries into a campfire; Unlock picks a locked prop's lock; Store (an
// Auto-Haul's step, never on the menu) puts what the unit carries that the prop takes into it
enum class PropAction { Inventory, Use, Occupy, Place, Unlock, Store, Lock, Destroy };

// What a kind of prop looks like, is called and does.
// file: its model, in the pak: data/model/model_<n>.glb, numbered from 1 in PropKind order (data/modelinfo.txt lists
//       them). A missing model is logged and drawn as a pink square.
// nameKey: its name in the language files, shown in the hover list and the storage panel.
// size: the model is scaled so its longest side is this many tiles.
// use: what right-clicking it does.
// For resource, crafting and passive crafting props only:
//   stat: the skill that sets how fast a unit works it (workHoursFor)
//   item: the item it gives the worker: what a resource prop yields, or what a crafting prop makes (an id from
//         data/itemdb.txt)
//   input: the item a crafting prop uses up, one for each item it makes; 0 for a resource prop, and for a crafting
//          prop that needs none (then it has no inventory). A passive crafting prop burns it as fuel.
//          INPUT_COOKABLE: any item with a cooks_into in data/itemdb.txt, made into that item (the campfire, the
//          stove). Its item
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
constexpr double ALTAR_LOG_HOURS = 4.0;   // in-game hours of upkeep one Wood Log gives an Altar: six logs a day

// The ground a prop's shape covers, in tiles from its centre, before its facing turns it (yaw). A rectangle is 2*halfW
// across along the prop's own x axis and 2*halfD along its z axis. A circle (radius above 0) replaces the rectangle.
// Most props stand on one tile, which is the default.
struct Footprint {
    float halfW = 0.5f;
    float halfD = 0.5f;
    float radius = 0.0f;
};

// The farthest a footprint reaches from its centre, for a quick check that two shapes are near each other
inline float footprintReach(const Footprint& f) {
    return f.radius > 0.0f ? f.radius : std::sqrt(f.halfW * f.halfW + f.halfD * f.halfD);
}

// Whether the point (dx, dz) from a shape's centre lies inside it, turned by yaw. The turn is the one the model is drawn
// with (placeModel in model.h): the prop's x axis points along (cos yaw, -sin yaw), its z axis along (sin yaw, cos yaw).
inline bool insideFootprint(const Footprint& f, float dx, float dz, float yaw) {
    if (f.radius > 0.0f) return dx * dx + dz * dz <= f.radius * f.radius;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float u = dx * c - dz * s;   // along the prop's x axis
    const float v = dx * s + dz * c;   // along its z axis
    return std::fabs(u) <= f.halfW && std::fabs(v) <= f.halfD;
}

struct PropInfo {
    const char* file;
    const char* nameKey;
    float size;
    PropUse use;
    StatId stat;
    int item;
    int input;
    int workers;
    int lockLevel;
    ItemFilter accepts = ItemFilter::Any;
    double fuelHours = 1.0;
    int altItem = 0;
    int altOneIn = 0;
    Footprint foot{};            // the ground it covers: one tile, unless it is larger
    bool built = false;          // a kind the player builds from the Build tab: owned by a faction (claims.h)
};

inline const PropInfo& propInfo(PropKind kind) {
    static const PropInfo info[PROP_KIND_COUNT] = {
        { "data/model/model_1.glb",  "PROP_WAGON",          1.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_2.glb",  "PROP_CAMPFIRE",       0.8f, PropUse::PassiveCrafting, StatId::Cooking, 0, INPUT_COOKABLE, 0, 0,
          ItemFilter::Any, 1.0 },                                                                            // raw meat -> cooked, by itself
        { "data/model/model_3.glb",  "PROP_BEDROLL",        1.0f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "data/model/model_4.glb",  "PROP_BED",            1.4f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "data/model/model_5.glb",  "PROP_BARREL",         0.7f, PropUse::Storage,  StatId::None, 0, 0, 0, 0 },
        { "data/model/model_6.glb",  "PROP_ORE",            1.0f, PropUse::Resource, StatId::Mining, 1, 0, 3, 0 },       // Iron Ore
        { "data/model/model_7.glb",  "PROP_TREE",           3.0f, PropUse::Resource, StatId::Woodcutting, 88, 0, 3, 0 }, // Wood Log
        { "data/model/model_8.glb",  "PROP_BOULDER",        1.2f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_9.glb",  "PROP_MUSHROOM",       0.6f, PropUse::Resource, StatId::Herbalism, 125, 0, 3, 0,
          ItemFilter::Any, 1.0, 126, 10 },                                                                   // Mushroom; 1 in 10 Poison Mushroom
        { "data/model/model_10.glb", "PROP_BONES",          1.2f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_11.glb", "PROP_WALL",           2.0f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_12.glb", "PROP_STALL",          2.0f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_13.glb", "PROP_LOCKED_CHEST",   0.9f, PropUse::Storage,  StatId::None, 0, 0, 0, 5 },    // Lockable
        { "data/model/model_14.glb", "PROP_PRISONER_CAGE",  1.8f, PropUse::Occupy,   StatId::None, 0, 0, 0, 15 },   // Lockable
        { "data/model/model_15.glb", "PROP_CHAIR",          0.9f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "data/model/model_16.glb", "PROP_BENCH",          1.5f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "data/model/model_17.glb", "PROP_STOOL",          0.5f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "data/model/model_18.glb", "PROP_TABLE",          1.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_19.glb", "PROP_END_TABLE",      0.7f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_20.glb", "PROP_ROUND_TABLE",    1.2f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_21.glb", "PROP_METAL_TABLE",    1.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_22.glb", "PROP_STANDING_TORCH", 1.6f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_23.glb", "PROP_SUPPORT_BEAM",   2.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "data/model/model_24.glb", "PROP_TRAINING_DUMMY", 1.6f, PropUse::Crafting, StatId::Melee, 0, 0, 1, 0 },        // XP only
        { "data/model/model_25.glb", "PROP_RAW_FOOD_STORAGE", 1.0f, PropUse::Storage, StatId::None, 0, 0, 0, 0, ItemFilter::RawFood },
        { "data/model/model_26.glb", "PROP_FOOD_STORAGE",   1.0f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Food },
        { "data/model/model_27.glb", "PROP_WEAPON_RACK",    1.4f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Weapon },
        { "data/model/model_28.glb", "PROP_ARMOR_STAND",    1.7f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Armor },
        { "data/model/model_29.glb", "PROP_WATER_TANK",     1.4f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Water },
        { "data/model/model_30.glb", "PROP_WATER_PUMP",     1.4f, PropUse::Crafting, StatId::Strength, 91, 0, 1, 0 },    // Water
        { "data/model/model_31.glb", "PROP_FORGE",          1.6f, PropUse::PassiveCrafting, StatId::Smelting, 92, 1, 0, 0,
          ItemFilter::Any, 1.0 },                                                                            // Iron Ore -> Iron Ingot
        // Built props (see the top of this file).
        // The Altar: passive crafting with no output. Wood Logs are its upkeep, each burning ALTAR_LOG_HOURS in-game
        // hours (the Forge's fuel code), so its input is the Wood Log and it makes nothing (item 0). One camp
        // woodcutter stores ten or more logs a day, so it keeps an Altar burning six a day fed with some to spare.
        { "data/model/model_32.glb", "PROP_ALTAR",             1.2f, PropUse::PassiveCrafting, StatId::None, 0, 88, 0, 0,
          ItemFilter::Any, ALTAR_LOG_HOURS, 0, 0, Footprint{}, true },
        // Debris: worked like ore, but it gives no item. DEBRIS_CLEAR_HOURS of one unit's work clears it away.
        { "data/model/model_33.glb", "PROP_DEBRIS",            1.0f, PropUse::Resource, StatId::Strength, 0, 0, 3, 0 },
        { "data/model/model_39.glb", "PROP_SHOP",              1.6f, PropUse::Storage, StatId::None, 0, 0, 0, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
        { "data/model/model_40.glb", "PROP_FARM",              2.0f, PropUse::Storage, StatId::None, 0, 0, 0, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
        { "data/model/model_41.glb", "PROP_TURBINE",           2.0f, PropUse::None, StatId::None, 0, 0, 0, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
        // The Stove: a crafting prop a unit works, cooking raw meat like the Campfire but CRAFT_SPEED_STOVE times faster
        { "data/model/model_42.glb", "PROP_STOVE",             1.2f, PropUse::Crafting, StatId::Cooking, 0, INPUT_COOKABLE, 1, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
    };
    const int k = idx(kind);
    return info[(k >= 0 && k < PROP_KIND_COUNT) ? k : 0];
}

// The emote shown over a unit using a prop of this kind: working it, or lying on it. EmoteId::None for none.
inline EmoteId propEmote(PropKind kind) {
    switch (kind) {
    case PropKind::Ore: return EmoteId::Mining;
    case PropKind::Tree: return EmoteId::Woodcutting;
    case PropKind::Mushroom: return EmoteId::Herbalism;
    case PropKind::Stove: return EmoteId::Cooking;
    case PropKind::Bed:
    case PropKind::Bedroll: return EmoteId::Sleeping;
    default: return EmoteId::None;
    }
}

// In-game hours of one unit's work that clear a Debris away, whatever its skill. Two units take half as long.
constexpr double DEBRIS_CLEAR_HOURS = 1.0;
constexpr float DEBRIS_CLEAR_XP = 20.0f;   // Strength XP for each unit that helped clear one
// Bones: scenery, and what a corpse turns into (corpseDecaySystem in systems.h), holding its items out of reach
// (BonesPile). It goes by itself after a day (bonesDecaySystem), or when a unit sent with the right-click menu's Destroy
// takes it away, whoever's claim it lies in. Either way what it held and BONES_ITEM_COUNT of the Bones item fall there.
constexpr int BONES_ITEM = 154;
constexpr int BONES_ITEM_COUNT = 3;
inline bool destroyedByHand(PropKind kind) { return kind == PropKind::Bones; }

// Whether a resource prop gives items when worked: everything but the Debris, which is only cleared
inline bool yieldsItems(PropKind kind) { return propInfo(kind).use == PropUse::Resource && propInfo(kind).item > 0; }

// Whether units are put to work on a prop of this use. A passive crafting prop works by itself.
inline bool isWorkProp(PropUse use) { return use == PropUse::Resource || use == PropUse::Crafting; }
// Whether a prop of this kind keeps items, and opens the storage panel: a storage prop, a passive crafting prop, and a
// crafting prop that has an input item
inline bool hasStorage(const PropInfo& info) {
    return info.use == PropUse::Storage || info.use == PropUse::PassiveCrafting ||
           (info.use == PropUse::Crafting && info.input != 0);
}
// The occupiable props a unit sits on, rather than lies in
inline bool isSeat(PropKind kind) { return kind == PropKind::Chair || kind == PropKind::Bench || kind == PropKind::Stool; }

// The slots of a storage or crafting prop. A crafting prop's inventory takes only its input item.
constexpr int STORAGE_SLOTS = 12;
constexpr int STORAGE_COLUMNS = 6;

constexpr int PROP_COPIES = 3;         // how many of each kind a new level places
// Names how a level lays its props out, so a save can tell whether its prop serials still name the same props. It
// changes whenever the number of kinds or copies does, and its last digit whenever the placement itself changes (2: the
// factions' camps come first; 3: every faction and the player get a camp at the compass points, with trees, extra props
// and serials from CAMP_SERIAL_BASE; 4: the camps' vaults are placed last, so they take the last camp serials; 5: a
// camp's Altar is part of its kit, and it has no foundation or vault). Props
// are placed a round of every placed kind at a time, so serial s is kind s % PROP_PLACED_KIND_COUNT.
constexpr int PROP_LAYOUT = PROP_PLACED_KIND_COUNT * 1000 + PROP_COPIES * 10 + 5;   // built kinds are not placed
// The order of the Build tab's rows after Destroy: the Bedroll and Campfire (which can be built anywhere), then the
// claim pieces, then every other kind in PropKind order
inline int buildRowCount() { return PROP_KIND_COUNT + 1; }
// What row picks: Destroy for row 0, a kind for the rest, nothing past the last row
inline BuildPick buildRowPick(int row) {
    static const int first[] = { idx(PropKind::Bedroll), idx(PropKind::Campfire), idx(PropKind::Altar) };
    static const std::vector<int> order = [] {
        std::vector<int> o(std::begin(first), std::end(first));
        for (int k = 0; k < PROP_KIND_COUNT; ++k) {
            if (std::find(o.begin(), o.end(), k) == o.end()) o.push_back(k);
        }
        return o;
    }();
    if (row <= 0) return BuildPick::destroy();
    return row - 1 < (int)order.size() ? BuildPick::place((PropKind)order[(size_t)row - 1]) : BuildPick{};
}

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

// How many times faster than workHoursFor a resource prop is worked: a Mushroom 8 times, any other resource 4 times.
// Crafting is not sped up, but for the Stove: CRAFT_SPEED_STOVE times, a tenth of the Campfire's hour or less an item.
constexpr double CRAFT_SPEED_STOVE = 10.0;
inline double resourceSpeedFor(PropKind kind) {
    if (kind == PropKind::Mushroom) return 8.0;
    if (kind == PropKind::Stove) return CRAFT_SPEED_STOVE;
    return propInfo(kind).use == PropUse::Resource ? 4.0 : 1.0;
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
