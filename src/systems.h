#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// Spawns the units, the red units and the props
// playerCamp: whether your camp is placed (only Full Random Camp has one, scenarioHasCamp in party.h). The level is
// rebuilt from its seed when a save loads, so a save keeps this too.
void setupWorld(World& w, Terrain& t, Rng& rng, bool playerCamp = true);

// Makes there be exactly count player units: adds them at the spawn, or removes the last made, as needed
void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count);
// Makes your mobs exactly these kinds, in order: any you have go, and each kind is placed at the spawn, in the squad's
// block from slot firstSlot on (after your people)
void ensurePlayerMobs(World& w, Terrain& t, Rng& rng, const std::vector<MobKind>& kinds, int firstSlot);
// Puts a mob of this kind (mobs.h) at (x, z), in the faction given, carrying its raw meat. One of the Player Faction's
// is yours to command. Returns it.
Entity spawnMob(World& w, const Terrain& t, Rng& rng, MobKind kind, int faction, float x, float z);
// A mob's stats for its kind: its attributes and combat skills, each a level or two either way of the kind's
Stats mobStats(MobKind kind, Rng& rng);
// Ends an Unconscious unit's Doom at once: it is a corpse, as when its Doom reaches 100 (doomSystem)
void finishDoom(World& w, Entity u);
// Whether a unit may take this item into its inventory: any unit may, but a mob only its own meat and bones (mobs.h)
bool canHoldItem(World& w, Entity unit, int item);
// Whether a unit's inventory is shut to everyone: a mob's, while it is awake (mobs.h)
bool inventoryLocked(World& w, Entity unit);

// Starting stats drawn the way new units get them: attributes at levels 5..15, skills at 1..5
Stats randomStartingStats(Rng& rng);

// A unit name from the list that is not in taken
// The body type a new unit gets: masculine 45% of the time, feminine 45%, androgynous 10%
BodyType randomBodyType(Rng& rng);
// A name for a unit of this body type, from the language in use (English if it has none), not in taken if it can be
std::string randomUnitName(Rng& rng, BodyType body, const std::vector<std::string>& taken);
// The body type of a unit saved before body types existed: the same 45/45/10 split, worked out from its name
BodyType bodyForSavedName(const std::string& name);

// Puts `perItem` copies of every item in the database on the ground at random walkable tiles across
// the whole level. Each copy is a single item.
void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem);

// Creates an active squad (a group move order) from the given units and sends it to a ground point
void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint);
// Walks units straight at a point, with no route planned, for a right button that is still held
void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target);

// Sends each unit that carries items to a ground item. Whichever gets in reach first takes it.
void issuePickupOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity item);
// A box drawn around ground items only: the units share one pickup chain (PickupChain) over the items. Each unit goes
// for the nearest item no other unit of the chain is walking to, then the next, until the items are gone. Any other
// order, or being attacked, ends a unit's chain.
void issuePickupChain(World& w, Terrain& t, const std::vector<Entity>& units, const std::vector<Entity>& items);
// Hands the next item of its chain to each unit on one that has no item to walk to (run by pickupSystem)
void pickupChainSystem(World& w, Terrain& t);

// Orders each unit to fight one red unit: each walks at it on its own, with no formation, and keeps it as its
// target until it is gone. Units with no place in the fight (red ones, or not your own) are skipped.
void issueAttackOrder(World& w, const std::vector<Entity>& units, Entity target);
// Right-click talk: each selected unit walks up to the NPC; the first in talk range starts the talk
// With trade set, it is a free trade with another unit of yours instead: the first in range starts the trade.
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade = false, bool shop = false);
// Standing orders from the right-click menu. Follow: each unit walks after the target and keeps close to it.
// Defend: the same, and each unit fights whatever is attacking the unit it defends (defendSystem). The target itself
// is skipped if it is among the units. Any other order ends them.
void issueFollowOrder(World& w, const std::vector<Entity>& units, Entity target);
void issueDefendOrder(World& w, const std::vector<Entity>& units, Entity ward);
// Before the fight each frame: each defending unit picks the nearest unit attacking its ward, and fights it until it
// stops or falls; with none, it follows its ward
void defendSystem(World& w);
// Attack (Unprovoked) on a neutral unit: it becomes hostile, with a line in the log
void makeHostile(World& w, Entity unit);
// Drops every talk order, once a talk has started or the NPC is gone
void clearTalkOrders(World& w);

