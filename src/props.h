#pragma once
#include <cstdint>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "emotes.h"
#include "enum_index.h"
#include "sounds.h"
#include "stats.h"

// A kind of prop. data/props.txt says what every kind is, and can add kinds with no name here: they number on from these.
// Only the kinds the code has to ask for by name are named, and it is rarely the right thing to do: if a rule can be a
// column or flag of the table, make it one, so a new prop can use it.
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
    Boneheap,
    Stall,
    LockedChest,
    PrisonerCage,
    Chair,
    Bench,
    Stool,
    Table,
    EndTable,
    RoundTable,
    ResearchTable,
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
    Altar,
    Debris,
    Farm,
    Turbine,
    Stove,
    Airship,
    Foundation,
    WoodWall,
    WoodDoor,
    Ladder,
    UpperFloor,
    DragonCrystal,
    WoodStockpile,
    StoneStockpile,
    Stairs,
    Wheelbarrow,
    IronStockpile,
    OreStockpile,
    GameTable,
    NamedCount
};
constexpr int PROP_NAMED_KIND_COUNT = idx(PropKind::NamedCount);
// The key a save or level's prop goes by now: props that are gone load as the ones that took their place (the Shop as
// the Stall, the Metal Table as the Research Table), or as Debris (the Barrier)
inline std::string currentPropKey(const std::string& key) {
    if (key == "Shop") return "Stall";
    if (key == "MetalTable") return "ResearchTable";
    if (key == "Wall") return "Debris";
    if (key == "Bones") return "Boneheap";   // the Bones prop was renamed (the bones items are still Bones)
    return key;
}
// How many kinds data/props.txt has
int propKindCount();
struct BuildPick {
    enum class Tool { None, Destroy, Place, Adjust, Deconstruct, CancelBuilding };
    Tool tool = Tool::None;
    PropKind kind = PropKind::Wagon;

    static BuildPick destroy() { return BuildPick{ Tool::Destroy }; }
    static BuildPick place(PropKind k) { return BuildPick{ Tool::Place, k }; }
    // Adjust: how a prop is drawn apart from where it stands (app_prop_adjust.cpp)
    static BuildPick adjust() { return BuildPick{ Tool::Adjust }; }
    // Deconstruct and Cancel Construction: an area dragged out with the left button
    static BuildPick deconstruct() { return BuildPick{ Tool::Deconstruct }; }
    static BuildPick cancelBuilding() { return BuildPick{ Tool::CancelBuilding }; }
    bool areaTool() const { return tool == Tool::Deconstruct || tool == Tool::CancelBuilding; }
    bool adjusting() const { return tool == Tool::Adjust; }
    bool any() const { return tool != Tool::None; }
    bool placing() const { return tool == Tool::Place; }
    bool places(PropKind k) const { return tool == Tool::Place && kind == k; }
    bool operator==(const BuildPick& o) const { return tool == o.tool && (tool != Tool::Place || kind == o.kind); }
    bool operator!=(const BuildPick& o) const { return !(*this == o); }
};
enum class PropUse { None, Storage, Occupy, Resource, Crafting, PassiveCrafting };

// The Build tab's headings, in the order listed. props.txt's group column names one for each prop; Tools (Destroy and
// Adjust Position) and Developer (the spawn panel) are the tab's own and hold no prop.
enum class BuildGroup : int { Tools = 0, Claims, Crafting, Storage, Settlement, Furniture, Cages, Vehicles, Nature, Developer, Count };
inline constexpr const char* BUILD_GROUP_KEYS[] = { "tools", "claims", "crafting", "storage", "settlement", "furniture", "cages",
                                                    "vehicles", "nature", "developer" };
inline constexpr const char* BUILD_GROUP_LABELS[] = { "UILABEL_BUILD_GROUP_TOOLS", "UILABEL_BUILD_GROUP_CLAIMS", "UILABEL_BUILD_GROUP_CRAFTING",
                                                      "UILABEL_BUILD_GROUP_STORAGE", "UILABEL_BUILD_GROUP_SETTLEMENT",
                                                      "UILABEL_BUILD_GROUP_FURNITURE", "UILABEL_BUILD_GROUP_CAGES",
                                                      "UILABEL_BUILD_GROUP_VEHICLES", "UILABEL_BUILD_GROUP_NATURE",
                                                      "UILABEL_BUILD_GROUP_DEVELOPER" };

enum class ItemFilter { Any, RawFood, Food, Weapon, Armor, Water, Wood, Stone, Gizmo, Iron, Ore };

