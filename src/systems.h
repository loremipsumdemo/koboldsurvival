#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "resources.h"
#include "terrain.h"

// Spawns the units, the red units and the wagon
void setupWorld(World& w, Terrain& t, Rng& rng);

// Creates a squad from the given units and sends it to a ground point
void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint);

// Systems, run once per frame in this order
void squadSystem(World& w, float dt);
void steeringSystem(World& w, Terrain& t, float dt);
void cleanupSquads(World& w);
