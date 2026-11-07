#pragma once
#include "ecs.h"

int factionOf(World& w, ObjectId e);
bool unitsHostile(World& w, ObjectId a, ObjectId b);
bool hostileToPlayer(World& w, ObjectId e);
void provoke(World& w, ObjectId unit, int faction);
void factionSystem(World& w, float dt);
