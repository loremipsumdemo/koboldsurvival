#pragma once
#include "components.h"
#include "ecs.h"
#include "stats.h"

// Weight, encumbrance and how fast a unit walks.

constexpr float SNEAK_SPEED = 0.5f;         // a sneaking unit walks at this share of its speed, and never runs
constexpr float ENCUMBRANCE_XP_FROM = 0.5f; // Strength XP starts once encumbrance reaches this
constexpr float ENDURANCE_XP_PER_TILE = 0.1f;
constexpr float RUN_SPEED_BONUS = 0.5f;     // running is this much faster than walking, before Athletics
constexpr float RUN_ATHLETICS_PER_LEVEL = 0.01f; // and each Athletics level above 1 adds this much more to run speed

// Weight of everything worn
float wornWeight(const Equipment& worn);

// Weight carried: the inventory plus everything worn
float carriedWeightOf(World& w, Entity e);

// 0..1: carried weight against the carry limit that Strength gives. At or above the limit it is 1.
float encumbranceFor(float carried, const Stats& s);
float encumbranceOf(World& w, Entity e);

// Tiles per second a unit can walk now. Its base speed, Athletics, and the encumbrance all count; a
// unit with no stamina left walks at half speed; a downed unit walks at zero. Every unit uses the same
// formula, so a red unit's base speed and stats count the same way. Running is faster again:
// RUN_SPEED_BONUS, plus RUN_ATHLETICS_PER_LEVEL for each Athletics level above 1. Only a player unit can
// run, since the run and walk choice is the player's.
float walkSpeedOf(World& w, Entity e, bool running = false);

// Strength XP for walking: nothing below ENCUMBRANCE_XP_FROM, then rising to 0.5 XP a second at a
// full load. Only counts while the unit is moving, which the caller checks.
float strengthXpWhileWalking(float encumbrance, float seconds);
