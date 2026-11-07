#pragma once
#include <string>
#include "ecs.h"
#include "game_log.h"
#include "stats.h"

void grantXp(World& w, ObjectId e, StatId stat, float amount, const char* whyKey = nullptr);
Stats statsOf(World& w, ObjectId e);
