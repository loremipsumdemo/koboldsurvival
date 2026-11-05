#pragma once
#include <algorithm>
#include "enum_index.h"
#include "stats.h"

// Mobs: animals and other creatures. A mob is a unit (it walks, fights, falls Unconscious and can be carried, looted
// and rested in a bed or bedroll), with a Mob component naming its kind, and these differences:
//   - It has no gear slots and can't wear gear (its Equipment stays empty). It can't use healing items.
//   - It can't talk or trade, either way: it has no Dialogue, is never offered Talk, Shop or Trade, and never talks or
//     trades for your squad.
//   - Its inventory is not shown, even when it is one of yours, until it is a corpse.
//   - It has only the attributes and the combat skills (StatGroup::Attributes and Combat); the gathering, crafting and
//     utility skills stay at 1 and earn no XP, and it works no props.
//   - It carries its own raw meat (MobInfo::meat) and bones (mobBones), and can hold nothing else (mobCanHold): it picks
//     nothing up, takes nothing from anyone and is given nothing. While it is awake its inventory can't be opened,
//     traded from or dropped from. Once it is Unconscious it can be looted, and taking anything out of it kills it:
//     it goes to 0 health and its Doom completes at once, so it is a corpse.
//   - Its picture is data/mob/mob_<kind>.png (a missing one is the pink square, data/mobinfo.txt lists them). A
//     kind with a model (MobInfo::model) is drawn in the world by data/model/mob/mob_<nn>.glb instead, and its
//     portrait is the pink square, as a person's is.
//   - It belongs to its kind's own faction (MOB_FACTION_BASE + kind - 1, data/factioninfo.txt), unless it was placed in
//     another one: each camp of a faction with units has CAMP_MOBS of random kinds in that faction, each locked in a
//     Prisoner Cage of its own, with " [!]" after its name to show it is a faction's, not wild. Yours are the
//     party's own (PartyMember::mob, party.h), as its scenario rolled them or the party menu chose them. Beyond the camps, MobInfo::wild of every kind (WILD_MOBS for most) roam in herds, in their own
//     factions. A kind placed only once (the Gorlicorn) is never one of a camp's.
// The Gorlicorn: one in the world, Aggressive, every stat at its most and GORLICORN_HP health. It does not Roam but
// Patrols Spawn: it walks about near where it was placed, chases outsiders, and past SPAWN_LEASH_RANGE from that spot
// gives up, heals fully and walks home, taking no damage until it gets there (patrolSpawnSystem in systems.h). Being a
// boss, it goes home at BOSS_RETURN_SPEED_FACTOR its speed, and is put there if it takes too long.
// Every mob has Rested (it follows Vitality, as a person's does), which never falls by itself and rises by
// MOB_RESTED_GAIN_PER_HOUR each in-game hour.
// Wild mobs: those in their kind's own faction. A mob faction has no reputation. Instead its kind's temperament
// (mobTemperament) says how its mobs take to any unit outside their faction:
//   Avoidant: keeps away. It runs from any outsider within WILD_AVOID_RANGE, and never fights, even when hit.
//   Passive: lets them be, and never fights, even when hit.
//   Neutral: lets them be, but fights back when attacked, and its herd joins in.
//   Aggressive: attacks them on sight.
// Wild mobs also Roam: they wander the open map, anywhere walkable outside the flags' claims (behavior.h).
// Knocking out or killing a mob of a mob faction costs no reputation: hunting is not a slight to anyone.
// Kinds are numbered from 1 in the order below, one for each raw meat in data/itemdb.txt.

enum class MobKind : int {
    None = 0,
    Bear, Cow, Chicken, Boar, Wolf, Deer, Rabbit, Fish, Crab, Lizard,
    Snake, Pig, Sheep, Duck, Turtle, Bat, Frog, Gorlicorn,
    End
};
constexpr int MOB_KIND_COUNT = idx(MobKind::End) - 1;   // kinds 1 to MOB_KIND_COUNT
constexpr int MOB_FACTION_BASE = 10;               // kind k's own faction is MOB_FACTION_BASE + k - 1
constexpr int CAMP_MOBS = 3;                       // mobs of random kinds each camp's faction starts with
constexpr int WILD_MOBS = 10;                      // mobs of each kind placed out in the world, in their own faction

// A kind's category: Regular for most, Boss for one of a kind (the Gorlicorn). Only Regular kinds stand in camps or
// can join a party from the scenarios and the party menu.
enum class MobCategory { Regular, Boss };

