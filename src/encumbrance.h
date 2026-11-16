#pragma once
#include "components.h"
#include "ecs.h"
#include "stats.h"

constexpr float SNEAK_SPEED = 0.5f;
constexpr float RUN_SPEED_BONUS = 2.0f;   // running is 3 times walking: 1 + this
constexpr int RUN_HUNGER_FACTOR = 3;      // hunger ticks this many times as fast, once a unit has run since its last tick
constexpr float MIN_LOAD_SPEED = 0.1f;   // the least share of its speed a load leaves a unit

float wornWeight(const Equipment& worn);

float carriedWeightOf(World& w, ObjectId e);

float encumbranceFor(float carried, const Stats& s);
float encumbranceOf(World& w, ObjectId e);

float walkSpeedOf(World& w, ObjectId e, bool running = false);
