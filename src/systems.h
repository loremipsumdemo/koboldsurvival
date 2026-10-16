#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "resources.h"
#include "terrain.h"

Door* findDoor(World& w);

// Spawns the building, the units, and the keys
void setupWorld(World& w, Terrain& t, Rng& rng);

// Creates a squad from the given units and sends it to a tile
void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units,
                    GridPos clickTile, bool clickedDoor);

// Systems, run once per frame in this order
void squadSystem(World& w, float dt);
void steeringSystem(World& w, Terrain& t, GameState& gs, float dt);
void pickupSystem(World& w, Terrain& t, GameState& gs);
void cleanupSquads(World& w);
void updateWinState(World& w, GameState& gs);
