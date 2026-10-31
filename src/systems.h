#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "resources.h"
#include "terrain.h"

// Spawns the units, the red units and the props
void setupWorld(World& w, Terrain& t, Rng& rng);

// Makes there be exactly count player units: adds them at the spawn, or removes the last made, as needed
void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count);

// Starting stats drawn the way new units get them: attributes at levels 5..15, skills at 1..5
Stats randomStartingStats(Rng& rng);

// A unit name from the list that is not in taken
// The body type a new unit gets: masculine 45% of the time, feminine 45%, androgynous 10%
int randomBodyType(Rng& rng);
// A name for a unit of this body type, from the language in use (English if it has none), not in taken if it can be
std::string randomUnitName(Rng& rng, int body, const std::vector<std::string>& taken);
// The body type of a unit saved before body types existed: the same 45/45/10 split, worked out from its name
int bodyForSavedName(const std::string& name);

// Puts `perItem` copies of every item in the database on the ground at random walkable tiles across
// the whole level. Each copy is a single item.
void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem);

// Creates a squad from the given units and sends it to a ground point
void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint);
// Walks units straight at a point, with no route planned, for a right button that is still held
void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target);

// Sends each unit that carries items to a ground item. Whichever gets in reach first takes it.
void issuePickupOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity item);

// Orders each unit to fight one red unit: each walks at it on its own, with no formation, and keeps it as its
// target until it is gone. Units with no place in the fight (red ones, or not your own) are skipped.
void issueAttackOrder(World& w, const std::vector<Entity>& units, Entity target);
// Right-click talk: each selected unit walks up to the NPC; the first in talk range starts the talk
// With trade set, it is a free trade with another unit of yours instead: the first in range starts the trade.
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade = false);
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

// Moves the whole stack in one inventory slot onto the ground, under its unit. False if the slot is empty.
bool dropStack(World& w, Entity unit, int slot);

// Systems, run once per frame in this order. combatSystem (combat.h) runs before all of them, so a unit
// that dies this frame is gone before the others move. running is the squad's walk or run mode: when set,
// player units move at run pace, and each one that moves is marked for a double hunger tick.
void squadSystem(World& w, float dt, bool running = false);
void steeringSystem(World& w, Terrain& t, float dt, bool running = false);
// After steering, so units that just arrived can take items. Returns how many player units took something this frame.
int pickupSystem(World& w);
void cleanupSquads(World& w);
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
    WorkStart craft = WorkStart::Started;
};
// After steering: units in reach of their prop use it. Returns those arrivals, so the game can open a storage panel
// or say why a unit could not start. Units on beds are kept on them.
std::vector<PropArrival> propOrderSystem(World& w);
// What a unit's work gave this frame, for the label over it: an item ("+item"), a lock opened, or a lockpick that failed
struct WorkGain {
    enum class Kind { Item, Unlocked, LockpickFailed, Trained };
    Entity unit = NULL_ENTITY;
    int item = 0;               // for Trained, the stat trained instead
    Kind kind = Kind::Item;
    float xp = 0.0f;            // for Trained, the XP given
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
// Units working a resource or crafting prop, or on their way to it
int propWorkerCount(World& w, Entity prop);
// Whether a prop of this kind takes this item into its inventory: a crafting or passive crafting prop only its input
// item, a storage prop the items its ItemFilter allows (props.h)
bool propAcceptsItem(int kind, int item);
// What a crafting or passive crafting prop of this kind makes from an input item: its item for its own input, or the
// input's cooks_into at a campfire (INPUT_COOKABLE). 0 if it takes no such input.
int recipeOutput(int kind, int input);
// Whether a unit is in an occupiable prop that is locked (the Prisoner Cage). Such a unit takes no orders except to
// unlock that prop: every issue...Order function and steerUnits skip it.
bool lockedIn(World& w, Entity u);
// The prop a unit occupies, or NULL_ENTITY
Entity occupiedProp(World& w, Entity u);

// Passive crafting props (the forge): each burns its fuel and makes items into its output, with no unit needed, for
// the in-game seconds that passed. One input item is taken as fuel whenever the last has burned out. It pauses,
// burning nothing, while its output has no room for the next item, and goes on once it has.
void passiveCraftSystem(World& w, double gameSeconds);
// What a passive crafting prop is doing, for its panel
enum class PassiveState { Burning, NoFuel, OutputFull };
PassiveState passiveStateOf(World& w, Entity prop);

// The emotes to show over a unit's head (emotes.h), in the order they take turns: what it is working at or lying on,
// then Encumbered when it carries ENCUMBERED_SHARE of its carry limit or more. Empty for none.
std::vector<int> emotesOf(World& w, Entity u);
// What a unit is doing, for the Unit Panel: the skill it is working with (such as Mining or Cooking), Resting,
// Sitting, Locked in, Moving or Idle
std::string taskNameOf(World& w, Entity u);
