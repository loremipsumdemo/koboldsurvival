#pragma once
#include "factions.h"
#include <array>
#include <string>
#include <vector>
#include "behavior.h"
#include "ecs.h"
#include "enum_index.h"
#include "grid.h"
#include "mobs.h"
#include "geometry.h"
#include "props.h"
#include "stats.h"

struct Transform {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Squared distance across the ground
inline float distSq(const Transform& a, const Transform& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

// An entity is a living thing: it has health and it moves. There are two kinds:
//  - a unit, a person: a player unit (Commandable) or an NPC
//  - a mob, an animal or beast (it also has Mob), of which a boss mob is one whose MobCategory is Boss
// Props and ground items are objects too, but they are not entities.
struct Entity {
    float speed = 3.0f;      // tiles per second
    float radius = 0.5f;
};

struct Selected {};

// What a level knows about this object: its id in the level's file
struct LevelId {
    int id = 0;
};
// In the World's resources: where a level starts your units, and the id its next new object gets
struct LevelInfo {
    bool spawnSet = false;
    float spawnX = 0.0f, spawnZ = 0.0f;
    int nextId = 1;
};

// Stands still and talks, unless it is attacked
struct InConversation {};

struct Commandable {};

struct Hostile {};

struct Neutral {};

struct DialogueOption {
    std::string prompt;
    std::string reply;
    bool opensTrade = false;
};

struct Dialogue {
    std::string text;
    std::vector<DialogueOption> options;
};

struct SpokenLine {
    std::string speaker;
    std::string text;
    bool player = false;
    int sprite = 1;
};

struct Name {
    std::string text;
};

struct UnitSprite {
    int id = 1;
};

struct Mob {
    MobKind kind = MobKind::Bear;
};

inline bool isUnit(World& w, ObjectId e) { return w.has<Entity>(e) && !w.has<Mob>(e); }

struct SpawnHome {
    float x = 0.0f, z = 0.0f;
    bool returning = false;
    float returnSeconds = 0.0f;
    float replanIn = 0.0f;
};

enum class BodyType : int { Masculine = 0, Feminine = 1, Androgynous = 2 };
struct Body {
    BodyType type = BodyType::Masculine;
};

inline const char* bodyTypeKey(BodyType type) {
    return type == BodyType::Feminine ? "UILABEL_BODY_FEMININE"
         : type == BodyType::Androgynous ? "UILABEL_BODY_ANDROGYNOUS" : "UILABEL_BODY_MASCULINE";
}

struct Faction {
    int id = FACTION_PLAYER;
};

struct Provoked {
    struct Entry {
        int faction = 0;
        float secondsLeft = 0.0f;
    };
    std::vector<Entry> against;
};
constexpr float PROVOKED_SECONDS = 10.0f;
constexpr float PROVOKED_SIGHT_RANGE = 9.0f;

struct IdleSpot {
    float x = 0.0f, z = 0.0f;
    float leadGap = -1.0f;
};

struct ActiveSquadMember {
    ObjectId squad = NO_OBJECT;
    int slot = 0;
};

struct ActiveSquad {
    float cx = 0.0f, cz = 0.0f;
    float headX = 0.0f, headZ = 1.0f;
    std::vector<Vec2> waypoints;
    size_t next = 0;
    bool arrived = false;
    float speed = 2.8f;
    int size = 0;
};

struct ChaseRoute {
    std::vector<Vec2> waypoints;
    size_t next = 0;
    Vec2 goal{ 0.0f, 0.0f };
    float checkIn = 0.0f;
    float planIn = 0.0f;
    bool blocked = false;
};

struct SoloMove {
    std::vector<Vec2> waypoints;
    size_t next = 0;
};

struct ItemStack {
    int item = 0;
    int count = 0;
};

struct TradeGhost {
    int slot = -1;
    ItemStack stack;
};

constexpr int INVENTORY_SLOTS = 32;
constexpr int INVENTORY_COLUMNS = 8;
struct Inventory {
    std::array<ItemStack, INVENTORY_SLOTS> slots{};
};

enum class GearSlot : int {
    Head = 0,
    Torso,
    MainHand,
    OffHand,
    Legs,
    Feet,
    Accessory1,
    Accessory2,
    Count
};
constexpr int GEAR_SLOTS = idx(GearSlot::Count);

struct GearItems : std::array<int, GEAR_SLOTS> {
    using std::array<int, GEAR_SLOTS>::operator[];
    int& operator[](GearSlot s) { return (*this)[idx(s)]; }
    const int& operator[](GearSlot s) const { return (*this)[idx(s)]; }
};

struct Equipment {
    GearItems items{};
};

constexpr unsigned NEEDS_HP_CHANGED = 1u;
constexpr unsigned NEEDS_HUNGER_CHANGED = 2u;
struct Needs {
    int hp = 100, maxHp = 100;
    int hunger = 100, maxHunger = 100;
    int rested = 50, maxRested = 50;
    int comfort = 50, maxComfort = 50;
    double restedPart = 0.0, comfortPart = 0.0;
    bool hurried = false;
    double healSeconds = 0.0;
    unsigned changed = NEEDS_HP_CHANGED | NEEDS_HUNGER_CHANGED;
};

struct Behaviors {
    std::vector<BehaviorRule> rules = defaultBehaviors();
    bool pending = true;
    float recovered = 0.0f;
    bool busy = false;
    BehaviorRule step;
    ObjectId stepProp = NO_OBJECT;
    float wait = 0.0f;          // real seconds before the automation takes its next step
    ObjectId seek = NO_OBJECT;
    bool seeking = false;
    float sense = 0.0f;
    bool passive = false;
    bool block = false;
    bool sneak = false;
};

struct Combat {
    float swingLeft = 0.0f;
    ObjectId target = NO_OBJECT;
    float buffAttack = 0.0f;
    float buffLeft = 0.0f;
    int downedByFaction = -1;
};

struct XpBuffer {
    std::array<float, STAT_COUNT> frac{};
};

struct GroundItem {
    int item = 0;
    int count = 1;
    double hours = 0.0;
};

struct PickupOrder {
    ObjectId item = NO_OBJECT;
};

struct PickupJob {
    std::vector<ObjectId> items;
};
struct PickupChain {
    ObjectId job = NO_OBJECT;
    std::vector<ObjectId> skipped;
    int replans = 0;
};

struct TalkOrder {
    ObjectId npc = NO_OBJECT;
    bool trade = false;
    bool shop = false;
};

struct FollowOrder {
    ObjectId target = NO_OBJECT;
};

struct DefendOrder {
    ObjectId ward = NO_OBJECT;
    ObjectId engaged = NO_OBJECT;
};

struct Unconscious {
    float dying = 0.0f;
    int koFaction = -1;
    float conscious = 0.0f;
    double tickPart = 0.0;
};
struct Carried {
    ObjectId by = NO_OBJECT;
};
struct PlayingDead {};
struct BonesPile {
    double hours = 0.0;
    std::vector<ItemStack> items;
};
struct Corpse {
    double hours = 0.0;
};
inline bool outCold(World& w, ObjectId e) { return w.has<Unconscious>(e) || w.has<Corpse>(e); }
struct Carrying {
    ObjectId unit = NO_OBJECT;
};
struct CarryOrder {
    ObjectId target = NO_OBJECT;
};
struct LootOrder {
    ObjectId target = NO_OBJECT;
};
struct AttackOrder {
    ObjectId target = NO_OBJECT;
};

struct Prop {
    PropKind kind = PropKind::Wagon;
    float yaw = 0.0f;
};

struct Lock {
    bool locked = true;
    int level = 1;
    int faction = -1;
};

struct Owner {
    int faction = 0;
};

struct Health {
    double value = 10.0;
    double max = 10.0;
};

struct Storage {
    std::array<ItemStack, STORAGE_SLOTS> slots{};
};

struct Output {
    std::array<ItemStack, STORAGE_SLOTS> slots{};
};

struct Fuel {
    double hours = 0.0;
    int item = 0;
};

struct PropOrder {
    ObjectId prop = NO_OBJECT;
    PropAction action = PropAction::Use;
    bool automatic = false;
};

struct Occupying {
    ObjectId prop = NO_OBJECT;
};
struct OccupyBan {
    float secondsLeft = 0.0f;
};

struct Working {
    ObjectId prop = NO_OBJECT;
    bool unlocking = false;
};

struct PropWork {
    double progress = 0.0;
    unsigned turn = 0;
    int workers = 0;
    unsigned made = 0;
};
