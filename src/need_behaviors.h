#pragma once
#include <cstddef>
#include "combat.h"
#include "ecs.h"

constexpr float SHARE_RANGE = TALK_RANGE;
constexpr float SHARE_SEEK_RANGE = 10.0f;
constexpr float SHARE_WALK_STOP = 0.8f;
bool canShareWith(World& w, ObjectId g, ObjectId taker, bool food, int takerValue);

size_t behaviorSystem(World& w, float dt);
