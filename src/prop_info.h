#pragma once
#include "sounds.h"
#include "props.h"
#include <string>
#include <vector>
#include "ecs.h"

std::vector<std::string> altarInfoLines(World& w, Entity e);

std::vector<std::string> propInfoLines(World& w, Entity e);

// The sound a prop makes while worked, and when it finishes an item. Sfx::Count: none.
Sfx propWorkSound(PropKind kind);
Sfx propDoneSound(PropKind kind);
