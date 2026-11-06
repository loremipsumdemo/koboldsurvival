#pragma once
#include <algorithm>
#include "enum_index.h"
#include "stats.h"

// Saved as numbers: after reordering, raise SAVE_VERSION
enum class MobKind : int {
    None = 0,
    Bear, Cow, Chicken, Boar, Wolf, Deer, Rabbit, Fish, Crab, Lizard,
    Snake, Pig, Sheep, Duck, Turtle, Bat, Frog, Gorlicorn, Griffin, Manticore,
    End
};
constexpr int MOB_KIND_COUNT = idx(MobKind::End) - 1;
constexpr int MOB_FACTION_BASE = 10;
constexpr int CAMP_MOBS = 3;
constexpr int WILD_MOBS = 10;

enum class MobCategory { Regular, Boss };

struct MobInfo {
    const char* nameKey;
    int meat;
    int meatCount;
    int strength, dexterity, vitality, agility;
    int melee, block;
    int wild = WILD_MOBS;
    int boneCount = 0;
    bool maxStats = false;
    int hp = 0;
    const char* model = nullptr;   // data/model/mob/<model>.glb
    float modelHeight = 1.2f;
    bool patrolSpawn = false;
    MobCategory category = MobCategory::Regular;
    int bones = 0;   // 0: MOB_BONES_FIRST + kind - 1
};

constexpr int GORLICORN_HP = 2000;
constexpr int GRIFFIN_HP = 10000;
constexpr int MANTICORE_HP = 5000;
constexpr float BOSS_DAMAGE_FACTOR = 2.0f;
constexpr float SPAWN_PATROL_RADIUS = 8.0f;
constexpr float SPAWN_LEASH_RANGE = 25.0f;
constexpr float SPAWN_HOME_REACH = 1.0f;
constexpr float BOSS_RETURN_SPEED_FACTOR = 10.0f;
constexpr float BOSS_RETURN_TELEPORT_SECONDS = 10.0f;

inline const MobInfo& mobInfo(MobKind kind) {
    static const MobInfo info[MOB_KIND_COUNT] = {
        { "MOB_BEAR",    89,  4, 22, 8,  22, 8,  14, 8 },
        { "MOB_COW",     93,  4, 16, 5,  18, 5,  3,  4 },
        { "MOB_CHICKEN", 95,  1, 3,  10, 4,  14, 1,  1 },
        { "MOB_BOAR",    97,  3, 16, 8,  16, 10, 10, 6 },
        { "MOB_WOLF",    99,  2, 13, 14, 12, 16, 12, 4 },
        { "MOB_DEER",    101, 3, 10, 10, 11, 18, 3,  2 },
        { "MOB_RABBIT",  103, 1, 3,  12, 4, 20, 1,  1 },
        { "MOB_FISH",    105, 1, 3,  12, 4,  14, 1,  1 },
        { "MOB_CRAB",    107, 1, 6,  6,  6,  6,  4,  12 },
        { "MOB_LIZARD",  109, 1, 5,  14, 5,  16, 4,  2 },
        { "MOB_SNAKE",   111, 1, 6,  18, 6,  18, 10, 1 },
        { "MOB_PIG",     113, 3, 12, 5,  14, 6,  4,  4 },
        { "MOB_SHEEP",   115, 3, 8,  6,  12, 8,  2,  3 },
        { "MOB_DUCK",    117, 1, 3,  10, 4,  14, 1,  1 },
        { "MOB_TURTLE",  119, 2, 6,  4,  14, 3,  3,  16 },
        { "MOB_BAT",     121, 1, 3,  16, 3, 20, 4,  1 },
        { "MOB_FROG",    123, 1, 2,  12, 3,  16, 1,  1 },
        { "MOB_GORLICORN", 152, 100, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL,
          1, 100, true, GORLICORN_HP, "gorlicorn", 9.6f, true, MobCategory::Boss },
        { "MOB_GRIFFIN", 156, 100, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL,
          1, 100, true, GRIFFIN_HP, "griffin", 9.6f, true, MobCategory::Boss, 155 },
        { "MOB_MANTICORE", 159, 100, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL,
          1, 100, true, MANTICORE_HP, "manticore", 9.6f, true, MobCategory::Boss, 158 },
    };
    const int i = idx(kind) - 1;
    return info[(i >= 0 && i < MOB_KIND_COUNT) ? i : 0];
}

