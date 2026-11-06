#pragma once
#include <string>
#include <vector>
#include "ecs.h"

// The lines the hover list shows for an Altar: its health and upkeep. Empty for any other prop.
std::vector<std::string> altarInfoLines(World& w, Entity e);

// The lines Show Prop Info puts under a prop's name: who owns it, its health or condition and whether it is decaying,
// its lock (and whether it is open to the player), and an Altar's upkeep. Empty when there is nothing to say.
std::vector<std::string> propInfoLines(World& w, Entity e);
