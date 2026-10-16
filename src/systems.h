#pragma once
#include "ecs.h"
#include "resources.h"

constexpr int GRID = 16;
constexpr float MOVE_TIME = 0.1f;

inline float tileCenter(int g) { return g + 0.5f; }

Entity findPlayer(World& w);

void spawnFloor(World& w, const Assets& a);
void generateLevel(World& w, const Assets& a, Rng& rng);

// Each of these runs once per frame, in this order.
void moveRequestSystem(World& w, Commands& cmds);
void movementSystem(World& w, float dt);
void pickupSystem(World& w);
void levelSystem(World& w, const Assets& a, Rng& rng);
