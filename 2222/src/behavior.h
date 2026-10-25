#pragma once
#include <cstddef>
#include "combat.h"
#include "ecs.h"

// Runs every unit's behavior rules, from the top of each list down. A rule is checked only when the value it
// watches has changed, for a unit in its condition group, or when the rule's unit was edited, so a value that no
// enabled rule watches is never looked at. A rule whose condition holds carries out its action once. Actions change
// values, and those changes are checked on the next call. Returns how many rules were checked.
size_t behaviorSystem(World& w, CombatLog& log);
