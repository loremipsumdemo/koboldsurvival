#pragma once
#include <cstddef>
#include "combat.h"
#include "ecs.h"

// Share hands items over at trading range: a unit this close to another can take its food or healing item
constexpr float SHARE_RANGE = TALK_RANGE;

// Runs every unit's Eat and Heal behaviors (behavior_rules.h), from the top of each list down. One is checked when the
// stat it watches has changed, when the unit's list was edited, or when the wait after a failed try (no item to use,
// none to share) has run out. dt is the real seconds since the last call, for those waits. The automated behaviors are
// worked by automationSystem (systems.h). Returns how many behaviors were checked.
size_t behaviorSystem(World& w, CombatLog& log, float dt);
