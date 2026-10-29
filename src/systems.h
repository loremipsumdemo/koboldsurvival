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
