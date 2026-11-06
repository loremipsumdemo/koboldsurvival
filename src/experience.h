#pragma once
#include <string>
#include "ecs.h"
#include "game_log.h"
#include "stats.h"

void grantXp(World& w, Entity e, StatId stat, float amount, const std::string& why = std::string());
Stats statsOf(World& w, Entity e);
