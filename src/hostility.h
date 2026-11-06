#pragma once
#include "ecs.h"

int factionOf(World& w, Entity e);
bool unitsHostile(World& w, Entity a, Entity b);
bool hostileToPlayer(World& w, Entity e);
void provoke(World& w, Entity unit, int faction);
void factionSystem(World& w, float dt);