// Systems, run once per frame in this order. combatSystem (combat.h) runs before all of them, so a unit
// that dies this frame is gone before the others move. running is the squad's walk or run mode: when set,
// player units move at run pace, and each one that moves is marked for a double hunger tick.
void squadSystem(World& w, float dt, bool running = false);
void steeringSystem(World& w, Terrain& t, float dt, bool running = false);
// After steering, so units that just arrived can take items. Returns how many player units took something this frame.
int pickupSystem(World& w, Terrain& t);
void cleanupActiveSquads(World& w);
// Health and hunger, after each whole in-game hour that passed (hours) and the in-game seconds that
// passed (gameSeconds, which can be fractional). Out of combat, units regain health as in regenHealth.
void needsSystem(World& w, int hours, double gameSeconds = 0.0);

// ---- Props (props.h) ----
// From the right-click menu on a prop: each unit walks to it, and in reach does the action picked. Inventory opens a
// storage or crafting prop's panel. Use takes a free bed, works a resource prop if it has room for another worker,
// or starts crafting if the prop's input and output allow it (tryStartWork).
void issuePropOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity prop, PropAction action);
// Puts a unit to work on a resource or crafting prop, if it can start now: it is in reach, the prop has room for
// another worker, and a crafting prop has its input item. Checked at the moment of the try.
// Locked: the prop is locked. NotLocked: an Unlock on a prop with no lock, or one already open. SkillTooLow: the unit's
// Lockpicking is below the lock's level.
enum class WorkStart { Started, TooFar, Full, NoInput, Locked, NotLocked, SkillTooLow };
WorkStart tryStartWork(World& w, Entity unit, Entity prop);
// A unit that reached the prop it was sent to, with what it was sent to do. Inventory: the game opens the panel.
// Use: started is set if it took the bed or started working; taken if the bed was occupied or the prop had all the
// workers it can have; unreachable if another prop stands between the unit and the bed; craft is the result of trying
// to start at a crafting prop.
struct PropArrival {
    Entity unit = NULL_ENTITY;
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
    bool taken = false;
    bool started = false;
    bool unreachable = false;
    bool locked = false;       // the prop is locked, so it could not be opened or used
    bool placed = false;       // Place: the unit it carried was placed on the occupiable prop
    bool corpse = false;       // Place: it carried a corpse, which is never placed
    WorkStart craft = WorkStart::Started;
    bool automatic = false;    // the unit's automation sent it: the game says nothing about it
};
// After steering: units in reach of their prop use it. Returns those arrivals, so the game can open a storage panel
// or say why a unit could not start. Units on beds are kept on them.
std::vector<PropArrival> propOrderSystem(World& w);
// Puts what a unit carries into a storage prop: every stack of item (0: of anything the prop takes), as much as fits.
// Returns how many items went in.
int storeCarried(World& w, Entity unit, Entity prop, int item);