// The kinds of item a general store (a Barrel, a chest: storage that takes any item) can be set to allow, from its
// right-click menu's Allowed Items. By default it takes all but Wood Logs and Stone, which have stockpiles of their
// own, and bones, which go to the Altar.
enum class StoreCategory : int { Food = 0, RawFood, Water, Weapons, Armor, Ammo, Metal, Materials, Wood, Stone, Bones, Count };
inline constexpr const char* STORE_CATEGORY_KEYS[] = { "UILABEL_STORE_FOOD", "UILABEL_STORE_RAW_FOOD", "UILABEL_STORE_WATER",
                                                       "UILABEL_STORE_WEAPONS", "UILABEL_STORE_ARMOR", "UILABEL_STORE_AMMO",
                                                       "UILABEL_STORE_METAL", "UILABEL_STORE_MATERIALS", "UILABEL_STORE_WOOD",
                                                       "UILABEL_STORE_STONE", "UILABEL_STORE_BONES" };
static_assert(std::size(STORE_CATEGORY_KEYS) == (size_t)StoreCategory::Count);
inline constexpr std::uint32_t storeBit(StoreCategory c) { return 1u << (int)c; }
inline constexpr std::uint32_t STORE_ALL = (1u << (int)StoreCategory::Count) - 1u;
inline constexpr std::uint32_t STORE_DEFAULT =
    STORE_ALL & ~(storeBit(StoreCategory::Wood) | storeBit(StoreCategory::Stone) | storeBit(StoreCategory::Bones));

inline const char* itemFilterKey(ItemFilter f) {
    switch (f) {
    case ItemFilter::RawFood: return "FILTER_RAW_FOOD";
    case ItemFilter::Food: return "FILTER_FOOD";
    case ItemFilter::Weapon: return "FILTER_WEAPON";
    case ItemFilter::Armor: return "FILTER_ARMOR";
    case ItemFilter::Water: return "FILTER_WATER";
    case ItemFilter::Wood: return "FILTER_WOOD";
    case ItemFilter::Stone: return "FILTER_STONE";
    case ItemFilter::Gizmo: return "FILTER_GIZMO";
    case ItemFilter::Iron: return "FILTER_IRON";
    case ItemFilter::Ore: return "FILTER_ORE";
    default: return "";
    }
}

enum class PropAction { Inventory, Use, Occupy, Place, Unlock, Store, Lock, Destroy, Evacuate, Climb };

constexpr int INPUT_COOKABLE = -1;
constexpr int INPUT_BONES = -2;   // any bones (the Altar's upkeep)

struct Footprint {
    float halfW = 0.5f;
    float halfD = 0.5f;
    float radius = 0.0f;
};

inline float footprintReach(const Footprint& f) {
    return f.radius > 0.0f ? f.radius : std::sqrt(f.halfW * f.halfW + f.halfD * f.halfD);
}

inline bool insideFootprint(const Footprint& f, float dx, float dz, float yaw) {
    if (f.radius > 0.0f) return dx * dx + dz * dz <= f.radius * f.radius;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float u = dx * c - dz * s;
    const float v = dx * s + dz * c;
    return std::fabs(u) <= f.halfW && std::fabs(v) <= f.halfD;
}

