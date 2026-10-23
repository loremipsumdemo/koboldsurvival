#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "resources.h"
#include "terrain.h"

// Spawns the units, the red units and the wagon
void setupWorld(World& w, Terrain& t, Rng& rng);

// Starting stats drawn the way new units get them: attributes at levels 5..15, skills at 1..5
Stats randomStartingStats(Rng& rng);

// A unit name from the list that is not in taken
std::string randomUnitName(Rng& rng, const std::vector<std::string>& taken);

// Puts `perItem` copies of every item in the database on the ground at random walkable tiles across
// the whole level. Each copy is a single item.
void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem);

// Creates a squad from the given units and sends it to a ground point
void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint);

// Sends each unit that carries items to a ground item. Whichever gets in reach first takes it.
void issuePickupOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity item);

// Moves the whole stack in one inventory slot onto the ground, under its unit. False if the slot is empty.
bool dropStack(World& w, Entity unit, int slot);

// Systems, run once per frame in this order
void needsSystem(World& w, int hours);          // health and hunger, for each whole in-game hour that passed
void squadSystem(World& w, float dt);
void steeringSystem(World& w, Terrain& t, float dt);
void pickupSystem(World& w);                     // after steering, so units that just arrived can take items
void cleanupSquads(World& w);
