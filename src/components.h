#pragma once
#include "factions.h"
#include <array>
#include "language.h"
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

// World position. y is the terrain height under the entity.
struct Transform {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// A player-controlled unit
struct Unit {
    float speed = 3.0f;      // tiles per second
    float radius = 0.5f;     // half a tile: every unit and mob takes up one tile square (steerUnits)
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

// Which unit model this unit uses: data/model/unit/unit_01.glb up to UNIT_SPRITE_COUNT (party.h)
struct UnitSprite {
    int id = 1;
};

// A mob (mobs.h): an animal or creature, of this kind (MobKind). Its picture is data/mob/mob_<kind>.png, not its
// UnitSprite.
struct Mob {
    MobKind kind = MobKind::Bear;
};

// Patrol Spawn (behavior.h): where a mob was placed, and whether it is walking back there after giving up a chase.
// Walking back it takes no damage and starts no fight. A boss walks back at BOSS_RETURN_SPEED_FACTOR its speed, and is
// put straight home if it is not there within BOSS_RETURN_TELEPORT_SECONDS (mobs.h).
struct SpawnHome {
    float x = 0.0f, z = 0.0f;
    bool returning = false;
    float returnSeconds = 0.0f;   // how long it has been walking back (not saved: a loaded game starts it again)
    float replanIn = 0.0f;        // seconds until it may look for a route home again
};

// The body type a unit is given, which decides which set of names its name comes from. A new unit is masculine 45% of
// the time, feminine 45% and androgynous 10% (randomBodyType in systems.h).
enum class BodyType : int { Masculine = 0, Feminine = 1, Androgynous = 2 };
struct Body {
    BodyType type = BodyType::Masculine;
};

// The language key for a body type's name
inline const char* bodyTypeKey(BodyType type) {
    return type == BodyType::Feminine ? "UILABEL_BODY_FEMININE"
         : type == BodyType::Androgynous ? "UILABEL_BODY_ANDROGYNOUS" : "UILABEL_BODY_MASCULINE";
}

// The faction a unit belongs to (factions.h): the Player Faction for your units, one from data/factioninfo.txt for the
// rest. Whether two units fight comes from their factions' reputation, and from Provoked.
struct Faction {
    int id = FACTION_PLAYER;
};

// A unit that saw its faction attacked (or was attacked itself) by a unit of a faction it was not hostile to. It treats
// each faction listed as hostile, as if their reputation were REP_HOSTILE, whatever it really is: it and the others of
// its faction that saw it fight back against the attacker and the attacker's whole faction (for the Player Faction,
// your whole squad). It calms down once it has seen none of that faction's units (within PROVOKED_SIGHT_RANGE) for
// PROVOKED_SECONDS in a row, so running out of its sight ends it, unless the factions are hostile anyway. Not saved.
struct Provoked {
    struct Entry {
        int faction = 0;
        float secondsLeft = 0.0f;
    };
    std::vector<Entry> against;
};
constexpr float PROVOKED_SECONDS = 10.0f;
constexpr float PROVOKED_SIGHT_RANGE = 9.0f;   // tiles: a provoking faction's unit this close keeps it provoked

// Where a unit came to rest the last time it stopped by its own movement. While it has nowhere to go, it walks back
// here, so a unit pushed aside by others passing returns to its place once they have gone. Set when the unit stops,
// and dropped while it is walking somewhere. Not saved.
// For a unit following or defending another, leadGap is how far it was from that unit when it stopped (else below 0).
struct IdleSpot {
    float x = 0.0f, z = 0.0f;
    float leadGap = -1.0f;
};

// Links a unit to its active squad and to its formation slot
struct ActiveSquadMember {
    Entity squad = NULL_ENTITY;
    int slot = 0;
};

// An active squad is one group movement order, not your squad of units: it lasts only for that move. Its center walks
// a path, and its members seek formation slots around the center.
struct ActiveSquad {
    float cx = 0.0f, cz = 0.0f;       // formation center
    float headX = 0.0f, headZ = 1.0f; // facing, from the direction of travel
    std::vector<Vec2> waypoints;      // smooth path, world units
    size_t next = 0;
    bool arrived = false;
    float speed = 2.8f;               // slower than units, so members can keep up
    int size = 0;                     // number of units, which sets the formation's spacing
};

// The way a unit is taking to something that moves (a unit it chases or follows, its place in a formation) when the
// straight line there is blocked. Made and kept up by steeringSystem.
struct ChaseRoute {
    std::vector<Vec2> waypoints;
    size_t next = 0;
    Vec2 goal{ 0.0f, 0.0f };   // where it was going when the route was found
    float checkIn = 0.0f;      // seconds until it looks at the straight line again
    float planIn = 0.0f;       // seconds until it may search for a route again
    bool blocked = false;      // the straight line was blocked when last looked at
};

// A single unit's own route. Used instead of an active squad when one unit is ordered.
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

// An item id for each gear slot, 0 for empty. It takes a GearSlot or a slot number as its index.
struct GearItems : std::array<int, GEAR_SLOTS> {
    using std::array<int, GEAR_SLOTS>::operator[];
    int& operator[](GearSlot s) { return (*this)[idx(s)]; }
    const int& operator[](GearSlot s) const { return (*this)[idx(s)]; }
};

struct Equipment {
    GearItems items{};
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
    // Rested: falls while awake and rises lying in a bed or bedroll (needs.h). Well rested, a unit knocked out gets no
    // Dying and wakes by itself. Comfort: rises sitting or lying in an occupiable prop that has no lock, falls otherwise.
    // A mob has neither, and no hunger either: it can't feed itself.
    int rested = 100, maxRested = 100;
    int comfort = 100, maxComfort = 100;
    double restedPart = 0.0, comfortPart = 0.0;   // the part of a point each has moved toward its next whole point
    bool hurried = false;        // moved at run pace this in-game hour, so the next hunger tick costs double
    double healSeconds = 0.0;    // in-game seconds saved toward the next point of health
    // Which values changed since the behavior pass last checked them. Every place that changes one sets its bit,
    // and the pass clears the bits it has read.
    unsigned changed = NEEDS_HP_CHANGED | NEEDS_HUNGER_CHANGED;
};

// A unit's behaviors (behavior.h), highest priority first. pending is set when the list is edited, so the next
// behavior pass checks Eat and Heal at once, not only when hunger or health changes.
// Its automation: busy while it is on a step an automated behavior gave it (the walk to a prop, its work there), and
// step names that behavior, so the step can be told apart from an order of yours. An order of yours, or a fight,
// pauses the automation; it picks up again once the unit has stood idle AUTO_RESUME_SECONDS (systems.h).
// Its fighting modes from the Behavior tab:
//   passive: it never starts a fight on its own: it does not attack on sight or join in to defend others, though it
//            still does what it is ordered to, an attack included
//   block:   it never swings, and blocks hits more often (BLOCK_MODE_FACTOR), even with nothing in its hands
//   sneak:   it walks at SNEAK_SPEED and is harder to notice, more so the higher its Stealth
// Whether it fights at range is not a choice: it does whenever a ranged weapon is in its main hand (isRangedWeapon).
struct Behaviors {
    std::vector<BehaviorRule> rules = defaultBehaviors();
    bool pending = true;
    float recovered = 0.0f;     // Leave Bed: real seconds it has lain in a bed fully recovered (not saved)
    bool busy = false;
    BehaviorRule step;          // the automated behavior whose step the unit is on, while busy
    Entity stepProp = NULL_ENTITY;   // the prop that step goes to
    float wait = 0.0f;          // real seconds before the automation takes its next step
    // Share: the unit it should walk to for an item it needs (set by Eat or Heal), and whether it is on that walk
    Entity seek = NULL_ENTITY;
    bool seeking = false;
    float sense = 0.0f;         // a wild Avoidant mob: real seconds before it looks about for outsiders again
    bool passive = false;
    bool block = false;
    bool sneak = false;
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
    std::vector<Entity> spotted;      // hostiles this unit has spotted this in-game hour, for Perception XP (forgetSpotted)
    int downedByFaction = -1;         // the faction of the unit whose hit just took it to 0 health, until it is knocked out
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

// A pickup chain from a box drawn around ground items only: the job entity lists the items, and every unit sent on it
// has a PickupChain. A unit with no PickupOrder of its own takes the nearest listed item no other unit of the job is
// walking to. An item that is gone is just skipped, so one unit's pickup never ends the chain for the others. Any
// order of yours, or the unit being attacked, ends the unit's chain; being pushed aside does not.
struct PickupJob {
    std::vector<Entity> items;
};
struct PickupChain {
    Entity job = NULL_ENTITY;
    std::vector<Entity> skipped;   // items this unit could not hold or reach; others of the job may still take them
    int replans = 0;               // new routes planned to the current item after stopping short of it
};

// A selected unit's order to talk to an NPC. It walks up to the NPC and the first unit in talk range starts the talk.
struct TalkOrder {
    Entity npc = NULL_ENTITY;
    bool trade = false;   // a free trade with another unit of yours, in place of a talk
    bool shop = false;    // Shop: the talk opens straight to the trade, as if Show me your wares were picked
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

// A unit at 0 health is Unconscious: it lies still, takes no orders, does nothing, and nothing fights it. A unit that is
// awake is at 100 Conscious; knocked out it falls to 0, and wakes back at 100 with a little health. Each tick
// (CONSCIOUS_TICKS_PER_HOUR) gains Conscious, up to double at full Comfort, and costs 1 Rested. With no Rested left, each
// tick costs 1 Dying instead, unless it lies in a bed or bedroll, which stops Dying. Dying 100 makes it a Corpse for
// good; waking ends Dying, so the next knock-out starts at 0.
struct Unconscious {
    float dying = 0.0f;
    // The faction whose unit knocked it out, or -1 (hunger, or nobody). A unit of another faction knocked out by your
    // units has cost you reputation with its faction, and costs more if it dies before it wakes.
    int koFaction = -1;
    float conscious = 0.0f;
    double tickPart = 0.0;   // the part of a tick saved toward the next one
};
// A unit being carried, by the unit `by` (which has Carrying). It lies over its carrier, moves with it and pushes
// nothing. Unconscious units and corpses can be carried by anyone; a unit that is awake only by its own squad, and it
// stays put on its carrier until put down. Awake and carried by a hostile, it plays dead (PlayingDead); awake and carried
// by anyone else, it gets free at once.
struct Carried {
    Entity by = NULL_ENTITY;
};
// A unit that is awake but carried by a hostile lies still, playing dead, until it is given an order: then it breaks
// free (it is put down where its carrier is) and does it. Never at the same time as Unconscious, and never with its
// own squad carrying it. One of yours (canPlayDead) that wakes from a knock-out with hostile units near plays dead on the ground the
// same way, until it is given an order or they have gone (playDeadSystem). Nobody attacks a unit playing dead.
struct PlayingDead {};
// An Unconscious unit whose Dying reached 100: it never recovers. It keeps its Unconscious, with Dying 100, so it can
// still be carried and looted, and it is no longer anyone's to command.
struct Corpse {};
// A unit carrying another (one at a time). It can put it down on the ground or into a bed or bedroll.
struct Carrying {
    Entity unit = NULL_ENTITY;
};
// An order to walk to a unit and pick it up: one that is Unconscious, or one of your own squad
struct CarryOrder {
    Entity target = NULL_ENTITY;
};
// An order to walk to an Unconscious unit or a corpse and open its gear and inventory to take from
struct LootOrder {
    Entity target = NULL_ENTITY;
};
// A player unit's order to fight one red unit. The unit walks at it and keeps it as its target, choosing no
// other, until the red unit is gone. A move or pickup order cancels it.
struct AttackOrder {
    Entity target = NULL_ENTITY;
};

// A prop: a 3D model on the map, such as a wagon or a campfire. It does not block movement.
// kind is a PropKind; props.h gives each kind its model, name, size and use. serial is its place in the order the
// level placed the props, which is the same each time the level is built from its seed, so a save can name it. A built
// prop has serial -1: it is not placed by a seed, so a save writes it out whole.
struct Prop {
    PropKind kind = PropKind::Wagon;
    int serial = 0;
    float yaw = 0.0f;   // the facing, in radians, turning its model and footprint about the vertical axis. Built props
                        // keep the facing they were placed with; a level's props face 0.
};

// A lockable prop's lock (props.h: PropInfo::lockLevel). While locked, the prop can't be opened or used. level is the
// Lockpicking level a unit needs to try to unlock it.
// faction is the faction that owns the lock (-1 for none). Its own units open it as if it were unlocked; every other unit
// has to lockpick it while it is locked. Built props inside a claim are locked to the claim's faction (claims.h).
struct Lock {
    bool locked = true;
    int level = 1;
    int faction = -1;
};

// Ownership tag of a built prop, or of a flag's claim: the faction whose it is (factions.h). Built props have one, and so
// does any prop other than a resource prop standing in a sealed claim when a new game starts (settleClaimOwnership,
// claims.h). Other props a level places belong to no faction.
struct Owner {
    int faction = 0;
};

// A flag's health: it starts at FLAG_HEALTH and is never hit, but it falls while its claim has no vault and recovers
// one point an hour while the vault stands in its claim (claims.h). At 0 the flag breaks.
struct Health {
    double value = 10.0;
    double max = 10.0;
};

// The serials of the level's props that have been taken away, so that a save can leave them out when the level is built
// again from its seed. One entity holds it, made the first time a level's prop goes (claims.cpp).
struct GoneProps {
    std::vector<int> serials;
};

// How much of a built foundation or flag is left, 1 when new, falling while it is not held up (claims.h). At 0 it breaks
// into debris. A flag decays on its own when its claim's vault is dry or missing; a foundation when it lies outside any
// claim of its faction, or in one whose vault is dry or missing.
struct Wear {
    double value = 1.0;
};

// The foundation a built prop stands on. The prop breaks when that foundation is gone (claims.h).
struct OnBase {
    Entity foundation = NULL_ENTITY;
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
    int item = 0;   // the input item burning: at a campfire, the raw meat being cooked (what it makes depends on it)
};

// A unit's order to use a prop: it walks to the prop, and once in reach it opens the storage, occupies the prop,
// places the unit it carries on it, or starts working, by the action (a crafting prop opens its panel too). Any other
// order replaces it.
struct PropOrder {
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
    bool automatic = false;   // given by the unit's automation, not by you: its arrival says nothing to the player
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