struct PropInfo {
    std::string key;
    std::string nameKey;
    std::string model;
    float size = 1.0f;
    PropUse use = PropUse::None;
    StatId stat = StatId::None;
    int item = 0;
    int input = 0;
    int workers = 0;
    int lockLevel = 0;
    ItemFilter accepts = ItemFilter::Any;
    double fuelHours = 1.0;
    int altItem = 0;
    int altOneIn = 0;
    Footprint foot{};
    double speed = 1.0;
    double restedPerHour = 0.0;
    double comfortPerHour = 0.0;
    Sfx workSound = Sfx::Count;
    Sfx doneSound = Sfx::ItemMove;
    EmoteId emote = EmoteId::None;
    bool built = false;
    bool buildAnywhere = false;
    bool scatter = false;
    bool claimsLand = false;
    bool burnsCorpses = false;
    bool byHand = false;
    bool debris = false;
    bool junk = false;   // Clear Junk (a citizen task) clears it: a Debris is worked clear, any other is broken (props.txt flag junk)
    bool sleeping = false;       // a Sleeping prop (a bed or bedroll): an occupant lies in it and sleeps
    bool resting = false;        // a Resting prop (a chair, bench or stool): an occupant sits
    bool cage = false;
    bool airship = false;   // floats, and flies its occupants (up to workers of them) over any ground
    bool wheelbarrow = false;    // its occupant pushes it about in front of them, with its store
    bool pulled = false;         // with wheelbarrow: drawn behind its puller, slowing all but a large animal
    bool taming = false;         // with cage: only an animal goes in
    bool moves = false;          // a vehicle: goes wherever it is taken while the game runs (airship and wheelbarrow imply it)
    bool foundation = false;     // laid by dragging a rectangle; the pieces below are built on it
    bool onFoundation = false;   // only on a finished foundation tile of the builder's
    bool wallRule = false;       // a wall: its shape fits the walls around it, and it blocks walking
    bool door = false;           // goes where a wall can; strangers can't pass it while it is locked
    bool ladder = false;         // climbs to the upper floor
    bool stairs = false;         // a ladder 3 tiles long that is walked up: from its foot to the floor above past its top
    bool upperFloor = false;     // a floor at wall height
    bool perimeter = false;      // a Perimeter Wall or Gate: stands on open ground, covering tilesW by tilesD tiles, 10 times as strong
    bool solid = false;          // with perimeter: a wall, closing every tile it covers (a gate is walked through while it lets you); a Watchtower too
    bool perimeterPart = false;  // part of a faction's Perimeter (props.txt flag perimeter_part): adding, finishing or removing one makes its automatic Perimeter be looked at at once
    bool slope = false;          // a Slope: stairs with no hop, laid on open ground, three tiles long, its top step beside a Perimeter Wall's walkway
    bool tower = false;          // a Watchtower: one unit occupies it (as an Airship is), shut in on top and out of melee reach
    int tilesW = 1, tilesD = 1;  // the tiles it covers, across and deep when it faces east (props.txt's tiles:W:D flag)
    bool rare = false;           // a new level scatters only one
    bool randomYaw = false;      // faces a way of its own, by where it stands, whenever one comes into being
    bool advanced = false;       // an Advanced Crafting prop: it makes the recipe picked for it (src/recipes.h)
    bool needsCamp = false;      // placed only by a Campfire or in an Altar's claim of its faction; it falls to Debris outside one
    bool fire = false;           // a fire burns on it: drawn as flames and a glow, at this spot from its base, this big
    float fireX = 0.0f, fireY = 0.0f, fireZ = 0.0f, fireSize = 0.4f;
    double materials = 0.0;      // building material a blueprint needs (Wood Logs, or the material picked); 0: placed finished
    double buildSeconds = 0.0;   // building it needs at Carpentry 1 (per tile for a foundation)
    double health = 0.0;         // damage it takes to break it, from the optional health column or its size; 0: none (nature, debris, pieces)
    int slots = 12;              // a store's slots (props.txt's optional slots column; STORAGE_SLOTS by default)
    int capacity = 0;            // the most items a store holds in all, or 0 for as many as its slots fit
    bool stall = false;
    bool exterior = false;       // goes outside buildings only, never on a foundation          // sells to other factions' units, manned by a shopkeeper
    BuildGroup group = BuildGroup::Nature;   // the Build tab's heading it is listed under
};

// The tiles a prop that covers several (tilesW by tilesD) takes when it stands at (cx, cz) turned by yaw: its centre is on a
// tile corner, so an even width or depth is whole tiles to each side
inline void propTileRect(const PropInfo& i, float cx, float cz, float yaw, int& x0, int& z0, int& x1, int& z1) {
    int w = i.tilesW, d = i.tilesD;
    if (std::fabs(std::sin(yaw)) > 0.7f) std::swap(w, d);
    x0 = (int)std::floor(cx - (float)w * 0.5f + 0.5f);
    z0 = (int)std::floor(cz - (float)d * 0.5f + 0.5f);
    x1 = x0 + w - 1;
    z1 = z0 + d - 1;
}

// How often what is drawn of a prop changes. A still prop stands where it was put and looks the same until it is built,
// broken or moved by hand; a door stays on its tile but swings open while someone is in its doorway; a moving prop (a
// vehicle) goes wherever it is taken. The renderer draws them in that order, and only still props could be kept drawn
// from one frame to the next.
enum class PropMotion { Still, Swings, Moves };
inline PropMotion propMotion(const PropInfo& p) {
    return p.moves ? PropMotion::Moves : p.door ? PropMotion::Swings : PropMotion::Still;
}

// Whether the item is what the prop takes as its input (an item, or any bones)
bool inputMatches(const PropInfo& info, int item);
// From data/props.txt. Any number outside the table gets its first row.
const PropInfo& propInfo(PropKind kind);

