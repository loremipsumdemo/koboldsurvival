#pragma once
#include "components.h"
#include "ecs.h"
#include "stats.h"

constexpr float SNEAK_SPEED = 0.5f;
constexpr float RUN_SPEED_BONUS = 0.5f;

float wornWeight(const Equipment& worn);

float carriedWeightOf(World& w, Entity e);

float encumbranceFor(float carried, const Stats& s);
float encumbranceOf(World& w, Entity e);

float walkSpeedOf(World& w, Entity e, bool running = false);
