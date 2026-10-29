#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "resources.h"
#include "terrain.h"

// Spawns the units, the red units and the props
void setupWorld(World& w, Terrain& t, Rng& rng);

// Adds player units, at the spawn, until there are count of them
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
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc);
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
// Right-click on a usable prop: each unit walks to it. In reach, a storage prop opens its panel; an occupiable prop
// takes the unit if it is free; a resource prop puts the unit to work if it has room for another worker; a crafting
// prop asks the player whether to open its inventory or to craft (tryStartWork).
void issuePropOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity prop);
// A unit that reached the prop it was sent to. started: it took the bed or started working. taken: the bed was
// occupied, or the prop had all the workers it can have.
struct PropArrival {
    Entity unit = NULL_ENTITY;
    Entity prop = NULL_ENTITY;
    bool taken = false;
    bool started = false;
};
// After steering: units in reach of their prop use it. Returns those arrivals, so the game can open a storage panel
// or say why a unit could not start. Units on beds are kept on them.
std::vector<PropArrival> propOrderSystem(World& w);
// Resource and crafting props: the units working each one add to its progress by their skill, for the in-game
// seconds that passed. At a resource prop each full bar gives one item to one of them, in turn; one with no room drops
// it at its feet and stops. At a crafting prop each full bar turns one input item into one output item; once the input
// runs out or the output is full, its workers stop.
void workSystem(World& w, double gameSeconds);
// Whether a crafting prop can be worked: it has its input item, and room for one more of its output
enum class CraftState { Ready, NoInput, OutputFull };
CraftState craftStateOf(World& w, Entity prop);
// Puts a unit to work on a resource or crafting prop, if it can start now: it is in reach, the prop has room for
// another worker, and a crafting prop has its input item and room for its output. Checked at the moment of the try.
enum class WorkStart { Started, TooFar, Full, NoInput, OutputFull };
WorkStart tryStartWork(World& w, Entity unit, Entity prop);
// Call whenever a crafting prop's items change (taken, put in or dropped): if it can no longer be worked, everyone
// working it stops and its progress is lost, so an item can never be made from input that was taken away
void propStorageChanged(World& w, Entity prop);
// The unit on an occupiable prop, or NULL_ENTITY
Entity propOccupant(World& w, Entity prop);
// Units working a resource or crafting prop, or on their way to it
int propWorkerCount(World& w, Entity prop);
// What a unit is doing, for the Unit Panel: the skill it is working with (such as Mining or Cooking), Resting, Moving
// or Idle
std::string taskNameOf(World& w, Entity u);