// A Sleeping prop (props.txt's sleeping flag), which is also what gives rested
inline bool isBed(PropKind kind) { return propInfo(kind).sleeping; }
// The kinds a new level scatters (PROP_COPIES of each). The others come only from building, camps or debris.
const std::vector<PropKind>& scatteredPropKinds();
// In the table's order, the Build tab's
const std::vector<PropKind>& propKindsListed();

constexpr double DEBRIS_CLEAR_HOURS = 1.0;
constexpr float DEBRIS_CLEAR_XP = 20.0f;
constexpr int BONES_ITEM_COUNT = 3;
inline bool destroyedByHand(PropKind kind) { return propInfo(kind).byHand; }
// Broken with the right-click menu's Break: every prop but Debris itself (worked to clear it). One destroyed by hand
// leaves its item or bones; any other leaves Debris.
inline bool breakableKind(PropKind kind) { return !propInfo(kind).debris; }

inline bool yieldsItems(PropKind kind) { return propInfo(kind).use == PropUse::Resource && propInfo(kind).item > 0; }

inline bool isWorkProp(PropUse use) { return use == PropUse::Resource || use == PropUse::Crafting; }
inline bool hasStorage(const PropInfo& info) {
    return info.use == PropUse::Storage || info.use == PropUse::PassiveCrafting || info.wheelbarrow ||
           (info.use == PropUse::Crafting && info.input != 0) || info.advanced;
}
// A Resting prop (props.txt's resting flag)
inline bool isSeat(PropKind kind) { return propInfo(kind).resting; }

constexpr int STORAGE_SLOTS = 12;
constexpr int STORAGE_COLUMNS = 6;

constexpr int PROP_COPIES = 3;
// One row of the Build tab: a heading, a pick (Destroy, or a prop to place), or the Developer entry that opens the spawn
// panel (first). Under each heading in BuildGroup's order: Destroy first under Claims, then that group's props in props.txt's order.
struct BuildRow {
    BuildGroup group = BuildGroup::Claims;
    bool heading = false;
    bool developer = false;
    BuildPick pick;
    bool blueprints = false;   // the Building Blueprint entry (under Adjust Position), which opens the plans popout
};
inline std::vector<BuildRow> buildRows() {
    std::vector<BuildRow> rows;
    // The Developer Menu is at the top
    rows.push_back(BuildRow{ BuildGroup::Developer, true, false, {} });
    rows.push_back(BuildRow{ BuildGroup::Developer, false, true, {} });
    for (int g = 0; g < (int)BuildGroup::Count; ++g) {
        const BuildGroup group = (BuildGroup)g;
        // Nature is spawned from the Developer Menu only
        if (group == BuildGroup::Nature || group == BuildGroup::Developer) continue;
        rows.push_back(BuildRow{ group, true, false, {} });
        if (group == BuildGroup::Tools) {
            rows.push_back(BuildRow{ group, false, false, BuildPick::destroy() });
            rows.push_back(BuildRow{ group, false, false, BuildPick::deconstruct() });
            rows.push_back(BuildRow{ group, false, false, BuildPick::cancelBuilding() });
            rows.push_back(BuildRow{ group, false, false, BuildPick::adjust() });
            BuildRow plans{ group, false, false, {} };
            plans.blueprints = true;
            rows.push_back(plans);
        }
        for (PropKind k : propKindsListed()) {
            if (propInfo(k).group == group) rows.push_back(BuildRow{ group, false, false, BuildPick::place(k) });
        }
    }
    return rows;
}

constexpr float PROP_RANGE = 40.0f;

constexpr float PROP_REACH = 2.0f;
constexpr float EVACUATE_OCCUPY_BAN_SECONDS = 10.0f;
constexpr float STORAGE_CLOSE_RANGE = 2.5f;
constexpr float PROP_BLOCK_RADIUS = 0.45f;

inline double workHoursFor(int level) {
    const int lv = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    return 1.0 + (double)(MAX_LEVEL - lv) / (double)(MAX_LEVEL - 1);
}

constexpr float WORK_XP_PER_ITEM = 20.0f;

constexpr double LOCKPICK_SHARE = 0.25;
inline double lockpickChance(int level) {
    const int lv = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    return (double)(lv - 1) / (double)(MAX_LEVEL - 1);
}
constexpr float LOCKPICK_XP_SUCCESS = 20.0f;
constexpr float LOCKPICK_XP_FAIL = 5.0f;

constexpr double PASSIVE_CRAFT_HOURS = 1.0;