// What a kind of mob is: its name, the raw meat it carries and how much, its attributes and its Melee and Block, all
// as levels. Only these are set; the other attributes start at the unit's usual 10, the rest at 1.
// The rest have defaults: how many roam the world (wild), whether every stat it has is at MAX_LEVEL (maxStats), its
// health if not the usual from Vitality (hp, 0 for the usual), its model (data/model/mob/mob_<nn>.glb, 0 for none: it
// is drawn by its picture) and how tall that model stands in tiles, and whether it Patrols Spawn instead of Roaming.
struct MobInfo {
    const char* nameKey;
    int meat;          // its raw meat's item id
    int meatCount;
    int strength, dexterity, vitality, endurance, agility;
    int melee, block;
    int wild = WILD_MOBS;
    int boneCount = 0;   // bones it carries; 0 for the usual, about half its meat (mobBoneCount)
    bool maxStats = false;
    int hp = 0;
    int model = 0;
    float modelHeight = 1.2f;
    bool patrolSpawn = false;
    MobCategory category = MobCategory::Regular;
};

constexpr int GORLICORN_HP = 2000;
constexpr float SPAWN_PATROL_RADIUS = 8.0f;   // Patrol Spawn: its walks end within this many tiles of its spawn
constexpr float SPAWN_LEASH_RANGE = 25.0f;    // past this far from its spawn it gives up a chase and goes home
constexpr float SPAWN_HOME_REACH = 1.0f;      // within this of its spawn it is home again
// A boss walking home (taking no damage) moves this many times its usual speed, and if it is not home within this many
// seconds it is put there at once
constexpr float BOSS_RETURN_SPEED_FACTOR = 10.0f;
constexpr float BOSS_RETURN_TELEPORT_SECONDS = 10.0f;
// Rested a mob gains each in-game hour, wherever it is, while below its most (more in a bed or bedroll)
constexpr double MOB_RESTED_GAIN_PER_HOUR = 10.0;

inline const MobInfo& mobInfo(MobKind kind) {
    static const MobInfo info[MOB_KIND_COUNT] = {
        { "MOB_BEAR",    89,  4, 22, 8,  22, 18, 8,  14, 8 },
        { "MOB_COW",     93,  4, 16, 5,  18, 14, 5,  3,  4 },
        { "MOB_CHICKEN", 95,  1, 3,  10, 4,  6,  14, 1,  1 },
        { "MOB_BOAR",    97,  3, 16, 8,  16, 14, 10, 10, 6 },
        { "MOB_WOLF",    99,  2, 13, 14, 12, 16, 16, 12, 4 },
        { "MOB_DEER",    101, 3, 10, 10, 11, 16, 18, 3,  2 },
        { "MOB_RABBIT",  103, 1, 3,  12, 4,  10, 20, 1,  1 },
        { "MOB_FISH",    105, 1, 3,  12, 4,  8,  14, 1,  1 },
        { "MOB_CRAB",    107, 1, 6,  6,  6,  8,  6,  4,  12 },
        { "MOB_LIZARD",  109, 1, 5,  14, 5,  8,  16, 4,  2 },
        { "MOB_SNAKE",   111, 1, 6,  18, 6,  8,  18, 10, 1 },
        { "MOB_PIG",     113, 3, 12, 5,  14, 12, 6,  4,  4 },
        { "MOB_SHEEP",   115, 3, 8,  6,  12, 12, 8,  2,  3 },
        { "MOB_DUCK",    117, 1, 3,  10, 4,  8,  14, 1,  1 },
        { "MOB_TURTLE",  119, 2, 6,  4,  14, 10, 3,  3,  16 },
        { "MOB_BAT",     121, 1, 3,  16, 3,  10, 20, 4,  1 },
        { "MOB_FROG",    123, 1, 2,  12, 3,  8,  16, 1,  1 },
        { "MOB_GORLICORN", 152, 100, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL, MAX_LEVEL,
          1, 100, true, GORLICORN_HP, 1, 9.6f, true, MobCategory::Boss },
    };
    const int i = idx(kind) - 1;
    return info[(i >= 0 && i < MOB_KIND_COUNT) ? i : 0];
}

// A kind's bones: items MOB_BONES_FIRST on, one for each kind in the same order, and how many it carries, about half
// its meat
constexpr int MOB_BONES_FIRST = 134;
inline int mobBones(MobKind kind) { return MOB_BONES_FIRST + idx(kind) - 1; }
inline int mobBoneCount(MobKind kind) {
    if (mobInfo(kind).boneCount > 0) return mobInfo(kind).boneCount;
    return std::max(1, (mobInfo(kind).meatCount + 1) / 2);
}
// Whether a mob of this kind may hold an item: its own meat or bones only
inline bool mobCanHold(MobKind kind, int item) { return item == mobInfo(kind).meat || item == mobBones(kind); }

