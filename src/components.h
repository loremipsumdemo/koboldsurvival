#pragma once
#include <array>
#include "language.h"
#include <string>
#include <vector>
#include "behavior_rules.h"
#include "ecs.h"
#include "grid.h"
#include "geometry.h"
#include "props.h"
#include "stats.h"

// World position. y is the terrain height under the entity.
struct Transform {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// A player-controlled unit
struct Unit {
    float speed = 3.0f;      // tiles per second
    float radius = 0.4f;     // used for separation from other units
};

struct Selected {};

// Player units. Only these can be ordered to move.
struct Commandable {};

// Red units. They can be selected but not commanded. They stand until a player unit comes within
// aggro range, then chase it.
struct Hostile {};

// Neutral units: like red units, but not hostile. They can be talked to, and nothing attacks them.
struct Neutral {};

// One thing the player can say in a talk, and the reply the unit gives to it
struct DialogueOption {
    std::string prompt;
    std::string reply;
    bool opensTrade = false;   // the option that brings up the trade
};

// What a unit says when it is talked to, and what the player can say back. Every red and neutral unit starts with
// the same greeting and options.
struct Dialogue {
    std::string text = lang("NPC_GREETING");
    std::vector<DialogueOption> options = {
        { lang("NPC_OPTION_WARES"), lang("NPC_GREETING"), true },
        { lang("NPC_OPTION_QUEST"), lang("NPC_GREETING") },
    };
};

// One line of a talk: who said it, what, and the sprite its portrait is drawn from
struct SpokenLine {
    std::string speaker;
    std::string text;
    bool player = false;
    int sprite = 1;
};

// Display name for a unit
struct Name {
    std::string text = lang("UILABEL_UNNAMED_UNIT");
};

// Which unit image this unit uses: data/unit/unit_1.png up to UNIT_SPRITE_COUNT (party.h)
struct UnitSprite {
    int id = 1;
};

// The body type a unit is given, which decides which set of names its name comes from. A new unit is masculine 45% of
// the time, feminine 45% and androgynous 10% (randomBodyType in systems.h).
enum BodyType : int { BODY_MASCULINE = 0, BODY_FEMININE = 1, BODY_ANDROGYNOUS = 2 };
struct Body {
    int type = BODY_MASCULINE;
};

// The language key for a body type's name
inline const char* bodyTypeKey(int type) {
    return type == BODY_FEMININE ? "UILABEL_BODY_FEMININE"
         : type == BODY_ANDROGYNOUS ? "UILABEL_BODY_ANDROGYNOUS" : "UILABEL_BODY_MASCULINE";
}

// The side a unit belongs to. Your units are the Player Faction, the units that start neutral the Gray Faction, and the
// hostile ones the Red Faction. A unit keeps its faction if it turns hostile (Attack (Unprovoked)).
enum FactionId : int { FACTION_PLAYER = 0, FACTION_GRAY = 1, FACTION_RED = 2, FACTION_COUNT };
struct Faction {
    int id = FACTION_PLAYER;
};

// The language key for a faction's name
inline const char* factionKey(int id) {
    return id == FACTION_GRAY ? "FACTION_GRAY" : id == FACTION_RED ? "FACTION_RED" : "FACTION_PLAYER";
}

// Where a unit came to rest the last time it stopped by its own movement. While it has nowhere to go, it walks back
// here, so a unit pushed aside by others passing returns to its place once they have gone. Set when the unit stops,
// and dropped while it is walking somewhere. Not saved.
// For a unit following or defending another, leadGap is how far it was from that unit when it stopped (else below 0).
struct IdleSpot {
    float x = 0.0f, z = 0.0f;
    float leadGap = -1.0f;
};

// Links a unit to its squad and to its formation slot
struct SquadMember {
    Entity squad = NULL_ENTITY;
    int slot = 0;
};

// A squad is one group movement order. Its center walks a path, and its
// members seek formation slots around the center.
struct Squad {
    float cx = 0.0f, cz = 0.0f;       // formation center
    float headX = 0.0f, headZ = 1.0f; // facing, from the direction of travel
    std::vector<Vec2> waypoints;      // smooth path, world units
    size_t next = 0;
    bool arrived = false;
    float speed = 2.8f;               // slower than units, so members can keep up
    int size = 0;                     // number of units, which sets the formation's spacing
};

// A single unit's own route. Used instead of a squad when one unit is ordered.
struct SoloMove {
    std::vector<Vec2> waypoints;      // smooth path, world units
    size_t next = 0;
};

// One inventory slot: an item id and how many of it. Item 0 means the slot is empty.
struct ItemStack {
    int item = 0;
    int count = 0;
};

// An item shown in one side of a trade: where it is in the inventory it came from, and the stack. It is only a
// picture of the trade; the inventory is not changed.
struct TradeGhost {
    int slot = -1;
    ItemStack stack;
};

// Items a unit carries, one stack per slot
constexpr int INVENTORY_SLOTS = 32;
constexpr int INVENTORY_COLUMNS = 8;
struct Inventory {
    std::array<ItemStack, INVENTORY_SLOTS> slots{};
};

// The gear a unit wears, one item per slot. The order is the order of the sidebar's gear panel.
enum GearSlot : int {
    GEAR_HEAD = 0,
    GEAR_TORSO,
    GEAR_MAIN_HAND,
    GEAR_OFF_HAND,
    GEAR_LEGS,
    GEAR_FEET,
    GEAR_ACCESSORY_1,
    GEAR_ACCESSORY_2,
    GEAR_SLOTS
};

struct Equipment {
    std::array<int, GEAR_SLOTS> items{};   // item id in each slot, 0 for empty
};

// Health and hunger, for every unit. Hunger falls by one each in-game hour, on the clock's hour. A tick
// while hunger is already 0 costs one health instead. Health is set from Vitality each frame, and it
// stays between zero and the maximum. A unit at zero health is downed. Out of combat, with hunger over
// half, health comes back one point per tenth of an in-game hour.
// The last three fields are not saved: a loaded unit starts with no run mark, no part of a regen point,
// and both values marked changed, so its behaviors are checked once.
constexpr unsigned NEEDS_HP_CHANGED = 1u;
constexpr unsigned NEEDS_HUNGER_CHANGED = 2u;
struct Needs {
    int hp = 100, maxHp = 100;
    int hunger = 100, maxHunger = 100;
    bool hurried = false;        // moved at run pace this in-game hour, so the next hunger tick costs double
    double healSeconds = 0.0;    // in-game seconds saved toward the next point of health
    // Which values changed since the behavior pass last checked them. Every place that changes one sets its bit,
    // and the pass clears the bits it has read.
    unsigned changed = NEEDS_HP_CHANGED | NEEDS_HUNGER_CHANGED;
};

// A unit's behavior rules, highest priority first. pending is set when a rule is added or edited, so the next
// behavior pass checks every rule, not only those whose values changed.
struct Behaviors {
    std::vector<BehaviorRule> rules = defaultBehaviors();
    bool pending = true;
};

// Fighting state for a unit. Nothing here is saved: a loaded unit starts fresh, with its stamina full.
struct Combat {
    float swingLeft = 0.0f;           // seconds until the unit may swing again
    Entity target = NULL_ENTITY;      // the unit it is fighting: a hostile for a player unit, and the reverse
    float buffAttack = 0.0f;          // attack added by an elixir
    float buffLeft = 0.0f;            // seconds the elixir has left
    float stamina = -1.0f;            // below zero until the first combat frame sets it to the maximum
    float encumbrance = 0.0f;         // 0..1, from the last frame's movement
    float moved = 0.0f;               // tiles walked in the last frame
    std::vector<Entity> spotted;      // hostiles this unit has already spotted, for Perception XP
};

// Fractional XP waiting to become whole points, one per stat. Walking and fighting give fractions of a point.
struct XpBuffer {
    std::array<float, STAT_COUNT> frac{};
};

// An item lying on the ground. Its Transform gives the place.
struct GroundItem {
    int item = 0;
    int count = 1;
};

// A unit's order to pick up a ground item. The unit walks to it and takes what fits.
struct PickupOrder {
    Entity item = NULL_ENTITY;
};

// A selected unit's order to talk to an NPC. It walks up to the NPC and the first unit in talk range starts the talk.
struct TalkOrder {
    Entity npc = NULL_ENTITY;
    bool trade = false;   // a free trade with another unit of yours, in place of a talk
};

// A standing order to follow a unit: the unit walks after it and stays within FOLLOW_DISTANCE of it, until it is
// given another order or the unit it follows is gone
struct FollowOrder {
    Entity target = NULL_ENTITY;
};

// A standing order to defend a unit: follow it, and fight whatever unit is attacking it. engaged is the attacker
// being fought, if any. It lasts until the unit is given another order or the unit it defends is gone.
struct DefendOrder {
    Entity ward = NULL_ENTITY;
    Entity engaged = NULL_ENTITY;
};

// A player unit's order to fight one red unit. The unit walks at it and keeps it as its target, choosing no
// other, until the red unit is gone. A move or pickup order cancels it.
struct AttackOrder {
    Entity target = NULL_ENTITY;
};

// A prop: a 3D model on the map, such as a wagon or a campfire. It does not block movement.
// kind is a PropKind; props.h gives each kind its model, name, size and use. serial is its place in the order the
// level placed the props, which is the same each time the level is built from its seed, so a save can name it.
struct Prop {
    int kind = 0;
    int serial = 0;
};

// A lockable prop's lock (props.h: PropInfo::lockLevel). While locked, the prop can't be opened or used. level is the
// Lockpicking level a unit needs to try to unlock it.
struct Lock {
    bool locked = true;
    int level = 1;
};

// The items a storage prop (a barrel) or a crafting prop (a campfire, which takes only its input item) holds, one stack
// per slot
struct Storage {
    std::array<ItemStack, STORAGE_SLOTS> slots{};
};

// A passive crafting prop's output inventory (the forge's Iron Ingots). Units can take from it, but not put in.
struct Output {
    std::array<ItemStack, STORAGE_SLOTS> slots{};
};

// A passive crafting prop's fuel: the in-game hours left to burn of the input item it last took. It takes the next
// input item only once this has run out, so an item is used up only when it starts to burn.
struct Fuel {
    double hours = 0.0;
};

// A unit's order to use a prop: it walks to the prop, and once in reach it opens the storage, takes the bed or
// starts working, by the prop's use (a crafting prop opens its panel too). Any other order replaces it.
struct PropOrder {
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
};

// A unit on an occupiable prop (a bed, a chair, a cage). It stays on the prop until it is given another order.
// One unit at a time can occupy a prop. In a locked prop (lockedIn in systems.h) it takes no orders at all, other than
// to unlock that prop, until the prop is unlocked.
struct Occupying {
    Entity prop = NULL_ENTITY;
};

// A unit working a resource prop (ore or a tree) or a crafting prop (a campfire). Its task shows as the prop's skill,
// such as Mining or Cooking. It ends when the unit is given another order or is downed; at a resource prop also when
// the unit has no room for what it gathers, and at a crafting prop when the input runs out or the output is full.
struct Working {
    Entity prop = NULL_ENTITY;
    bool unlocking = false;   // picking the prop's lock, rather than working it
};

// A resource or crafting prop's progress toward its next item, 0 to 1. The units working it add to it each in-game
// hour by their skill. Each time it is full, an item is made: at a resource prop one of the workers, in turn, receives
// it; at a crafting prop one input item becomes one output item. A passive crafting prop fills it by itself, while
// its fuel burns (workers is then 1 while it burns, for the ring). Saved only for a passive crafting prop.
struct PropWork {
    double progress = 0.0;
    unsigned turn = 0;      // which worker receives the next item
    int workers = 0;        // units working it in the last frame, for the progress ring
    unsigned made = 0;      // items finished here so far, so the progress ring can hop as each one is done
};
