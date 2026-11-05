#pragma once
#include <string>
#include <vector>
#include "ecs.h"

// The lines the hover list shows for a flag: its faction, its health and whether its claim has a vault. Empty for any
// entity that is not a flag.
std::vector<std::string> flagInfoLines(World& w, Entity e);

// The lines Show Prop Info puts under a prop's name: who owns it, its health or condition and whether it is decaying,
// its lock (and whether it is open to the player), and a vault's upkeep. Empty when there is nothing to say.
std::vector<std::string> propInfoLines(World& w, Entity e);