enum class MobTemperament { Avoidant, Passive, Neutral, Aggressive };
inline MobTemperament mobTemperament(MobKind kind) {
    switch (kind) {
    case MobKind::Bear: case MobKind::Wolf: case MobKind::Snake: case MobKind::Bat: case MobKind::Gorlicorn: return MobTemperament::Aggressive;
    case MobKind::Boar: case MobKind::Crab: case MobKind::Turtle: return MobTemperament::Neutral;
    case MobKind::Cow: case MobKind::Fish: case MobKind::Pig: case MobKind::Sheep: return MobTemperament::Passive;
    default: return MobTemperament::Avoidant;   // chicken, deer, rabbit, lizard, duck, frog
    }
}
// The language key for a temperament's name
inline const char* temperamentKey(MobTemperament t) {
    switch (t) {
    case MobTemperament::Avoidant: return "UILABEL_TEMPERAMENT_AVOIDANT";
    case MobTemperament::Passive: return "UILABEL_TEMPERAMENT_PASSIVE";
    case MobTemperament::Neutral: return "UILABEL_TEMPERAMENT_NEUTRAL";
    case MobTemperament::Aggressive: return "UILABEL_TEMPERAMENT_AGGRESSIVE";
    }
    return "";
}
constexpr float WILD_AVOID_RANGE = 6.0f;    // an Avoidant wild mob runs from an outsider this close
constexpr float WILD_FLEE_DISTANCE = 12.0f; // and runs this far from it
constexpr int EXILE_FLEE_PERCENT = 25;      // an Exile at this share of its health or less stops fighting and runs

// How tall a kind drawn by its picture stands, against a person's height: the Bear twice as tall, the small creatures
// half as tall, the rest as tall as a person
inline float mobSpriteScale(MobKind kind) {
    switch (kind) {
    case MobKind::Bear: return 2.0f;
    case MobKind::Crab: case MobKind::Snake: case MobKind::Duck: case MobKind::Rabbit: case MobKind::Frog: case MobKind::Turtle: case MobKind::Bat:
    case MobKind::Chicken:
        return 0.5f;
    default: return 1.0f;
    }
}

// How big a kind is against a person (1): its model's height against a person's 1.2 tiles, or its picture's scale.
// A smaller unit can't push a bigger one, and a bigger one reaches further in melee (meleeReachFor).
inline float mobSize(MobKind kind) {
    const MobInfo& info = mobInfo(kind);
    return info.model > 0 ? info.modelHeight / 1.2f : mobSpriteScale(kind);
}

// How far above the ground a kind is drawn while it is up and about, as a share of its height: the Bat flies. Lying
// (Unconscious, playing dead, a corpse), carried, or occupying a prop, it is on the ground like any other.
inline float mobHoverFraction(MobKind kind) {
    return kind == MobKind::Bat ? 0.8f : 0.0f;
}

// Kinds that leave each other be, whatever their temperaments: the Gorlicorn and the Rabbits that keep it company. They
// do not fight each other, and an Avoidant one does not run from the other.
inline bool mobKindsFriendly(MobKind a, MobKind b) {
    return (a == MobKind::Gorlicorn && b == MobKind::Rabbit) || (a == MobKind::Rabbit && b == MobKind::Gorlicorn);
}
constexpr int GORLICORN_RABBITS = 3;   // Rabbits placed by the Gorlicorn, keeping near it

// Whether a kind is a regular mob, not a boss
inline bool isRegularMob(MobKind kind) {
    return idx(kind) >= 1 && idx(kind) <= MOB_KIND_COUNT && mobInfo(kind).category == MobCategory::Regular;
}
// Whether a camp may be given a mob of this kind: a regular one
inline bool campMobKind(MobKind kind) { return isRegularMob(kind); }

// The faction a kind of mob belongs to when it is not placed in another one
inline int mobFaction(MobKind kind) { return MOB_FACTION_BASE + idx(kind) - 1; }
// Whether a faction is a mob kind's own
inline bool isMobFaction(int faction) { return faction >= MOB_FACTION_BASE && faction < MOB_FACTION_BASE + MOB_KIND_COUNT; }
// The kind whose own faction this is (a mob faction only)
inline MobKind mobKindOfFaction(int faction) { return (MobKind)(faction - MOB_FACTION_BASE + 1); }
// Whether a mob has this stat: the attributes and the combat skills
inline bool mobHasStat(StatId stat) {
    const StatGroup g = statInfo(stat).group;
    return g == StatGroup::Attributes || g == StatGroup::Combat;
}
