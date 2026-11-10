#pragma once
#include <algorithm>
#include <string>
#include "enum_index.h"
#include "stats.h"

// A kind of mob. data/mobs.txt says what every kind is, and can add kinds with no name here: they number on from these.
enum class MobKind : int {
    None = 0,
    Bear, Cow, Chicken, Boar, Wolf, Deer, Rabbit, Fish, Crab, Lizard,
    Snake, Pig, Sheep, Duck, Turtle, Bat, Frog, Gorlicorn, Griffin, Manticore,
    End
};
constexpr int MOB_NAMED_KIND_COUNT = idx(MobKind::End) - 1;
// How many kinds data/mobs.txt has. They are numbered from 1.
int mobKindCount();
constexpr int CAMP_MOBS = 3;

enum class MobCategory { Regular, Boss };
enum class MobTemperament { Avoidant, Passive, Neutral, Aggressive };

struct MobInfo {
    std::string nameKey;
    int meat = 0;
    int meatCount = 0;
    int strength = 0, dexterity = 0, vitality = 0, agility = 0;
    int melee = 0, block = 0;
    int wild = 0;
    int bones = 0;
    int boneCount = 0;
    bool maxStats = false;
    int hp = 0;
    float modelHeight = 1.2f;
    std::string picture;
    std::string model;
    bool patrolSpawn = false;
    MobCategory category = MobCategory::Regular;
    MobTemperament temperament = MobTemperament::Avoidant;
    float spriteScale = 1.0f;
    float hover = 0.0f;
    std::string key;
    int faction = 0;
    MobKind escort = MobKind::None;
    int escortCount = 0;
};

constexpr float BOSS_DAMAGE_FACTOR = 2.0f;
constexpr float SPAWN_PATROL_RADIUS = 8.0f;
constexpr float SPAWN_LEASH_RANGE = 25.0f;
constexpr float SPAWN_HOME_REACH = 1.0f;
constexpr float BOSS_RETURN_SPEED_FACTOR = 10.0f;
constexpr float BOSS_RETURN_TELEPORT_SECONDS = 10.0f;

// From data/mobs.txt
const MobInfo& mobInfo(MobKind kind);

inline int mobBones(MobKind kind) { return mobInfo(kind).bones; }
inline int mobBoneCount(MobKind kind) { return mobInfo(kind).boneCount; }
inline bool mobCanHold(MobKind kind, int item) { return item == mobInfo(kind).meat || item == mobBones(kind); }
inline MobTemperament mobTemperament(MobKind kind) { return mobInfo(kind).temperament; }
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
constexpr int FACTIONLESS_FLEE_PERCENT = 25;

inline float mobSpriteScale(MobKind kind) { return mobInfo(kind).spriteScale; }
inline float mobHoverFraction(MobKind kind) { return mobInfo(kind).hover; }

inline bool mobKindsFriendly(MobKind a, MobKind b) { return mobInfo(a).escort == b || mobInfo(b).escort == a; }

inline bool isRegularMob(MobKind kind) {
    return idx(kind) >= 1 && idx(kind) <= mobKindCount() && mobInfo(kind).category == MobCategory::Regular;
}
inline bool isBossMob(MobKind kind) {
    return idx(kind) >= 1 && idx(kind) <= mobKindCount() && mobInfo(kind).category == MobCategory::Boss;
}
inline bool campMobKind(MobKind kind) { return isRegularMob(kind); }

inline int mobFaction(MobKind kind) { return mobInfo(kind).faction; }
// None if no mob kind has this faction
MobKind mobKindOfFaction(int faction);
inline bool isMobFaction(int faction) { return mobKindOfFaction(faction) != MobKind::None; }
inline bool mobHasStat(StatId stat) {
    const StatGroup g = statInfo(stat).group;
    return g == StatGroup::Attributes || g == StatGroup::Combat;
}
