#pragma once
#include "components.h"
#include "ecs.h"
#include "stats.h"

// Weight, encumbrance and how fast a unit walks.

constexpr float SNEAK_SPEED = 0.5f;         // a sneaking unit walks at this share of its speed, and never runs
constexpr float RUN_SPEED_BONUS = 0.5f;     // running is this much faster than walking

// Weight of everything worn
float wornWeight(const Equipment& worn);

// Weight carried: the inventory plus everything worn
float carriedWeightOf(World& w, Entity e);

// 0..1: carried weight against the carry limit that Strength gives. At or above the limit it is 1.
float encumbranceFor(float carried, const Stats& s);
float encumbranceOf(World& w, Entity e);

// Tiles per second a unit can walk now. Its base speed and the encumbrance count; a downed unit walks at zero. Every
// unit uses the same formula. Running is RUN_SPEED_BONUS faster. Only a player unit can run, since the run and walk
// choice is the player's.
float walkSpeedOf(World& w, Entity e, bool running = false);
