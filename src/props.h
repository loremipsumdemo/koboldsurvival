#pragma once
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>
#include "emotes.h"
#include "enum_index.h"
#include "stats.h"

// Saved as numbers: after reordering, raise SAVE_VERSION
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
    Count
};
constexpr int PROP_KIND_COUNT = idx(PropKind::Count);
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
// The kinds a new level scatters (PROP_COPIES of each). The kinds after them come only from building, camps or debris.
constexpr int PROP_PLACED_KIND_COUNT = idx(PropKind::Altar);

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

enum class PropAction { Inventory, Use, Occupy, Place, Unlock, Store, Lock, Destroy };

constexpr int INPUT_COOKABLE = -1;
constexpr double ALTAR_LOG_HOURS = 4.0;

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
    Footprint foot{};
    bool built = false;
};

inline const PropInfo& propInfo(PropKind kind) {
    static const PropInfo info[PROP_KIND_COUNT] = {
        { "PROP_WAGON",          1.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_CAMPFIRE",       0.8f, PropUse::PassiveCrafting, StatId::Cooking, 0, INPUT_COOKABLE, 0, 0,
          ItemFilter::Any, 1.0 },
        { "PROP_BEDROLL",        1.0f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "PROP_BED",            1.4f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "PROP_BARREL",         0.7f, PropUse::Storage,  StatId::None, 0, 0, 0, 0 },
        { "PROP_ORE",            1.0f, PropUse::Resource, StatId::Mining, 1, 0, 3, 0 },
        { "PROP_TREE",           3.0f, PropUse::Resource, StatId::Woodcutting, 88, 0, 3, 0 },
        { "PROP_BOULDER",        1.2f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_MUSHROOM",       0.6f, PropUse::Resource, StatId::Herbalism, 125, 0, 3, 0,
          ItemFilter::Any, 1.0, 126, 10 },
        { "PROP_BONES",          1.2f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_WALL",           2.0f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_STALL",          2.0f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_LOCKED_CHEST",   0.9f, PropUse::Storage,  StatId::None, 0, 0, 0, 5 },
        { "PROP_PRISONER_CAGE",  1.8f, PropUse::Occupy,   StatId::None, 0, 0, 0, 15 },
        { "PROP_CHAIR",          0.9f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "PROP_BENCH",          1.5f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "PROP_STOOL",          0.5f, PropUse::Occupy,   StatId::None, 0, 0, 0, 0 },
        { "PROP_TABLE",          1.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_END_TABLE",      0.7f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_ROUND_TABLE",    1.2f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_METAL_TABLE",    1.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_STANDING_TORCH", 1.6f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_SUPPORT_BEAM",   2.5f, PropUse::None,     StatId::None, 0, 0, 0, 0 },
        { "PROP_TRAINING_DUMMY", 1.6f, PropUse::Crafting, StatId::Melee, 0, 0, 1, 0 },
        { "PROP_RAW_FOOD_STORAGE", 1.0f, PropUse::Storage, StatId::None, 0, 0, 0, 0, ItemFilter::RawFood },
        { "PROP_FOOD_STORAGE",   1.0f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Food },
        { "PROP_WEAPON_RACK",    1.4f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Weapon },
        { "PROP_ARMOR_STAND",    1.7f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Armor },
        { "PROP_WATER_TANK",     1.4f, PropUse::Storage,  StatId::None, 0, 0, 0, 0, ItemFilter::Water },
        { "PROP_WATER_PUMP",     1.4f, PropUse::Crafting, StatId::Strength, 91, 0, 1, 0 },
        { "PROP_FORGE",          1.6f, PropUse::PassiveCrafting, StatId::Smelting, 92, 1, 0, 0,
          ItemFilter::Any, 1.0 },
        { "PROP_ALTAR",             1.2f, PropUse::PassiveCrafting, StatId::None, 0, 88, 0, 0,
          ItemFilter::Any, ALTAR_LOG_HOURS, 0, 0, Footprint{}, true },
        { "PROP_DEBRIS",            1.0f, PropUse::Resource, StatId::Strength, 0, 0, 3, 0 },
        { "PROP_SHOP",              1.6f, PropUse::Storage, StatId::None, 0, 0, 0, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
        { "PROP_FARM",              2.0f, PropUse::Storage, StatId::None, 0, 0, 0, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
        { "PROP_TURBINE",           2.0f, PropUse::None, StatId::None, 0, 0, 0, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
        { "PROP_STOVE",             1.2f, PropUse::Crafting, StatId::Cooking, 0, INPUT_COOKABLE, 1, 0,
          ItemFilter::Any, 1.0, 0, 0, Footprint{}, true },
    };
    const int k = idx(kind);
    return info[(k >= 0 && k < PROP_KIND_COUNT) ? k : 0];
}

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

constexpr double DEBRIS_CLEAR_HOURS = 1.0;
constexpr float DEBRIS_CLEAR_XP = 20.0f;
constexpr int BONES_ITEM = 154;
constexpr int BONES_ITEM_COUNT = 3;
inline bool destroyedByHand(PropKind kind) { return kind == PropKind::Bones; }

inline bool yieldsItems(PropKind kind) { return propInfo(kind).use == PropUse::Resource && propInfo(kind).item > 0; }

inline bool isWorkProp(PropUse use) { return use == PropUse::Resource || use == PropUse::Crafting; }
inline bool hasStorage(const PropInfo& info) {
    return info.use == PropUse::Storage || info.use == PropUse::PassiveCrafting ||
           (info.use == PropUse::Crafting && info.input != 0);
}
inline bool isSeat(PropKind kind) { return kind == PropKind::Chair || kind == PropKind::Bench || kind == PropKind::Stool; }

constexpr int STORAGE_SLOTS = 12;
constexpr int STORAGE_COLUMNS = 6;

constexpr int PROP_COPIES = 3;
inline int buildRowCount() { return PROP_KIND_COUNT + 1; }
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

constexpr float PROP_RANGE = 40.0f;

constexpr float PROP_REACH = 2.0f;
constexpr float STORAGE_CLOSE_RANGE = 2.5f;
constexpr float PROP_BLOCK_RADIUS = 0.45f;

inline double workHoursFor(int level) {
    const int lv = level < 1 ? 1 : (level > MAX_LEVEL ? MAX_LEVEL : level);
    return 1.0 + (double)(MAX_LEVEL - lv) / (double)(MAX_LEVEL - 1);
}

constexpr double CRAFT_SPEED_STOVE = 10.0;
inline double resourceSpeedFor(PropKind kind) {
    if (kind == PropKind::Mushroom) return 8.0;
    if (kind == PropKind::Stove) return CRAFT_SPEED_STOVE;
    return propInfo(kind).use == PropUse::Resource ? 4.0 : 1.0;
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