// The automated behaviors (behavior.h): a unit with Auto-Harvest or Auto-Haul on takes one step at a time, the
// first from the top of its list that can be done now. A harvest goes on until the unit carries AUTO_LOAD of the item
// and an Auto-Haul can take it, or until it has no room; a haul takes what the prop takes and comes back. Only props in
// a claim of the unit's faction are worked or hauled to. Run after propOrderSystem, each frame, with the real seconds
// that passed. A unit on another order or in a fight is left to it; the automation picks up again once the unit has
// stood idle AUTO_RESUME_SECONDS (at once for a unit that is not yours).
constexpr int AUTO_LOAD = 10;
constexpr float AUTO_RESUME_SECONDS = 8.0f;
void automationSystem(World& w, Terrain& t, float dt, Rng& rng);
// Wild mobs (mobs.h) by their temperament: an Avoidant or Passive one never fights (its passive mode is on), and an
// Avoidant one runs WILD_FLEE_DISTANCE from any outsider that comes within WILD_AVOID_RANGE, as a Roam step. Run before
// automationSystem, each frame, with the real seconds that passed.
void wildMobSystem(World& w, Terrain& t, float dt);
// What a unit's work gave this frame, for the label over it: an item ("+item"), a lock opened, or a lockpick that failed
struct WorkGain {
    enum class Kind { Item, Unlocked, LockpickFailed, Trained };
    Entity unit = NULL_ENTITY;
    int item = 0;               // for Trained, the stat trained instead
    Kind kind = Kind::Item;
    float xp = 0.0f;            // for Trained, the XP given
    int propKind = -1;          // the kind of prop it came from (props.h), for its sound
};
// Resource and crafting props: the units working each one add to its progress by their skill, for the in-game
// seconds that passed. Each full bar gives one item to one of the workers, in turn. At a resource prop, one with no
// room drops it at its feet and stops. At a crafting prop, the item uses up one input item, checked at that moment;
// once the input runs out the workers stop, and if no worker has room the bar waits, full, for one that has.
// A unit picking a lock (Working::unlocking) makes an attempt each time the bar fills, at its Lockpicking pace, and
// opens the lock with lockpickChance (props.h); it keeps trying until the lock opens or it is given another order.
// Returns what the workers received this frame: items, opened locks and failed attempts.
std::vector<WorkGain> workSystem(World& w, double gameSeconds, Rng& rng);
// Starts a unit picking a locked prop's lock, if it is in reach, has the Lockpicking level the lock needs, and nobody
// else is at it. Checked at the moment of the try.
WorkStart tryStartUnlock(World& w, Entity unit, Entity prop);
// Whether a crafting prop can be worked: it has its input item, or needs none
enum class CraftState { Ready, NoInput };
CraftState craftStateOf(World& w, Entity prop);
// Call whenever a crafting prop's items change (taken, put in or dropped): if it can no longer be worked, everyone
// working it stops and its progress is lost, so an item can never be made from input that was taken away
void propStorageChanged(World& w, Entity prop);
// Whether another prop stands on the straight line from a unit to a prop (within PROP_BLOCK_RADIUS of it). Units and
// items on the ground don't count.
bool propInTheWay(World& w, Entity unit, Entity prop);
// The unit on an occupiable prop, or NULL_ENTITY
Entity propOccupant(World& w, Entity prop);
// Whether a prop of this kind takes this item into its inventory: a crafting or passive crafting prop only its input
// item, a storage prop the items its ItemFilter allows (props.h)
bool propAcceptsItem(PropKind kind, int item);
// What a storage prop holds when a level places it: a few stacks of what belongs in it (setupWorld)
Storage startingStorage(PropKind kind, Rng& rng);
// What a crafting or passive crafting prop of this kind makes from an input item: its item for its own input, or the
// input's cooks_into at a campfire (INPUT_COOKABLE). 0 if it takes no such input.
int recipeOutput(PropKind kind, int input);
// Whether a unit is in an occupiable prop that is locked (the Prisoner Cage). Such a unit takes no orders except to
// unlock that prop: every issue...Order function and steerUnits skip it.
bool lockedIn(World& w, Entity u);
// The prop a unit occupies, or NULL_ENTITY
Entity occupiedProp(World& w, Entity u);
// Whether a unit can take no orders: locked in a cage, Unconscious (or a Corpse), or carried (unless it is playing dead)
bool cannotAct(World& w, Entity u);
// For an order about to be given: a unit playing dead breaks free first (breakFree), then as cannotAct
bool cannotTakeOrder(World& w, Entity u);
// A carried unit gets free: it stops playing dead and stands where its carrier is
void breakFree(World& w, Entity u);
// Whether a unit can be picked up: it is Unconscious (or a corpse), or one of your squad; and nobody carries it, nor
// is it carrying anyone
bool canBeCarried(World& w, Entity target);

