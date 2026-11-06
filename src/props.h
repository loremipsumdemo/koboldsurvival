#pragma once
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
    Altar,
    Debris,
    Shop,
    Farm,
    Turbine,
    Stove,
    NamedCount
};
constexpr int PROP_NAMED_KIND_COUNT = idx(PropKind::NamedCount);
// How many kinds data/props.txt has
int propKindCount();
struct BuildPick {
    enum class Tool { None, Destroy, Place };
    Tool tool = Tool::None;
    PropKind kind = PropKind::Wagon;

    static BuildPick destroy() { return BuildPick{ Tool::Destroy }; }
    static BuildPick place(PropKind k) { return BuildPick{ Tool::Place, k }; }
    bool any() const { return tool != Tool::None; }
    bool placing() const { return tool == Tool::Place; }
    bool places(PropKind k) const { return tool == Tool::Place && kind == k; }
    bool operator==(const BuildPick& o) const { return tool == o.tool && (tool != Tool::Place || kind == o.kind); }
    bool operator!=(const BuildPick& o) const { return !(*this == o); }
};
enum class PropUse { None, Storage, Occupy, Resource, Crafting, PassiveCrafting };

enum class ItemFilter { Any, RawFood, Food, Weapon, Armor, Water };

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

enum class PropAction { Inventory, Use, Occupy, Place, Unlock, Store, Lock, Destroy, Evacuate };

constexpr int INPUT_COOKABLE = -1;

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
    bool seat = false;
    bool cage = false;
};

// From data/props.txt. Any number outside the table gets its first row.
const PropInfo& propInfo(PropKind kind);

inline bool isBed(PropKind kind) { return propInfo(kind).restedPerHour > 0.0; }
// The kinds a new level scatters (PROP_COPIES of each). The others come only from building, camps or debris.
const std::vector<PropKind>& scatteredPropKinds();
// In the table's order, the Build tab's
const std::vector<PropKind>& propKindsListed();

constexpr double DEBRIS_CLEAR_HOURS = 1.0;
constexpr float DEBRIS_CLEAR_XP = 20.0f;
constexpr int BONES_ITEM_COUNT = 3;
inline bool destroyedByHand(PropKind kind) { return propInfo(kind).byHand; }

inline bool yieldsItems(PropKind kind) { return propInfo(kind).use == PropUse::Resource && propInfo(kind).item > 0; }

inline bool isWorkProp(PropUse use) { return use == PropUse::Resource || use == PropUse::Crafting; }
inline bool hasStorage(const PropInfo& info) {
    return info.use == PropUse::Storage || info.use == PropUse::PassiveCrafting ||
           (info.use == PropUse::Crafting && info.input != 0);
}
inline bool isSeat(PropKind kind) { return propInfo(kind).seat; }

constexpr int STORAGE_SLOTS = 12;
constexpr int STORAGE_COLUMNS = 6;

constexpr int PROP_COPIES = 3;
inline int buildRowCount() { return propKindCount() + 1; }
inline BuildPick buildRowPick(int row) {
    if (row <= 0) return BuildPick::destroy();
    const std::vector<PropKind>& order = propKindsListed();
    return row - 1 < (int)order.size() ? BuildPick::place(order[(size_t)row - 1]) : BuildPick{};
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