constexpr int MOB_BONES_FIRST = 134;
inline int mobBones(MobKind kind) { return mobInfo(kind).bones > 0 ? mobInfo(kind).bones : MOB_BONES_FIRST + idx(kind) - 1; }
inline int mobBoneCount(MobKind kind) {
    if (mobInfo(kind).boneCount > 0) return mobInfo(kind).boneCount;
    return std::max(1, (mobInfo(kind).meatCount + 1) / 2);
}
inline bool mobCanHold(MobKind kind, int item) { return item == mobInfo(kind).meat || item == mobBones(kind); }

enum class MobTemperament { Avoidant, Passive, Neutral, Aggressive };
inline MobTemperament mobTemperament(MobKind kind) {
    switch (kind) {
    case MobKind::Bear: case MobKind::Wolf: case MobKind::Snake: case MobKind::Bat: case MobKind::Gorlicorn:
    case MobKind::Griffin: case MobKind::Manticore: return MobTemperament::Aggressive;
    case MobKind::Boar: case MobKind::Crab: case MobKind::Turtle: return MobTemperament::Neutral;
    case MobKind::Cow: case MobKind::Fish: case MobKind::Pig: case MobKind::Sheep: return MobTemperament::Passive;
    default: return MobTemperament::Avoidant;
    }
}
inline const char* temperamentKey(MobTemperament t) {
    switch (t) {
    case MobTemperament::Avoidant: return "UILABEL_TEMPERAMENT_AVOIDANT";
    case MobTemperament::Passive: return "UILABEL_TEMPERAMENT_PASSIVE";
    case MobTemperament::Neutral: return "UILABEL_TEMPERAMENT_NEUTRAL";
    case MobTemperament::Aggressive: return "UILABEL_TEMPERAMENT_AGGRESSIVE";
    }
    return "";
}
constexpr float WILD_AVOID_RANGE = 6.0f;
constexpr float WILD_FLEE_DISTANCE = 12.0f;
constexpr int EXILE_FLEE_PERCENT = 25;

inline float mobSpriteScale(MobKind kind) {
    switch (kind) {
    case MobKind::Bear: return 2.0f;
    case MobKind::Crab: case MobKind::Snake: case MobKind::Duck: case MobKind::Rabbit: case MobKind::Frog: case MobKind::Turtle: case MobKind::Bat:
    case MobKind::Chicken:
        return 0.5f;
    default: return 1.0f;
    }
}

inline float mobHoverFraction(MobKind kind) {
    return kind == MobKind::Bat ? 0.8f : 0.0f;
}

inline bool mobKindsFriendly(MobKind a, MobKind b) {
    return (a == MobKind::Gorlicorn && b == MobKind::Rabbit) || (a == MobKind::Rabbit && b == MobKind::Gorlicorn);
}
constexpr int GORLICORN_RABBITS = 3;

inline bool isRegularMob(MobKind kind) {
    return idx(kind) >= 1 && idx(kind) <= MOB_KIND_COUNT && mobInfo(kind).category == MobCategory::Regular;
}
inline bool campMobKind(MobKind kind) { return isRegularMob(kind); }

inline int mobFaction(MobKind kind) { return MOB_FACTION_BASE + idx(kind) - 1; }
inline bool isMobFaction(int faction) { return faction >= MOB_FACTION_BASE && faction < MOB_FACTION_BASE + MOB_KIND_COUNT; }
inline MobKind mobKindOfFaction(int faction) { return (MobKind)(faction - MOB_FACTION_BASE + 1); }
inline bool mobHasStat(StatId stat) {
    const StatGroup g = statInfo(stat).group;
    return g == StatGroup::Attributes || g == StatGroup::Combat;
}
