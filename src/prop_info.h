#pragma once
#include "sounds.h"
#include "props.h"
#include <string>
#include <vector>
#include "ecs.h"

std::vector<std::string> altarInfoLines(World& w, Entity e);

// What the hover box says about a prop. The first line is its name.
std::vector<std::string> propInfoLines(World& w, Entity e);

// The sound a prop makes while worked, and when it finishes an item. Sfx::Count: none.
Sfx propWorkSound(PropKind kind);
Sfx propDoneSound(PropKind kind);
