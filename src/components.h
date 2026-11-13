#pragma once
#include <cstdint>
#include "factions.h"
#include "formation.h"
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

// Squared distance across the ground between two points
inline float distSq(float x1, float z1, float x2, float z2) {
    const float dx = x1 - x2, dz = z1 - z2;
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
// A boss's own coloring: its texture's colors turned round the color wheel by this many degrees, 0 to 359
struct Coloration {
    int hue = 0;
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
    // Once the squad has arrived: settled means it has stopped trying for its slot, and is standing around like anyone else
    bool settled = false;
    float bestGap = 1e9f;     // the closest it has been to its slot
    float stuck = 0.0f;       // seconds since it last got closer
    float retryIn = 0.0f;     // gave up: seconds until it tries again
    int retries = 0;
    float swapIn = 0.0f;
};

struct ActiveSquad {
    float cx = 0.0f, cz = 0.0f;
    float headX = 0.0f, headZ = 1.0f;
    float endX = 0.0f, endZ = 1.0f;   // the way the shape faces once it has arrived, kept when it re-forms
    Vec2 end{ 0.0f, 0.0f };           // the middle of the shape once it has arrived
    std::vector<Vec2> waypoints;
    size_t next = 0;
    bool arrived = false;
    float speed = 2.8f;
    int size = 0;
    float spacing = FORMATION_TIGHT_SPACING;
    std::vector<Vec2> offsets;      // each slot on the way, turning with the squad
    std::vector<Vec2> finalSpots;   // each slot once it has arrived
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
    bool held = false;   // waiting for its way to be planned (stuck.h): it stands, and counts as walking
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
    std::uint64_t carried = 0;  // what the unit carried when the automation last looked, to see when that changes
    float social = 0.0f;        // Socialize and Sit Down: real seconds left of the chat or the sit, once it has begun (not saved)
    bool began = false;         // that has begun
    int slot = -1;              // Playing: which side of the table; Dancing: its place on the circle (not saved)
};

struct Combat {
    float swingLeft = 0.0f;
    ObjectId target = NO_OBJECT;
    float buffAttack = 0.0f;
    float buffLeft = 0.0f;
    float slowedLeft = 0.0f;   // seconds of slower movement left after taking damage
    int downedByFaction = -1;
};

// A melee fighter's place around its target, while it is near enough to want one
struct AttackSlot {
    ObjectId target = NO_OBJECT;
    int slot = -1;         // 0 to ATTACK_SLOTS - 1, or -1: every slot is taken, so it waits its turn
    bool closing = true;   // walking to its place, rather than standing in it
    float lookIn = 0.0f;   // waiting: seconds until it looks for a foe with a free slot
};

struct XpBuffer {
    std::array<float, STAT_COUNT> frac{};
};

// A general store's allowed item kinds (StoreCategory bits). One without it allows STORE_DEFAULT.
struct StoreFilter {
    std::uint32_t allowed = STORE_DEFAULT;
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
// A unit carrying a rescued one to near the middle of its claim, with no bed free: how long it may take. Not saved.
struct RescueWalk {
    float left = 60.0f;
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

// How a prop is drawn apart from where it stands (the Build tab's Adjust tool): moved up to half a tile each way,
// turned about its middle (degrees round x, y and z), and stretched from half to one and a half times its size along
// each. Only how it looks and where it is clicked: it stands, blocks and is walked to on its own tile as before.
inline constexpr float PROP_VISUAL_MOVE = 0.5f;
inline constexpr float PROP_VISUAL_SCALE_MIN = 0.5f, PROP_VISUAL_SCALE_MAX = 1.5f;
struct PropVisual {
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
    bool plain() const {
        return dx == 0.0f && dy == 0.0f && dz == 0.0f && rx == 0.0f && ry == 0.0f && rz == 0.0f && sx == 1.0f && sy == 1.0f && sz == 1.0f;
    }
};
inline float wrapDegrees(float d) {
    d = d - 360.0f * (float)(int)(d / 360.0f);
    return d < 0.0f ? d + 360.0f : d;
}
inline PropVisual clampPropVisual(PropVisual v) {
    auto move = [](float a) { return a < -PROP_VISUAL_MOVE ? -PROP_VISUAL_MOVE : a > PROP_VISUAL_MOVE ? PROP_VISUAL_MOVE : a; };
    auto size = [](float a) { return a < PROP_VISUAL_SCALE_MIN ? PROP_VISUAL_SCALE_MIN : a > PROP_VISUAL_SCALE_MAX ? PROP_VISUAL_SCALE_MAX : a; };
    v.dx = move(v.dx); v.dy = move(v.dy); v.dz = move(v.dz);
    v.rx = wrapDegrees(v.rx); v.ry = wrapDegrees(v.ry); v.rz = wrapDegrees(v.rz);
    v.sx = size(v.sx); v.sy = size(v.sy); v.sz = size(v.sz);
    return v;
}

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

// A prop's store. Most have STORAGE_SLOTS slots; a kind can have more (props.txt's slots column, a Stall's 1000).
// capacity: the most items it holds in all, or 0 for as many as its slots fit.
struct Storage {
    std::vector<ItemStack> slots = std::vector<ItemStack>(STORAGE_SLOTS);
    int capacity = 0;
};

struct Output {
    std::array<ItemStack, STORAGE_SLOTS> slots{};
};

// A prop or ground item above the ground floor of a building: on the upper floor of this level (1 is the first upper
// floor). Not saved: worked out again from the height when a game is opened.
struct Storey {
    int level = 1;
};

// A unit on an upper floor of a building: the foundation tile it stands over, and which floor (1 the first)
struct Upstairs {
    ObjectId foundation = NO_OBJECT;
    int level = 1;
};

// Which floor something is on: 0 the ground (or a ground floor's deck), 1 the first upper floor and so on
inline int storeyOf(World& w, ObjectId e) {
    if (const Upstairs* u = w.get<Upstairs>(e)) return u->level;
    if (const Storey* s = w.get<Storey>(e)) return s->level;
    return 0;
}

// A bed or bedroll inside a building set to Public: anyone may use it. Without it, a bed on a faction's foundation is
// only for that faction.
struct BedPublic {};

// A Stall's orders: items picked from your stores to be hauled to it (each tried once), and what it takes as payment
struct StallRequest {
    ObjectId from = NO_OBJECT;
    int item = 0;
    int count = 0;
    ObjectId takenBy = NO_OBJECT;   // the unit hauling it, or NO_OBJECT while it waits
};
struct StallStock {
    std::vector<StallRequest> requests;
    std::uint32_t payment = STORE_ALL;   // StoreCategory bits it accepts as payment
};

// A unit hauling a Stall order: fetching it from its store, then bringing it to the Stall. Not saved.
struct StallJob {
    ObjectId stall = NO_OBJECT;
    ObjectId source = NO_OBJECT;
    int item = 0;
    int count = 0;
    bool fetched = false;
    float stuck = 0.0f;
};

// A unit of a friendly faction on its way to shop at a Stall. Not saved.
struct Shopper {
    ObjectId stall = NO_OBJECT;
    float timeLeft = 0.0f;
};

// A unit of a hostile faction raiding your stores: going for one, then running off with what it took. Not saved.
struct Raider {
    ObjectId target = NO_OBJECT;
    bool fleeing = false;
    float timeLeft = 0.0f;
    float fleeX = 0.0f, fleeZ = 0.0f;
};

// A unit of another faction that was beaten (knocked out and come round): it keeps out of fights and raids and goes to
// its faction's Altar or camp, or away from your claims to make a camp there, and rests until it is back to full health
struct Retreating {
    float x = 0.0f, z = 0.0f;   // where it is going
    bool planned = false;       // x, z picked
    bool home = false;          // x, z is its faction's Altar or camp (else a spot to make a camp)
    bool arrived = false;
    bool wasPassive = false;    // its Passive before, given back when it is done
    double hours = 0.0;         // game hours since it began; it gives up after RETREAT_MOST_HOURS
    float replan = 0.0f;
};

// A unit going for food (on the ground, or in its own faction's store): another faction's, or yours with Eat on. Not saved.
struct Foraging {
    ObjectId source = NO_OBJECT;
    float timeLeft = 0.0f;
};

// A bedroll or campfire out in the open (off buildings and outside every Altar's claim): game hours it has stood there
struct OutdoorDecay {
    double hours = 0.0;
};

// The Wandering Trader: buys from a Stall with coins it makes on the spot, then leaves in a puff of smoke. Not saved.
struct WanderingTrader {
    ObjectId stall = NO_OBJECT;
    bool leaving = false;
    float timeLeft = 0.0f;
    float exitX = 0.0f, exitZ = 0.0f;
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