// ---- Unconscious, Doom and Corpses (components.h) ----
// How far a unit carrying, or picking up, another must be from it, in tiles
constexpr float CARRY_REACH = 1.2f;
// Doom gained each in-game hour while Unconscious and not in a bed: 20, plus 30 less 1% of it for each Vitality level
float doomPerHour(const Stats& s);
// Doom lost each in-game hour lying Unconscious in a bed (20) or a bedroll (10); 0 for anything else
float doomRecoveryPerHour(PropKind propKind);
// Health a unit wakes with when its Doom falls to 0 in a bed or bedroll
constexpr int RECOVERY_HEALTH = 10;
// Knocks a unit out: it gets Unconscious with Doom 0, every order and task it had ends, it drops whatever unit it
// carried, and it drops out of the active squad (group move) it was in. A unit already on a bed or bedroll stays on
// it. Does nothing to one already out.
void knockOut(World& w, Entity u);
// After anything that hurts: each unit at 0 health that is not yet Unconscious is knocked out
void downedSystem(World& w);
// Doom for the in-game seconds that passed: up while out on the ground or carried, down in a bed or bedroll. Doom 0 in
// a bed or bedroll wakes the unit with RECOVERY_HEALTH; Doom 100 makes it a Corpse, no longer yours to command.
// Returns the units that woke and the units that died this call.
struct DoomChanges {
    std::vector<Entity> woke, died;
};
DoomChanges doomSystem(World& w, double gameSeconds);
// Whether a hostile unit that is up and about stands within RESTED_HOSTILE_RANGE of u
bool hostileNear(World& w, Entity u);
// Units playing dead on the ground (woken from a rested knock-out near hostile units) get up once no hostile unit is
// near. An order gets them up sooner (cannotTakeOrder).
void playDeadSystem(World& w);
// Patrol Spawn (mobs.h): a mob too far from its SpawnHome gives up its chase, is made whole, and walks home taking no
// damage and starting no fight; once home it patrols again. Runs after the combat step, so the chase it drops is not
// walked this frame. A boss goes home faster (walkSpeedOf), and after BOSS_RETURN_TELEPORT_SECONDS of dt on the way it
// is put home at once.
void patrolSpawnSystem(World& w, Terrain& t, float dt);
// Sends the first unit that can (the list is nearest first) to pick up a unit that canBeCarried. False if none can.
bool issueCarryOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target);
// Sends each unit that is carrying someone to a ground point, to put it down there
void issuePutDownOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 point);
// Puts down the unit a unit carries, where the carrier stands
void putDown(World& w, Entity carrier);
// Sends the first unit that can (nearest first) to an Unconscious unit or a corpse to loot it. False if none can.
bool issueLootOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target);
// How near a looter must stay to what it loots for the panel to stay open, in tiles
constexpr float LOOT_RANGE = 2.0f;
// Takes from an Unconscious unit or a corpse into the looter's inventory: a gear slot (gear set) or an inventory slot,
// as much as fits. False, with why, if nothing could be taken.
bool lootTake(World& w, Entity looter, Entity target, bool gear, int slot, std::string& why);
// Takes everything that fits, gear first. Returns how many stacks or gear items moved.
int lootAll(World& w, Entity looter, Entity target);
// After steering: units that reached the unit they were sent for pick it up, carriers that reached their spot put
// down what they carry, and every carried unit is moved to its carrier. Units sent to loot that got there are
// returned, with what they loot, for the game to open its panel.
struct CarryEvents {
    std::vector<Entity> picked;
    std::vector<std::pair<Entity, Entity>> loot;   // looter, then the unit looted
};
CarryEvents carrySystem(World& w);

// Passive crafting props (the forge): each burns its fuel and makes items into its output, with no unit needed, for
// the in-game seconds that passed. One input item is taken as fuel whenever the last has burned out. It pauses,
// burning nothing, while its output has no room for the next item, and goes on once it has.
void passiveCraftSystem(World& w, double gameSeconds);
// What a passive crafting prop is doing, for its panel
enum class PassiveState { Burning, NoFuel, OutputFull };
PassiveState passiveStateOf(World& w, Entity prop);

// The emotes to show over a unit's head (emotes.h), in the order they take turns: what it is working at or lying on,
// then Encumbered when it carries ENCUMBERED_SHARE of its carry limit or more. Empty for none.
std::vector<EmoteId> emotesOf(World& w, Entity u);
// What a unit is doing, for the Unit Panel: the skill it is working with (such as Mining or Cooking), Resting,
// Sitting, Locked in, Moving or Idle
std::string taskNameOf(World& w, Entity u);
// Whether a unit or mob is idle: awake, on no order, step, prop, fight or walk of any kind. The renderer stops turning
// one that has stood idle a while (Renderer::unitYaw), so units nudged by others close by don't keep turning.
bool unitIdle(World& w, Entity u);

// Disband: a unit of yours leaves your squad and faction for good. Whatever it was doing ends, and it drops anyone it
// carried. A person becomes an Exile (FACTION_EXILE): neutral to everyone, fighting back as any neutral unit does but
// running from attacks once its health is low (EXILE_FLEE_PERCENT), its
// hunger kept full, with only Eat, Heal and Roam. A mob goes back to the wild, in its kind's own faction, roaming (or
// Patrolling Spawn where it stands) with its kind's temperament. False if it is not one of yours.
bool disbandUnit(World& w, Entity u);
